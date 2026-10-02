#ifndef VEYRA_LAYOUT_GRID_PLACEMENT_HPP
#define VEYRA_LAYOUT_GRID_PLACEMENT_HPP

#include "veyra/layout/grid.hpp"

namespace veyra::grid_detail {

bool has_automatic_placement(std::span<const GridItem> items) noexcept;

GridLayoutResult resolve_placements(
    std::span<const GridItem> items,
    std::size_t row_count,
    std::size_t column_count,
    std::span<GridItemPlacement> placements,
    std::span<std::uint64_t> occupancy
) noexcept;

GridItemPlacement item_placement(
    const GridItem& item,
    std::size_t index,
    std::span<const GridItemPlacement> placements
) noexcept;

}

#endif
