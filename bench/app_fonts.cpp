#include "veyra/text/shaping/font_discovery.hpp"

#include <array>
#include <charconv>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

using Clock = std::chrono::steady_clock;

struct Options {
    std::size_t queries{200};
    std::size_t warmup{10};
};

std::size_t parse_size(const char* value) {
    const std::string_view text(value);
    std::size_t parsed = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), parsed);
    if (error != std::errc{} || end != text.data() + text.size()) {
        throw std::invalid_argument("expected a non-negative integer");
    }
    return parsed;
}

Options parse_options(int argc, char** argv) {
    auto options = Options{};
    for (int index = 1; index < argc; ++index) {
        const std::string_view argument(argv[index]);
        if (index + 1 >= argc) {
            throw std::invalid_argument("missing value");
        }
        const auto value = parse_size(argv[++index]);
        if (argument == "--queries") {
            options.queries = value;
        } else if (argument == "--warmup") {
            options.warmup = value;
        } else {
            throw std::invalid_argument("unknown option");
        }
    }
    if (options.queries == 0 || options.queries > 100000 || options.warmup > 100000) {
        throw std::invalid_argument("bench range overflow");
    }
    return options;
}

std::size_t scan_testdata() {
    const std::string fonts = std::string(VEYRA_TESTDATA_DIR) + "/fonts";
    const std::array<std::string_view, 1> wanted{std::string_view(fonts)};
    std::array<std::filesystem::path, veyra::kFontDiscoveryCapacity> output{};
    return veyra::discover_fonts_in(wanted, output).count;
}

std::size_t scan_system() {
    std::array<std::filesystem::path, veyra::kFontDiscoveryCapacity> output{};
    return veyra::default_font_search_paths(output).count;
}

int run(int argc, char** argv) {
    const Options options = parse_options(argc, argv);

    for (std::size_t index = 0; index < options.warmup; ++index) {
        scan_testdata();
        scan_system();
    }

    const auto start = Clock::now();
    auto found = std::size_t{0};
    for (std::size_t index = 0; index < options.queries; ++index) {
        found += scan_testdata();
        found += scan_system();
    }
    const auto elapsed = Clock::now() - start;
    const auto micros = std::chrono::duration_cast<std::chrono::microseconds>(elapsed).count();

    std::cout << "app_fonts queries=" << options.queries << " found=" << found
              << " micros=" << micros << '\n';
    return 0;
}

}

int main(int argc, char** argv) {
    try {
        return run(argc, argv);
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
