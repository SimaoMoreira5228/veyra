#include "veyra/files/shell/file_shell.hpp"

#include <array>
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <limits>
#include <new>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

#ifdef _WIN32
#include <objbase.h>
#include <shellapi.h>
#include <windows.h>
#elif defined(__linux__)
#include <fcntl.h>
#include <linux/fs.h>
#include <spawn.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>

extern char** environ;
#endif

namespace veyra {
namespace {

#ifdef _WIN32
class ComApartment {
  public:
    ComApartment() noexcept : result_(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)) {
    }

    ~ComApartment() {
        if (SUCCEEDED(result_)) {
            CoUninitialize();
        }
    }

    HRESULT result() const noexcept {
        return result_;
    }

  private:
    HRESULT result_;
};

std::error_code launch_windows(const std::filesystem::path& target) noexcept {
    const auto apartment = ComApartment{};
    if (FAILED(apartment.result())) {
        return {static_cast<int>(apartment.result()), std::system_category()};
    }

    auto operation = SHELLEXECUTEINFOW{};
    operation.cbSize = sizeof(operation);
    operation.fMask = SEE_MASK_FLAG_NO_UI;
    operation.lpVerb = L"open";
    operation.lpFile = target.c_str();
    operation.nShow = SW_SHOWNORMAL;
    if (!ShellExecuteExW(&operation)) {
        return {static_cast<int>(GetLastError()), std::system_category()};
    }
    return {};
}

std::error_code trash_windows(const std::filesystem::path& path) {
    const auto apartment = ComApartment{};
    if (FAILED(apartment.result())) {
        return {static_cast<int>(apartment.result()), std::system_category()};
    }

    auto source = path.native();
    source.push_back(L'\0');
    source.push_back(L'\0');

    auto operation = SHFILEOPSTRUCTW{};
    operation.wFunc = FO_DELETE;
    operation.pFrom = source.data();
    operation.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_NOERRORUI | FOF_SILENT;
    const auto result = SHFileOperationW(&operation);
    if (result != 0) {
        return {result, std::system_category()};
    }
    if (operation.fAnyOperationsAborted) {
        return std::make_error_code(std::errc::operation_canceled);
    }
    return {};
}
#elif defined(__linux__)
std::error_code launch_linux(const std::filesystem::path& target) noexcept {
    auto arguments = std::array{
        const_cast<char*>("xdg-open"),
        const_cast<char*>(target.c_str()),
        static_cast<char*>(nullptr),
    };
    auto process = pid_t{};
    const auto spawn_error =
        posix_spawnp(&process, "xdg-open", nullptr, nullptr, arguments.data(), environ);
    if (spawn_error != 0) {
        return {spawn_error, std::generic_category()};
    }

    auto status = int{};
    while (::waitpid(process, &status, 0) < 0) {
        if (errno == EINTR) {
            continue;
        }
        return {errno, std::generic_category()};
    }
    if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
        return {};
    }
    if (WIFSIGNALED(status)) {
        return std::make_error_code(std::errc::operation_canceled);
    }
    return std::make_error_code(std::errc::io_error);
}

class ScopedDescriptor {
  public:
    explicit ScopedDescriptor(int value = -1) noexcept : value_(value) {
    }

    ScopedDescriptor(const ScopedDescriptor&) = delete;
    ScopedDescriptor& operator=(const ScopedDescriptor&) = delete;

    ScopedDescriptor(ScopedDescriptor&& other) noexcept : value_(other.release()) {
    }

    ScopedDescriptor& operator=(ScopedDescriptor&& other) noexcept {
        if (this == &other) {
            return *this;
        }
        reset(other.release());
        return *this;
    }

    ~ScopedDescriptor() {
        reset();
    }

    int get() const noexcept {
        return value_;
    }

    int release() noexcept {
        const auto value = value_;
        value_ = -1;
        return value;
    }

    std::error_code close() noexcept {
        const auto value = release();
        if (value < 0 || ::close(value) == 0) {
            return {};
        }
        return {errno, std::generic_category()};
    }

    void reset(int value = -1) noexcept {
        if (value_ >= 0) {
            ::close(value_);
        }
        value_ = value;
    }

  private:
    int value_;
};

std::error_code
open_private_directory(const std::filesystem::path& path, ScopedDescriptor& descriptor) noexcept {
    if (::mkdir(path.c_str(), 0700) < 0 && errno != EEXIST) {
        return {errno, std::generic_category()};
    }

    const auto value = ::open(path.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    if (value < 0) {
        return {errno, std::generic_category()};
    }
    auto opened = ScopedDescriptor(value);

    struct stat status{};
    if (::fstat(opened.get(), &status) < 0) {
        return {errno, std::generic_category()};
    }
    if (!S_ISDIR(status.st_mode) || status.st_uid != ::geteuid()) {
        return std::make_error_code(std::errc::permission_denied);
    }
    if (::fchmod(opened.get(), 0700) < 0) {
        return {errno, std::generic_category()};
    }

    descriptor = std::move(opened);
    return {};
}

std::filesystem::path data_home_path() {
    if (const auto* value = std::getenv("XDG_DATA_HOME"); value != nullptr && value[0] != '\0') {
        const auto path = std::filesystem::path(value);
        if (path.is_absolute()) {
            return path;
        }
    }

    if (const auto* value = std::getenv("HOME"); value != nullptr && value[0] != '\0') {
        const auto home = std::filesystem::path(value);
        if (home.is_absolute()) {
            return home / ".local" / "share";
        }
    }
    return {};
}

bool is_within_path(
    const std::filesystem::path& path,
    const std::filesystem::path& parent
) noexcept {
    auto path_part = path.begin();
    auto parent_part = parent.begin();
    while (parent_part != parent.end()) {
        if (path_part == path.end() || *path_part != *parent_part) {
            return false;
        }
        ++path_part;
        ++parent_part;
    }
    return true;
}

std::string percent_encode(std::string_view value) {
    constexpr auto hexadecimal = "0123456789ABCDEF";

    auto result = std::string{};
    result.reserve(value.size() * 3);

    for (const auto character : value) {
        const auto byte = static_cast<unsigned char>(character);
        const auto unreserved = (byte >= 'A' && byte <= 'Z') || (byte >= 'a' && byte <= 'z') ||
            (byte >= '0' && byte <= '9') || byte == '-' || byte == '_' || byte == '.' ||
            byte == '~' || byte == '/';
        if (unreserved) {
            result.push_back(static_cast<char>(byte));
            continue;
        }
        result.push_back('%');
        result.push_back(hexadecimal[byte >> 4]);
        result.push_back(hexadecimal[byte & 0x0f]);
    }
    return result;
}

std::error_code write_all(int descriptor, std::string_view value) noexcept {
    while (!value.empty()) {
        const auto written = ::write(descriptor, value.data(), value.size());
        if (written > 0) {
            value.remove_prefix(static_cast<std::size_t>(written));
            continue;
        }
        if (written < 0 && errno == EINTR) {
            continue;
        }
        return {written < 0 ? errno : EIO, std::generic_category()};
    }
    return {};
}

std::error_code deletion_timestamp(std::array<char, 32>& output) noexcept {
    const auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    auto local = std::tm{};
    if (::localtime_r(&now, &local) == nullptr) {
        return std::make_error_code(std::errc::value_too_large);
    }
    if (std::strftime(output.data(), output.size(), "%Y-%m-%dT%H:%M:%S", &local) == 0) {
        return std::make_error_code(std::errc::value_too_large);
    }
    return {};
}

std::error_code trash_linux(const std::filesystem::path& path) {
    struct stat source_status{};
    if (::lstat(path.c_str(), &source_status) < 0) {
        return {errno, std::generic_category()};
    }
    const auto original_name = path.filename().native();
    if (original_name.empty() || original_name == "." || original_name == "..") {
        return std::make_error_code(std::errc::invalid_argument);
    }

    const auto data_home = data_home_path();
    if (data_home.empty()) {
        return std::make_error_code(std::errc::invalid_argument);
    }
    auto filesystem_error = std::error_code{};
    std::filesystem::create_directories(data_home, filesystem_error);
    if (filesystem_error) {
        return filesystem_error;
    }
    struct stat data_home_status{};
    if (::stat(data_home.c_str(), &data_home_status) < 0) {
        return {errno, std::generic_category()};
    }
    if (source_status.st_dev != data_home_status.st_dev) {
        return std::make_error_code(std::errc::cross_device_link);
    }

    const auto trash = data_home / "Trash";
    const auto files = trash / "files";
    const auto info = trash / "info";
    auto trash_descriptor = ScopedDescriptor{};
    auto files_descriptor = ScopedDescriptor{};
    auto info_descriptor = ScopedDescriptor{};
    auto error = open_private_directory(trash, trash_descriptor);
    if (error) {
        return error;
    }
    error = open_private_directory(files, files_descriptor);
    if (error) {
        return error;
    }
    error = open_private_directory(info, info_descriptor);
    if (error) {
        return error;
    }

    auto source_parent_error = std::error_code{};
    const auto source_parent = std::filesystem::canonical(path.parent_path(), source_parent_error);
    if (source_parent_error) {
        return source_parent_error;
    }
    auto trash_error = std::error_code{};
    const auto canonical_trash = std::filesystem::canonical(trash, trash_error);
    if (trash_error) {
        return trash_error;
    }
    if (is_within_path(source_parent / path.filename(), canonical_trash)) {
        return std::make_error_code(std::errc::invalid_argument);
    }

    struct stat files_status{};
    if (::fstat(files_descriptor.get(), &files_status) < 0) {
        return {errno, std::generic_category()};
    }
    if (source_status.st_dev != files_status.st_dev) {
        return std::make_error_code(std::errc::cross_device_link);
    }

    auto timestamp = std::array<char, 32>{};
    error = deletion_timestamp(timestamp);
    if (error) {
        return error;
    }
    const auto encoded_path = percent_encode(path.native());
    const auto record = std::string{"[Trash Info]\nPath="} + encoded_path +
        "\nDeletionDate=" + timestamp.data() + "\n";
    auto candidate_base = original_name;
    if (candidate_base.size() > 160) {
        candidate_base.resize(160);
    }
    const auto process_id = std::to_string(::getpid());

    auto stored_name = std::string{};
    stored_name.reserve(candidate_base.size() + process_id.size() + 22);

    auto info_name = std::string{};
    info_name.reserve(stored_name.capacity() + 10);

    for (auto suffix = std::uint64_t{0};; ++suffix) {
        stored_name = candidate_base;
        stored_name.push_back('.');
        stored_name.append(process_id);
        stored_name.push_back('.');
        stored_name.append(std::to_string(suffix));

        info_name = stored_name;
        info_name += ".trashinfo";
        const auto info_file = ::openat(
            info_descriptor.get(),
            info_name.c_str(),
            O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW,
            0600
        );
        if (info_file < 0) {
            if (errno == EEXIST) {
                if (suffix == std::numeric_limits<std::uint64_t>::max()) {
                    return std::make_error_code(std::errc::file_exists);
                }
                continue;
            }
            return {errno, std::generic_category()};
        }

        auto info_file_descriptor = ScopedDescriptor(info_file);
        error = write_all(info_file_descriptor.get(), record);
        if (error) {
            ::unlinkat(info_descriptor.get(), info_name.c_str(), 0);
            return error;
        }
        error = info_file_descriptor.close();
        if (error) {
            ::unlinkat(info_descriptor.get(), info_name.c_str(), 0);
            return error;
        }

#ifdef SYS_renameat2
        const auto renamed = ::syscall(
            SYS_renameat2,
            AT_FDCWD,
            path.c_str(),
            files_descriptor.get(),
            stored_name.c_str(),
            RENAME_NOREPLACE
        );
#else
        const auto renamed = -1;
        errno = ENOSYS;
#endif
        if (renamed == 0) {
            return {};
        }

        const auto rename_error = errno;
        ::unlinkat(info_descriptor.get(), info_name.c_str(), 0);
        if (rename_error == EEXIST) {
            if (suffix == std::numeric_limits<std::uint64_t>::max()) {
                return std::make_error_code(std::errc::file_exists);
            }
            continue;
        }
        return {rename_error, std::generic_category()};
    }
}
#endif

}

std::error_code
perform_file_shell_action(FileShellAction action, const std::filesystem::path& path) noexcept {
    if (action != FileShellAction::open && action != FileShellAction::reveal &&
        action != FileShellAction::trash) {
        return std::make_error_code(std::errc::invalid_argument);
    }
    if (path.empty() || !path.is_absolute() ||
        path.native().find(std::filesystem::path::value_type{}) !=
            std::filesystem::path::string_type::npos) {
        return std::make_error_code(std::errc::invalid_argument);
    }

    if (action == FileShellAction::trash) {
#ifdef _WIN32
        try {
            return trash_windows(path);
        } catch (const std::bad_alloc&) {
            return std::make_error_code(std::errc::not_enough_memory);
        }
#elif defined(__linux__)
        try {
            return trash_linux(path);
        } catch (const std::bad_alloc&) {
            return std::make_error_code(std::errc::not_enough_memory);
        }
#else
        return std::make_error_code(std::errc::operation_not_supported);
#endif
    }

    if (action == FileShellAction::open) {
#ifdef _WIN32
        return launch_windows(path);
#elif defined(__linux__)
        return launch_linux(path);
#else
        return std::make_error_code(std::errc::operation_not_supported);
#endif
    }

    try {
        const auto parent = path.parent_path();
        const auto& target = parent.empty() ? path : parent;
#ifdef _WIN32
        return launch_windows(target);
#elif defined(__linux__)
        return launch_linux(target);
#else
        static_cast<void>(target);
        return std::make_error_code(std::errc::operation_not_supported);
#endif
    } catch (const std::bad_alloc&) {
        return std::make_error_code(std::errc::not_enough_memory);
    }
}

}
