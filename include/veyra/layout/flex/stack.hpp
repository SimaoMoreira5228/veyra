#ifndef VEYRA_LAYOUT_STACK_HPP
#define VEYRA_LAYOUT_STACK_HPP

#include "veyra/foundation/geometry.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>

namespace veyra {

enum class StackAlignment : std::uint8_t {
    start,
    center,
    end,
    stretch,
};

enum class StackLayoutResult : std::uint8_t {
    complete,
    overflow,
    invalid_input,
    insufficient_output,
};

struct StackContainer {
    float width{};
    float height{};
    LayoutInsets padding;
    StackAlignment align_x{StackAlignment::stretch};
    StackAlignment align_y{StackAlignment::stretch};
};

struct StackItem {
    float width{};
    float height{};
    float min_width{};
    float max_width{std::numeric_limits<float>::max()};
    float min_height{};
    float max_height{std::numeric_limits<float>::max()};
};

StackLayoutResult layout_stack(
    const StackContainer& container,
    std::span<const StackItem> items,
    std::span<LayoutRect> output
) noexcept;

}

namespace veyra::layout {
using ::veyra::StackAlignment;
using ::veyra::StackContainer;
using ::veyra::StackItem;
using ::veyra::StackLayoutResult;
using ::veyra::layout_stack;
}

namespace veyra::layout::flex {
using ::veyra::StackAlignment;
using ::veyra::StackContainer;
using ::veyra::StackItem;
using ::veyra::StackLayoutResult;
using ::veyra::layout_stack;
}

#endif
