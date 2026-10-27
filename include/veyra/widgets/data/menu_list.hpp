#ifndef VEYRA_WIDGETS_DATA_MENU_LIST_HPP
#define VEYRA_WIDGETS_DATA_MENU_LIST_HPP

#include "veyra/layout/geometry.hpp"
#include "veyra/render/cpu/software_renderer.hpp"
#include "veyra/style/theme.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

namespace veyra {

struct MenuListRowState {
    bool active{};
    bool checked{};
    bool disabled{};
    bool is_separator{};
    bool has_submenu{};
};

struct MenuRowAppearance {
    Srgba8 background;
    Srgba8 text;
    Srgba8 checkmark;
    Srgba8 separator;
    Srgba8 submenu_arrow;
    Srgba8 disabled_text;
};

MenuRowAppearance menu_row_appearance(const Theme& theme, MenuListRowState state) noexcept;

struct MenuListGeometry {
    LayoutRect list_bounds{};
    float row_height{};
    float shortcut_column_x{};
};

enum class MenuListEmitResult : std::uint8_t {
    complete,
    invalid_input,
    insufficient_output,
};

struct MenuListOutput {
    std::size_t placed{};
    std::size_t required{};
    MenuListEmitResult result{MenuListEmitResult::complete};
};

std::size_t menu_list_required_commands(std::span<const MenuListRowState> rows) noexcept;

MenuListOutput emit_menu_list(
    const Theme& theme,
    std::span<const MenuListRowState> rows,
    const MenuListGeometry& geometry,
    std::span<DisplayCommand> commands_out
) noexcept;

}

#endif
