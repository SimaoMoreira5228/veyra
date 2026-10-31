#include "veyra/text/shaping/font_discovery.hpp"

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace {

void require(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

struct TemporaryTree {
    std::filesystem::path path;

    explicit TemporaryTree(std::filesystem::path directory) : path(std::move(directory)) {
    }

    TemporaryTree(TemporaryTree&& other) noexcept : path(std::move(other.path)) {
        other.path.clear();
    }

    TemporaryTree(const TemporaryTree&) = delete;
    TemporaryTree& operator=(const TemporaryTree&) = delete;

    ~TemporaryTree() {
        std::error_code error;
        std::filesystem::remove_all(path, error);
    }
};

TemporaryTree make_tree() {
    static auto sequence = std::atomic<std::uint64_t>{};
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    auto root = std::filesystem::temp_directory_path() / "opencode" /
        ("veyra-font-discovery-" + std::to_string(stamp) + "-" +
         std::to_string(sequence.fetch_add(1, std::memory_order_relaxed)));
    std::error_code error;
    std::filesystem::create_directories(root, error);
    require(!error, "temporary font tree is created");
    return TemporaryTree(std::move(root));
}

void touch(const std::filesystem::path& file) {
    std::ofstream stream(file, std::ios::binary);
    require(stream.good(), "fixture font file is created");
}

std::string native_of(const std::filesystem::path& path) {
    return path.string();
}

void check_testdata_fonts_found() {
    static_assert(noexcept(veyra::discover_fonts_in(
        std::span<const std::string_view>{},
        std::span<std::filesystem::path>{}
    )));
    static_assert(noexcept(veyra::default_font_search_paths(std::span<std::filesystem::path>{})));

    const std::string fonts = std::string(VEYRA_TESTDATA_DIR) + "/fonts";
    const std::array<std::string_view, 1> directories{std::string_view{fonts}};
    auto output = std::array<std::filesystem::path, veyra::kFontDiscoveryCapacity>{};

    const auto found = veyra::discover_fonts_in(directories, output);
    require(!found.truncated, "testdata scan fits the fixed capacity");
    require(found.count == 2, "testdata fonts directory yields both samples");
    require(
        output[0].filename().string() == "cjk-sample.ttf" &&
            output[1].filename().string() == "latin-arabic.ttf",
        "testdata hits sort by filename"
    );
    for (auto index = std::size_t{}; index < found.count; ++index) {
        std::error_code error;
        require(
            std::filesystem::is_regular_file(output[index], error) && !error,
            "testdata hits are regular files"
        );
    }
}

void check_wanted_families_filter() {
    const std::string fonts = std::string(VEYRA_TESTDATA_DIR) + "/fonts";
    const std::array<std::string_view, 1> directories{std::string_view{fonts}};
    auto output = std::array<std::filesystem::path, veyra::kFontDiscoveryCapacity>{};

    const std::array<std::string_view, 1> arabic{std::string_view{"arabic"}};
    const auto arabic_only = veyra::discover_fonts_in(directories, arabic, output);
    require(
        arabic_only.count == 1 && !arabic_only.truncated &&
            output[0].filename().string() == "latin-arabic.ttf",
        "family filter keeps the matching file"
    );

    const std::array<std::string_view, 1> upper{std::string_view{"ARABIC"}};
    const auto upper_only = veyra::discover_fonts_in(directories, upper, output);
    require(upper_only.count == 1, "family matching ignores case");

    const std::array<std::string_view, 2> both{std::string_view{"cjk"}, std::string_view{"arabic"}};
    const auto both_found = veyra::discover_fonts_in(directories, both, output);
    require(both_found.count == 2, "every matching family is kept");

    const std::array<std::string_view, 1> missing{std::string_view{"no-such-family"}};
    const auto none_found = veyra::discover_fonts_in(directories, missing, output);
    require(none_found.count == 0 && !none_found.truncated, "an unmatched filter finds nothing");
}

void check_fake_tree_scan() {
    auto tree = make_tree();
    touch(tree.path / "beta.ttf");
    touch(tree.path / "alpha.ttf");
    touch(tree.path / "upper.TTF");
    touch(tree.path / "note.txt");
    std::error_code error;
    std::filesystem::create_directory(tree.path / "sub", error);
    require(!error, "fixture subdirectory is created");
    touch(tree.path / "sub" / "gamma.ttf");
    touch(tree.path / "plain-file");

    const std::string root = native_of(tree.path);
    const std::string plain = native_of(tree.path / "plain-file");
    const std::string missing = native_of(tree.path / "missing-dir");
    const std::array<std::string_view, 4> directories{
        std::string_view{root},
        std::string_view{root},
        std::string_view{plain},
        std::string_view{missing},
    };
    auto output = std::array<std::filesystem::path, veyra::kFontDiscoveryCapacity>{};

    const auto found = veyra::discover_fonts_in(directories, output);
    require(!found.truncated, "fake tree scan fits the fixed capacity");
    require(found.count == 3, "scan keeps fonts, drops text, files-as-dirs and missing dirs");
    require(
        output[0].filename().string() == "alpha.ttf" &&
            output[1].filename().string() == "beta.ttf" &&
            output[2].filename().string() == "upper.TTF",
        "scan sorts hits by filename and repeats a directory without duplicating"
    );
}

void check_truncation_contract() {
    auto tree = make_tree();
    for (auto index = 0; index < 5; ++index) {
        touch(tree.path / ("font-" + std::to_string(index) + ".ttf"));
    }
    const std::string root = native_of(tree.path);
    const std::array<std::string_view, 1> directories{std::string_view{root}};

    auto small = std::array<std::filesystem::path, 2>{};
    const auto clipped = veyra::discover_fonts_in(directories, small);
    require(clipped.count == 2 && clipped.truncated, "a full span reports truncation");

    auto roomy = std::array<std::filesystem::path, veyra::kFontDiscoveryCapacity>{};
    const auto retried = veyra::discover_fonts_in(directories, roomy);
    require(retried.count == 5 && !retried.truncated, "a bigger span retries without truncation");
    for (auto index = std::size_t{1}; index < retried.count; ++index) {
        require(
            roomy[index - 1].filename().string() < roomy[index].filename().string(),
            "retry keeps filename order"
        );
    }
}

void check_empty_inputs() {
    auto output = std::array<std::filesystem::path, veyra::kFontDiscoveryCapacity>{};

    const auto no_dirs = veyra::discover_fonts_in(std::span<const std::string_view>{}, output);
    require(no_dirs.count == 0 && !no_dirs.truncated, "no directories finds no fonts");

    const std::array<std::string_view, 2> blanks{std::string_view{}, std::string_view{""}};
    const auto blank_dirs = veyra::discover_fonts_in(blanks, output);
    require(blank_dirs.count == 0 && !blank_dirs.truncated, "blank directories are skipped");

    const std::array<std::string_view, 1> missing{std::string_view{"/no-such-font-dir-anywhere"}};
    const auto missing_dirs = veyra::discover_fonts_in(missing, output);
    require(missing_dirs.count == 0 && !missing_dirs.truncated, "missing dirs never fail");

    const std::string fonts = std::string(VEYRA_TESTDATA_DIR) + "/fonts";
    const std::array<std::string_view, 1> directories{std::string_view{fonts}};
    auto none = std::span<std::filesystem::path>{};
    const auto no_room = veyra::discover_fonts_in(directories, none);
    require(no_room.count == 0 && no_room.truncated, "an empty span reports truncation");
}

void check_default_table() {
    auto output = std::array<std::filesystem::path, veyra::kFontDiscoveryCapacity>{};
    const auto table = veyra::default_font_search_paths(output);
    require(!table.truncated, "the built-in table fits the fixed capacity");
    require(table.count <= output.size(), "the built-in table stays bounded");

#ifdef __linux__
    require(table.count == 4, "Linux probes Noto, DejaVu twice and Liberation");
    const std::array<std::string_view, 4> expected{
        "/usr/share/fonts/google-noto/NotoSans-Regular.ttf",
        "/usr/share/fonts/dejavu-sans-fonts/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/liberation-sans-fonts/LiberationSans-Regular.ttf",
    };
    for (auto index = std::size_t{}; index < table.count; ++index) {
        require(output[index].native() == expected[index], "Linux rows keep preference order");
    }
#elif defined(__APPLE__)
    require(table.count == 2, "macOS probes Supplemental Arial then SFNS");
    require(
        output[0].native() == "/System/Library/Fonts/Supplemental/Arial.ttf" &&
            output[1].native() == "/System/Library/Fonts/SFNS.ttf",
        "macOS rows keep preference order"
    );
#elif defined(_WIN32)
    require(table.count <= 2, "Windows probes at most segoeui then arial");
    for (auto index = std::size_t{}; index < table.count; ++index) {
        const std::string_view native{output[index].string()};
        require(
            native.ends_with("segoeui.ttf") || native.ends_with("arial.ttf"),
            "Windows rows stay inside Fonts"
        );
    }
#endif

    if (table.count > 1) {
        auto single = std::array<std::filesystem::path, 1>{};
        const auto clipped = veyra::default_font_search_paths(single);
        require(clipped.count == 1 && clipped.truncated, "a small span clips the built-in table");
    }
    auto empty = std::span<std::filesystem::path>{};
    const auto no_room = veyra::default_font_search_paths(empty);
    if (table.count == 0) {
        require(no_room.count == 0 && !no_room.truncated, "an empty table needs no room");
    } else {
        require(no_room.count == 0 && no_room.truncated, "an empty span clips a table");
    }
}

}

int main() {
    try {
        check_testdata_fonts_found();
        check_wanted_families_filter();
        check_fake_tree_scan();
        check_truncation_contract();
        check_empty_inputs();
        check_default_table();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
