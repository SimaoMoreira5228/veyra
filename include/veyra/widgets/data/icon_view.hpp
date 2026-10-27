#ifndef VEYRA_WIDGETS_DATA_ICON_VIEW_HPP
#define VEYRA_WIDGETS_DATA_ICON_VIEW_HPP

#include "veyra/layout/core/geometry.hpp"
#include "veyra/layout/image/image_fit.hpp"
#include "veyra/render/cpu/software_renderer.hpp"
#include "veyra/style/theme.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

namespace veyra {

struct IconStyle {
    bool tintable{};
    bool has_badge{};
};

struct IconAppearance {
    Srgba8 box;
    Srgba8 badge;
    Srgba8 outline;
};

IconAppearance icon_appearance(const Theme& theme, IconStyle style) noexcept;

struct IconContent {
    const SoftwareImage* light{};
    const SoftwareImage* dark{};
    bool dark_ui{};
    bool tintable{};
    std::uint32_t badge_count{};
    bool selected{};
    SoftwareImageSampling sampling{SoftwareImageSampling::nearest};
};

struct IconGeometry {
    LayoutRect box{};
    ImageFitMode fit{ImageFitMode::contain};
};

enum class IconEmitResult : std::uint8_t {
    complete,
    invalid_input,
    insufficient_output,
};

struct IconOutput {
    std::size_t placed{};
    std::size_t required{};
    LayoutRect badge_rect{};
    IconEmitResult result{IconEmitResult::complete};
};

constexpr std::size_t kIconBadgeCap = 99;
constexpr std::size_t kIconBadgeTextMax = 3;

std::size_t icon_badge_text(std::uint32_t count, std::span<char> out) noexcept;
LayoutRect icon_badge_rect(const Theme& theme, LayoutRect box, std::uint32_t count) noexcept;

IconOutput emit_icon(
    const Theme& theme,
    const IconContent& content,
    const IconGeometry& geometry,
    std::span<DisplayCommand> commands_out
) noexcept;

}

#endif
