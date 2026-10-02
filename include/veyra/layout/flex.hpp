#ifndef VEYRA_LAYOUT_FLEX_HPP
#define VEYRA_LAYOUT_FLEX_HPP

#include "veyra/layout/geometry.hpp"

#include <cstdint>
#include <limits>
#include <span>

namespace veyra {

enum class FlexDirection : std::uint8_t {
    row,
    column,
};

enum class FlexAlign : std::uint8_t {
    start,
    center,
    end,
    stretch,
};

enum class FlexJustify : std::uint8_t {
    start,
    center,
    end,
    space_between,
};

enum class FlexLayoutResult : std::uint8_t {
    complete,
    overflow,
    invalid_input,
    insufficient_output,
};

struct FlexContainer {
    float width{};
    float height{};
    LayoutInsets padding;
    float gap{};
    FlexDirection direction{FlexDirection::row};
    FlexAlign align_items{FlexAlign::stretch};
    FlexJustify justify_content{FlexJustify::start};
};

struct FlexItem {
    float basis{};
    float min_main{};
    float max_main{std::numeric_limits<float>::max()};
    float grow{};
    float shrink{1.0F};
    float cross_size{};
    float min_cross{};
    float max_cross{std::numeric_limits<float>::max()};
};

FlexLayoutResult layout_flex(
    const FlexContainer& container,
    std::span<const FlexItem> items,
    std::span<LayoutRect> output
) noexcept;

}

#endif
