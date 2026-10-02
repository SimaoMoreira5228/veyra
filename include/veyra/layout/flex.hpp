#ifndef VEYRA_LAYOUT_FLEX_HPP
#define VEYRA_LAYOUT_FLEX_HPP

#include "veyra/layout/geometry.hpp"
#include "veyra/layout/measure.hpp"

#include <cstdint>
#include <limits>
#include <optional>
#include <span>

namespace veyra {

enum class FlexDirection : std::uint8_t {
    row,
    column,
};

enum class FlexWrap : std::uint8_t {
    no_wrap,
    wrap,
};

enum class FlexAlignContent : std::uint8_t {
    start,
    center,
    end,
    space_between,
    stretch,
};

enum class FlexAlign : std::uint8_t {
    start,
    center,
    end,
    stretch,
    baseline,
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
    insufficient_measurements,
    insufficient_workspace,
};

struct FlexContainer {
    float width{};
    float height{};
    LayoutInsets padding;
    float gap{};
    FlexDirection direction{FlexDirection::row};
    FlexWrap wrap{FlexWrap::no_wrap};
    FlexAlignContent align_content{FlexAlignContent::start};
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
    std::optional<float> baseline;
};

struct FlexItemStyle {
    std::optional<float> basis;
    float min_main{};
    std::optional<float> max_main;
    float grow{};
    float shrink{1.0F};
    float min_cross{};
    std::optional<float> max_cross;
};

FlexLayoutResult layout_flex(
    const FlexContainer& container,
    std::span<const FlexItem> items,
    std::span<LayoutRect> output
) noexcept;

bool flex_item_constraints(
    const FlexContainer& container,
    const FlexItemStyle& item,
    LayoutConstraints& constraints
) noexcept;

FlexLayoutResult layout_flex_measured(
    const FlexContainer& container,
    std::span<const FlexItemStyle> styles,
    std::span<const IntrinsicSize> intrinsic_sizes,
    std::span<FlexItem> resolved_items,
    std::span<LayoutRect> output
) noexcept;

}

#endif
