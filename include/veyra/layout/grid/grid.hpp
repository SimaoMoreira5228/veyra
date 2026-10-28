#ifndef VEYRA_LAYOUT_GRID_HPP
#define VEYRA_LAYOUT_GRID_HPP

#include "veyra/foundation/geometry.hpp"
#include "veyra/layout/core/measure.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>

namespace veyra {

struct GridContainer {
    float width{};
    float height{};
    LayoutInsets padding;
    float gap{};
};

struct GridTrack {
    float minimum{};
    float maximum{std::numeric_limits<float>::max()};
    float fraction{};
};

struct GridItem {
    std::optional<std::size_t> row;
    std::optional<std::size_t> column;
    std::size_t row_span{1};
    std::size_t column_span{1};
};

struct GridItemPlacement {
    std::size_t row{};
    std::size_t column{};
};

struct GridTrackWorkspace {
    float min_content{};
    float preferred{};
    float size{};
    float position{};
};

struct GridLayoutWorkspace {
    std::span<GridTrackWorkspace> columns;
    std::span<GridTrackWorkspace> rows;
    std::span<GridItemPlacement> placements;
    std::span<std::uint64_t> occupancy;
};

enum class GridLayoutResult : std::uint8_t {
    complete,
    overflow,
    no_placement,
    invalid_input,
    insufficient_output,
    insufficient_measurements,
    insufficient_workspace,
};

GridLayoutResult layout_grid(
    const GridContainer& container,
    std::span<const GridTrack> columns,
    std::span<const GridTrack> rows,
    std::span<const GridItem> items,
    std::span<const IntrinsicSize> intrinsic_sizes,
    GridLayoutWorkspace workspace,
    std::span<LayoutRect> output
) noexcept;

}

namespace veyra::layout {
using ::veyra::GridContainer;
using ::veyra::GridItem;
using ::veyra::GridItemPlacement;
using ::veyra::GridLayoutResult;
using ::veyra::GridLayoutWorkspace;
using ::veyra::GridTrack;
using ::veyra::GridTrackWorkspace;
using ::veyra::layout_grid;
}

namespace veyra::layout::grid {
using ::veyra::GridContainer;
using ::veyra::GridItem;
using ::veyra::GridItemPlacement;
using ::veyra::GridLayoutResult;
using ::veyra::GridLayoutWorkspace;
using ::veyra::GridTrack;
using ::veyra::GridTrackWorkspace;
using ::veyra::layout_grid;
}

#endif
