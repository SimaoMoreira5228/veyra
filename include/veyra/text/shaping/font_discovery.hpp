#ifndef VEYRA_TEXT_FONT_DISCOVERY_HPP
#define VEYRA_TEXT_FONT_DISCOVERY_HPP

#include <cstddef>
#include <filesystem>
#include <span>
#include <string_view>

namespace veyra {

constexpr std::size_t kFontDiscoveryCapacity = 16;

struct FontDiscoveryResult {
    std::size_t count{};
    bool truncated{};
};

// NOTE: discovery itself performs no container allocation and never touches the fallback
// resolver; the std::filesystem calls below may allocate internally, so treat both entry
// points as a startup platform service and never call them on the frame path.
FontDiscoveryResult discover_fonts_in(
    std::span<const std::string_view> directories,
    std::span<const std::string_view> wanted_families,
    std::span<std::filesystem::path> output
) noexcept;

FontDiscoveryResult discover_fonts_in(
    std::span<const std::string_view> directories,
    std::span<std::filesystem::path> output
) noexcept;

FontDiscoveryResult default_font_search_paths(std::span<std::filesystem::path> output) noexcept;

}

namespace veyra::text {
using ::veyra::FontDiscoveryResult;
using ::veyra::default_font_search_paths;
using ::veyra::discover_fonts_in;
using ::veyra::kFontDiscoveryCapacity;
}

namespace veyra::text::shaping {
using ::veyra::FontDiscoveryResult;
using ::veyra::default_font_search_paths;
using ::veyra::discover_fonts_in;
using ::veyra::kFontDiscoveryCapacity;
}

#endif
