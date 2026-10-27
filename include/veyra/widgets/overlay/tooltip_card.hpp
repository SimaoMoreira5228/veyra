#ifndef VEYRA_WIDGETS_OVERLAY_TOOLTIP_CARD_HPP
#define VEYRA_WIDGETS_OVERLAY_TOOLTIP_CARD_HPP

#include "veyra/layout/core/geometry.hpp"
#include "veyra/render/cpu/software_renderer.hpp"
#include "veyra/style/theme.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

namespace veyra {

struct TooltipAppearance {
    Srgba8 background;
    Srgba8 border;
    Srgba8 text;
};

TooltipAppearance tooltip_appearance(const Theme& theme) noexcept;

struct TooltipCardGeometry {
    LayoutRect rect{};
};

enum class TooltipCardEmitResult : std::uint8_t {
    complete,
    invalid_input,
    insufficient_output,
};

struct TooltipCardOutput {
    std::size_t placed{};
    std::size_t required{};
    TooltipCardEmitResult result{TooltipCardEmitResult::complete};
};

TooltipCardOutput emit_tooltip_card(
    const Theme& theme,
    const TooltipCardGeometry& geometry,
    std::span<DisplayCommand> commands_out
) noexcept;

}

#endif
