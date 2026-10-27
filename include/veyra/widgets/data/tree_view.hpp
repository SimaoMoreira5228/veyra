#ifndef VEYRA_WIDGETS_DATA_TREE_VIEW_HPP
#define VEYRA_WIDGETS_DATA_TREE_VIEW_HPP

#include "veyra/behaviors/selection/tree_cursor.hpp"
#include "veyra/layout/geometry.hpp"
#include "veyra/render/cpu/software_renderer.hpp"
#include "veyra/style/theme.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

namespace veyra {

struct TreeViewRowState {
    bool selected{};
    bool current{};
    bool placeholder{};
};

struct TreeViewRowAppearance {
    Srgba8 background;
    Srgba8 separator;
    Srgba8 current_outline;
    Srgba8 chevron;
    Srgba8 guide;
};

TreeViewRowAppearance tree_view_row_appearance(
    const Theme& theme,
    TreeViewRowState state,
    const TreeCursorRow& row
) noexcept;

struct TreeViewRowsGeometry {
    LayoutRect list_bounds{};
    float row_height{};
    float sticky_header_height{};
    float indent_width{};
};

enum class TreeViewEmitResult : std::uint8_t {
    complete,
    invalid_input,
    insufficient_output,
};

struct TreeViewRowsOutput {
    std::size_t placed{};
    std::size_t required{};
    TreeViewEmitResult result{TreeViewEmitResult::complete};
};

std::size_t tree_view_required_commands(
    std::span<const TreeCursorRow> rows,
    std::span<const TreeViewRowState> states,
    const TreeViewRowsGeometry& geometry
) noexcept;

TreeViewRowsOutput emit_tree_view_rows(
    const Theme& theme,
    std::span<const TreeCursorRow> rows,
    std::span<const TreeViewRowState> states,
    const TreeViewRowsGeometry& geometry,
    std::span<DisplayCommand> commands_out
) noexcept;

}

#endif
