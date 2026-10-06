#include "veyra/files/file_shell.hpp"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

void require(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

class TemporaryDirectory {
  public:
    TemporaryDirectory() {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        path_ =
            std::filesystem::temp_directory_path() / ("veyra-file-shell-" + std::to_string(stamp));
        std::error_code error;
        if (!std::filesystem::create_directory(path_, error)) {
            throw std::filesystem::filesystem_error("create temporary directory", path_, error);
        }
    }

    ~TemporaryDirectory() {
        std::error_code error;
        std::filesystem::remove_all(path_, error);
    }

    const std::filesystem::path& path() const noexcept {
        return path_;
    }

  private:
    std::filesystem::path path_;
};

#ifdef __linux__
class ScopedEnvironment {
  public:
    ScopedEnvironment(const char* name, std::string_view value) : name_(name) {
        if (const auto* previous = std::getenv(name); previous != nullptr) {
            previous_ = previous;
        }
        set(value);
    }

    void set(std::string_view value) {
        if (::setenv(name_.c_str(), std::string(value).c_str(), 1) != 0) {
            throw std::runtime_error("could not set test environment variable");
        }
    }

    ~ScopedEnvironment() {
        if (previous_) {
            ::setenv(name_.c_str(), previous_->c_str(), 1);
        } else {
            ::unsetenv(name_.c_str());
        }
    }

  private:
    std::string name_;
    std::optional<std::string> previous_;
};

int run_xdg_open_stub(int argc, char** argv) {
    const auto* expected = std::getenv("VEYRA_FILE_SHELL_TEST_EXPECTED");
    if (argc != 2 || expected == nullptr) {
        return 1;
    }
    return std::string_view(argv[1]) == expected ? 0 : 2;
}

void check_linux_shell_arguments(const char* executable_path) {
    auto fixture = TemporaryDirectory{};
    const auto executable = std::filesystem::canonical(executable_path);
    const auto command_path = fixture.path() / "xdg-open";
    std::filesystem::create_symlink(executable, command_path);

    const auto target = fixture.path() / "path with spaces;$(not-a-command).txt";
    const auto path_environment = ScopedEnvironment("PATH", fixture.path().string());
    auto expected_environment =
        ScopedEnvironment("VEYRA_FILE_SHELL_TEST_EXPECTED", target.string());

    require(
        !veyra::perform_file_shell_action(veyra::FileShellAction::open, target),
        "open passes a file path as one argument"
    );

    expected_environment.set(fixture.path().string());
    require(
        !veyra::perform_file_shell_action(veyra::FileShellAction::reveal, target),
        "reveal opens the file's containing directory"
    );

    std::filesystem::remove(command_path);
    require(
        veyra::perform_file_shell_action(veyra::FileShellAction::open, target) ==
            std::make_error_code(std::errc::no_such_file_or_directory),
        "a missing shell launcher returns its spawn error"
    );
}

std::string read_file(const std::filesystem::path& path) {
    auto input = std::ifstream(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

void check_linux_trash_action() {
    auto fixture = TemporaryDirectory{};
    const auto data_home = fixture.path() / "data";
    const auto xdg_environment = ScopedEnvironment("XDG_DATA_HOME", data_home.string());
    const auto source_directory = fixture.path() / "source";
    std::filesystem::create_directory(source_directory);
    const auto missing_source = source_directory / "missing.txt";
    require(
        veyra::perform_file_shell_action(veyra::FileShellAction::trash, missing_source) ==
            std::make_error_code(std::errc::no_such_file_or_directory),
        "trash reports missing sources without deleting anything"
    );

    const auto source = source_directory / "café ;$(bad).txt";
    {
        auto output = std::ofstream(source, std::ios::binary);
        output << "first item";
    }

    require(
        !veyra::perform_file_shell_action(veyra::FileShellAction::trash, source),
        "trash moves a file into the user trash"
    );
    require(!std::filesystem::exists(source), "a trashed source path is removed");

    const auto files_directory = data_home / "Trash" / "files";
    const auto info_directory = data_home / "Trash" / "info";
    const auto first = std::filesystem::directory_iterator(files_directory);
    require(first != std::filesystem::directory_iterator{}, "trash stores the moved file");
    const auto& first_item = *first;
    const auto& stored_file = first_item.path();
    const auto info_file = info_directory / (stored_file.filename().string() + ".trashinfo");
    require(std::filesystem::exists(info_file), "each trashed file has metadata");
    require(read_file(stored_file) == "first item", "trash preserves file contents");

    const auto info = read_file(info_file);
    require(
        info.starts_with("[Trash Info]\nPath=") &&
            info.find("caf%C3%A9%20%3B%24%28bad%29.txt") != std::string::npos,
        "trash metadata stores the escaped original path"
    );
    const auto deletion_date = info.find("\nDeletionDate=");
    require(deletion_date != std::string::npos, "trash metadata records the deletion date");
    require(
        info.size() >= deletion_date + 14 + 20 && info[deletion_date + 14 + 19] == '\n',
        "deletion date uses the desktop trash timestamp format"
    );

    const auto second_directory = fixture.path() / "second";
    std::filesystem::create_directory(second_directory);
    const auto duplicate_name = second_directory / source.filename();
    {
        auto output = std::ofstream(duplicate_name, std::ios::binary);
        output << "second item";
    }
    require(
        !veyra::perform_file_shell_action(veyra::FileShellAction::trash, duplicate_name),
        "trash accepts a repeated original filename"
    );
    auto stored_count = std::size_t{0};
    for ([[maybe_unused]] const auto& entry :
         std::filesystem::directory_iterator(files_directory)) {
        ++stored_count;
    }
    require(stored_count == 2, "repeated names do not overwrite earlier trash entries");

    const auto symlink_target = fixture.path() / "symlink-target.txt";
    {
        auto output = std::ofstream(symlink_target, std::ios::binary);
        output << "target remains";
    }
    const auto symlink = source_directory / "symlink.txt";
    std::filesystem::create_symlink(symlink_target, symlink);
    require(
        !veyra::perform_file_shell_action(veyra::FileShellAction::trash, symlink),
        "trash moves a symbolic link as a link"
    );
    require(!std::filesystem::exists(symlink), "trashed symbolic link is removed");
    require(read_file(symlink_target) == "target remains", "trashing a link leaves its target");
}
#endif

void check_invalid_requests() {
    require(
        veyra::perform_file_shell_action(veyra::FileShellAction::open, "relative") ==
            std::make_error_code(std::errc::invalid_argument),
        "shell actions require absolute paths"
    );
}

}

int main(int argc, char** argv) {
#ifdef __linux__
    if (std::filesystem::path(argv[0]).filename() == "xdg-open") {
        return run_xdg_open_stub(argc, argv);
    }
#else
    static_cast<void>(argc);
    static_cast<void>(argv);
#endif

    try {
        check_invalid_requests();
#ifdef __linux__
        check_linux_shell_arguments(argv[0]);
        check_linux_trash_action();
#endif
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
