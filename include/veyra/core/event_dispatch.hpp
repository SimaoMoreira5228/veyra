#ifndef VEYRA_CORE_EVENT_DISPATCH_HPP
#define VEYRA_CORE_EVENT_DISPATCH_HPP

#include "veyra/core/node_tree.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

namespace veyra {

enum class EventPhase : std::uint8_t {
    capture,
    target,
    bubble,
};

enum class EventDispatchResult : std::uint8_t {
    complete,
    stopped,
    invalid_target,
    insufficient_path,
    target_destroyed,
};

template <typename Event, typename Handler>
EventDispatchResult dispatch_event(
    const NodeTree& tree,
    NodeId target,
    const Event& event,
    std::span<NodeId> path,
    std::size_t& path_length,
    Handler&& handler
) {
    path_length = 0;
    if (!tree.contains(target)) {
        return EventDispatchResult::invalid_target;
    }
    if (!tree.path_to_root(target, path, path_length)) {
        return EventDispatchResult::insufficient_path;
    }

    for (std::size_t index = path_length; index > 1; --index) {
        if (!handler(path[index - 1], EventPhase::capture, event)) {
            return EventDispatchResult::stopped;
        }
        if (!tree.contains(target)) {
            return EventDispatchResult::target_destroyed;
        }
    }

    if (!handler(target, EventPhase::target, event)) {
        return EventDispatchResult::stopped;
    }
    if (!tree.contains(target)) {
        return EventDispatchResult::target_destroyed;
    }

    for (std::size_t index = 1; index < path_length; ++index) {
        if (!handler(path[index], EventPhase::bubble, event)) {
            return EventDispatchResult::stopped;
        }
        if (!tree.contains(target)) {
            return EventDispatchResult::target_destroyed;
        }
    }
    return EventDispatchResult::complete;
}

}

#endif
