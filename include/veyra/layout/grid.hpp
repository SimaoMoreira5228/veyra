#ifndef VEYRA_LAYOUT_GRID_HPP
#define VEYRA_LAYOUT_GRID_HPP

#include "veyra/layout/geometry.hpp"
#include "veyra/layout/measure.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>
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
};

struct GridItem {
    std::size_t row{};
    std::size_t column{};
    std::size_t row_span{1};
    std::size_t column_span{1};
};

struct GridTrackWorkspace {
    float min_content{};
    float preferred{};
    float size{};
    float position{};
};

enum class GridLayoutResult : std::uint8_t {
    complete,
    overflow,
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
    std::span<GridTrackWorkspace> column_workspace,
    std::span<GridTrackWorkspace> row_workspace,
    std::span<LayoutRect> output
) noexcept;

}

#endif
