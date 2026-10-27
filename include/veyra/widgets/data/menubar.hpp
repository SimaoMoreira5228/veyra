#ifndef VEYRA_WIDGETS_DATA_MENUBAR_HPP
#define VEYRA_WIDGETS_DATA_MENUBAR_HPP

#include "veyra/layout/core/geometry.hpp"
#include "veyra/render/cpu/software_renderer.hpp"
#include "veyra/style/theme.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

namespace veyra {

struct MenubarLabelState {
    bool active{};
    bool disabled{};
};

struct MenubarAppearance {
    Srgba8 background;
    Srgba8 item_text;
    Srgba8 active_background;
    Srgba8 separator;
};

MenubarAppearance menubar_appearance(const Theme& theme, MenubarLabelState state) noexcept;

struct MenubarGeometry {
    LayoutRect strip_bounds{};
    std::span<const float> label_widths{};
};

enum class MenubarEmitResult : std::uint8_t {
    complete,
    invalid_input,
    insufficient_output,
};

struct MenubarOutput {
    std::size_t placed{};
    std::size_t required{};
    MenubarEmitResult result{MenubarEmitResult::complete};
};

std::size_t menubar_required_commands(std::span<const MenubarLabelState> labels) noexcept;

MenubarOutput emit_menubar(
    const Theme& theme,
    std::span<const MenubarLabelState> labels,
    const MenubarGeometry& geometry,
    std::span<DisplayCommand> commands_out
) noexcept;

}

#endif
