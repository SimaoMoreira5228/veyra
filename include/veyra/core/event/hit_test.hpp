#ifndef VEYRA_CORE_HIT_TEST_HPP
#define VEYRA_CORE_HIT_TEST_HPP

#include "veyra/core/tree/node_tree.hpp"
#include "veyra/foundation/geometry.hpp"

#include <cstdint>
#include <span>

namespace veyra {

enum class HitTestResult : std::uint8_t {
    hit,
    no_hit,
    invalid_root,
    invalid_point,
    insufficient_bounds,
    invalid_bounds,
};

HitTestResult hit_test(
    const NodeTree& tree,
    NodeId root,
    LayoutPoint point,
    std::span<const LayoutRect> bounds_by_node_index,
    NodeId& target
) noexcept;

}

namespace veyra::core {
using ::veyra::HitTestResult;
using ::veyra::hit_test;
}

namespace veyra::core::event {
using ::veyra::HitTestResult;
using ::veyra::hit_test;
}

#endif
