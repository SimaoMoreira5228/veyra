#include "veyra/text/font_discovery.hpp"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>

namespace veyra {
namespace {

constexpr char to_lower(char value) noexcept {
    if (value >= 'A' && value <= 'Z') {
        return static_cast<char>(value - 'A' + 'a');
    }
    return value;
}

bool extension_matches(std::string_view extension, std::string_view wanted) noexcept {
    if (extension.size() != wanted.size()) {
        return false;
    }
    for (auto index = std::size_t{}; index < extension.size(); ++index) {
        if (to_lower(extension[index]) != wanted[index]) {
            return false;
        }
    }
    return true;
}

bool has_font_extension(std::string_view filename) noexcept {
    if (filename.size() < 5) {
        return false;
    }
    const auto extension = filename.substr(filename.size() - 4);
    return extension_matches(extension, ".ttf") || extension_matches(extension, ".otf") ||
        extension_matches(extension, ".ttc") || extension_matches(extension, ".otc");
}

bool contains_insensitive(std::string_view haystack, std::string_view needle) noexcept {
    if (needle.empty() || needle.size() > haystack.size()) {
        return false;
    }
    for (auto start = std::size_t{}; start + needle.size() <= haystack.size(); ++start) {
        auto match = true;
        for (auto offset = std::size_t{}; offset < needle.size(); ++offset) {
            if (to_lower(haystack[start + offset]) != to_lower(needle[offset])) {
                match = false;
                break;
            }
        }
        if (match) {
            return true;
        }
    }
    return false;
}

bool wanted_matches(
    std::string_view filename,
    std::span<const std::string_view> wanted_families
) noexcept {
    if (wanted_families.empty()) {
        return true;
    }
    for (const auto family : wanted_families) {
        if (contains_insensitive(filename, family)) {
            return true;
        }
    }
    return false;
}

bool filenames_less(
    const std::filesystem::path& left,
    const std::filesystem::path& right
) noexcept {
    return left.filename().native() < right.filename().native();
}

}

FontDiscoveryResult discover_fonts_in(
    std::span<const std::string_view> directories,
    std::span<const std::string_view> wanted_families,
    std::span<std::filesystem::path> output
) noexcept {
    auto count = std::size_t{};
    auto truncated = false;

    const auto append = [&](const std::filesystem::path& candidate) noexcept {
        for (auto index = std::size_t{}; index < count; ++index) {
            if (output[index].native() == candidate.native()) {
                return;
            }
        }
        if (count >= output.size()) {
            truncated = true;
            return;
        }
        output[count++] = candidate;
    };

    for (const auto directory : directories) {
        if (directory.empty()) {
            continue;
        }
        std::error_code error;
        auto iterator =
            std::filesystem::directory_iterator(std::filesystem::path{directory}, error);
        if (error) {
            continue;
        }
        const auto segment_start = count;
        const auto end = std::filesystem::directory_iterator{};
        while (iterator != end && !truncated) {
            const auto entry_path = iterator->path();
            std::error_code entry_error;
            const auto regular = iterator->is_regular_file(entry_error);
            if (!entry_error && regular) {
#ifdef _WIN32
                const std::string narrow = entry_path.filename().string();
                const std::string_view filename{narrow};
#else
                const auto name = entry_path.filename();
                const std::string_view filename{name.native()};
#endif
                if (has_font_extension(filename) && wanted_matches(filename, wanted_families)) {
                    append(entry_path);
                }
            }
            iterator.increment(error);
            if (error) {
                break;
            }
        }
        std::sort(output.begin() + segment_start, output.begin() + count, filenames_less);
        if (truncated) {
            break;
        }
    }

    return {.count = count, .truncated = truncated};
}

FontDiscoveryResult discover_fonts_in(
    std::span<const std::string_view> directories,
    std::span<std::filesystem::path> output
) noexcept {
    return discover_fonts_in(directories, std::span<const std::string_view>{}, output);
}

FontDiscoveryResult default_font_search_paths(std::span<std::filesystem::path> output) noexcept {
    auto count = std::size_t{};
    auto truncated = false;

    const auto emit = [&](const std::filesystem::path& candidate) noexcept {
        if (count >= output.size()) {
            truncated = true;
            return;
        }
        output[count++] = candidate;
    };

#ifdef _WIN32
    if (const auto* root = std::getenv("WINDIR"); root != nullptr && root[0] != '\0') {
        const auto fonts = std::filesystem::path{root} / "Fonts";
        emit(fonts / "segoeui.ttf");
        emit(fonts / "arial.ttf");
    }
#elif defined(__APPLE__)
    emit("/System/Library/Fonts/Supplemental/Arial.ttf");
    emit("/System/Library/Fonts/SFNS.ttf");
#else
    emit("/usr/share/fonts/google-noto/NotoSans-Regular.ttf");
    emit("/usr/share/fonts/dejavu-sans-fonts/DejaVuSans.ttf");
    emit("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf");
    emit("/usr/share/fonts/liberation-sans-fonts/LiberationSans-Regular.ttf");
#endif

    return {.count = count, .truncated = truncated};
}

}
