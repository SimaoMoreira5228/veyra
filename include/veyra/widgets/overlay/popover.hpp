#ifndef VEYRA_WIDGETS_OVERLAY_POPOVER_HPP
#define VEYRA_WIDGETS_OVERLAY_POPOVER_HPP

#include "veyra/layout/geometry.hpp"
#include "veyra/render/software_renderer.hpp"
#include "veyra/style/theme.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

namespace veyra {

struct PopoverAppearance {
    Srgba8 panel;
    Srgba8 border;
    Srgba8 separator;
    Srgba8 scrim;
};

PopoverAppearance popover_appearance(const Theme& theme, bool modal) noexcept;

struct PopoverGeometry {
    LayoutRect panel{};
    LayoutRect viewport{};
    bool modal{};
};

enum class PopoverEmitResult : std::uint8_t {
    complete,
    invalid_input,
    insufficient_output,
};

struct PopoverOutput {
    std::size_t placed{};
    std::size_t required{};
    PopoverEmitResult result{PopoverEmitResult::complete};
};

PopoverOutput emit_popover(
    const Theme& theme,
    const PopoverGeometry& geometry,
    std::span<DisplayCommand> commands_out
) noexcept;

}

#endif
