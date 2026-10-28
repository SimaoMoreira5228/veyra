#ifndef VEYRA_WIDGETS_DATA_DATA_GRID_HPP
#define VEYRA_WIDGETS_DATA_DATA_GRID_HPP

#include "veyra/behaviors/data/datagrid.hpp"
#include "veyra/foundation/geometry.hpp"
#include "veyra/render/cpu/software_renderer.hpp"
#include "veyra/style/theme/theme.hpp"

#include <cstddef>
#include <span>

namespace veyra {

struct DataGridHeaderState {
    bool sorted_asc{};
    bool sorted_desc{};
    bool pinned{};
};

struct DataGridHeaderAppearance {
    Srgba8 background;
    Srgba8 text;
    Srgba8 sort_arrow;
    Srgba8 separator;
    Srgba8 pinned_separator;
};

DataGridHeaderAppearance
data_grid_header_appearance(const Theme& theme, DataGridHeaderState state) noexcept;

struct DataGridCellState {
    bool selected{};
    bool current{};
    bool in_range{};
    bool edit_pending{};
};

struct DataGridCellAppearance {
    Srgba8 background;
    Srgba8 range_outline;
};

DataGridCellAppearance
data_grid_cell_appearance(const Theme& theme, DataGridCellState state) noexcept;

struct DataGridEmit {
    std::size_t count{};
    bool overflow{};
};

constexpr std::size_t data_grid_header_capacity(std::size_t columns) noexcept {
    return 1 + 3 * columns;
}

constexpr std::size_t data_grid_range_capacity() noexcept {
    return 2;
}

struct DataGridHeaderGeometry {
    float x{};
    float y{};
    float row_height{};
};

DataGridEmit emit_data_grid_header(
    const Theme& theme,
    std::span<const DataGridColumn> columns,
    std::span<const SortSpec> sorts,
    DataGridHeaderGeometry geometry,
    std::span<DisplayCommand> commands_out
) noexcept;

struct DataGridRangeGeometry {
    LayoutRect bounds{};
    LayoutRect current{};
    bool has_range{};
    bool has_current{};
    bool edit_pending{};
};

DataGridEmit emit_data_grid_range(
    const Theme& theme,
    const DataGridRangeGeometry& geometry,
    std::span<DisplayCommand> commands_out
) noexcept;

DataGridEmit emit_data_grid_cell_background(
    const Theme& theme,
    const DataGridCellAppearance& appearance,
    LayoutRect bounds,
    std::span<DisplayCommand> commands_out
) noexcept;

}

namespace veyra::widgets {
using ::veyra::DataGridCellAppearance;
using ::veyra::DataGridCellState;
using ::veyra::DataGridEmit;
using ::veyra::DataGridHeaderAppearance;
using ::veyra::DataGridHeaderGeometry;
using ::veyra::DataGridHeaderState;
using ::veyra::DataGridRangeGeometry;
using ::veyra::data_grid_cell_appearance;
using ::veyra::data_grid_header_appearance;
using ::veyra::data_grid_header_capacity;
using ::veyra::data_grid_range_capacity;
using ::veyra::emit_data_grid_cell_background;
using ::veyra::emit_data_grid_header;
using ::veyra::emit_data_grid_range;
}

namespace veyra::widgets::data {
using ::veyra::DataGridCellAppearance;
using ::veyra::DataGridCellState;
using ::veyra::DataGridEmit;
using ::veyra::DataGridHeaderAppearance;
using ::veyra::DataGridHeaderGeometry;
using ::veyra::DataGridHeaderState;
using ::veyra::DataGridRangeGeometry;
using ::veyra::data_grid_cell_appearance;
using ::veyra::data_grid_header_appearance;
using ::veyra::data_grid_header_capacity;
using ::veyra::data_grid_range_capacity;
using ::veyra::emit_data_grid_cell_background;
using ::veyra::emit_data_grid_header;
using ::veyra::emit_data_grid_range;
}

#endif
