#ifndef VEYRA_CORE_EVENT_DISPATCH_HPP
#define VEYRA_CORE_EVENT_DISPATCH_HPP

#include "veyra/core/tree/node_tree.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

namespace veyra {

enum class EventPhase : std::uint8_t {
    capture,
    target,
    bubble,
};

enum class RouteStrategy : std::uint8_t {
    direct,
    tunnel,
    bubble,
    tunnel_bubble,
};

enum class RouteDecision : std::uint8_t {
    continue_route,
    handled,
};

enum class RouteResult : std::uint8_t {
    complete,
    handled,
    invalid_target,
    insufficient_path,
    target_destroyed,
};

struct RouteState {
    NodeId source{};
    NodeId original_source{};
    EventPhase phase{EventPhase::target};
    std::size_t depth{};
    bool class_handler{};

    constexpr bool on_tunnel() const noexcept {
        return phase == EventPhase::capture;
    }

    constexpr bool on_target() const noexcept {
        return phase == EventPhase::target;
    }

    constexpr bool on_bubble() const noexcept {
        return phase == EventPhase::bubble;
    }
};

// NOTE: original_source is the route target (the hit node); source is the
// logical initiator and defaults to the same node when null. A class handler
// runs before the instance handler for every node on the route and sees
// class_handler set in its route state.
template <typename Event, typename ClassHandler, typename InstanceHandler>
RouteResult dispatch_routed_event(
    const NodeTree& tree,
    NodeId target,
    NodeId source,
    const Event& event,
    RouteStrategy strategy,
    std::span<NodeId> path,
    std::size_t& path_length,
    ClassHandler&& class_handler,
    InstanceHandler&& instance_handler
) {
    path_length = 0;
    if (!tree.contains(target)) {
        return RouteResult::invalid_target;
    }
    if (!tree.path_to_root(target, path, path_length)) {
        return RouteResult::insufficient_path;
    }

    const auto logical_source = source.is_null() ? target : source;
    const auto invoke =
        [&](auto&& handler, NodeId node, EventPhase phase, std::size_t depth, bool is_class) {
            const RouteState state{
                .source = logical_source,
                .original_source = target,
                .phase = phase,
                .depth = depth,
                .class_handler = is_class,
            };

            return handler(node, event, state) == RouteDecision::handled;
        };

    enum class VisitResult : std::uint8_t {
        proceed,
        handled,
        destroyed,
    };

    const auto visit = [&](NodeId node, EventPhase phase, std::size_t depth) {
        if (invoke(class_handler, node, phase, depth, true)) {
            return VisitResult::handled;
        }
        if (!tree.contains(target)) {
            return VisitResult::destroyed;
        }
        if (invoke(instance_handler, node, phase, depth, false)) {
            return VisitResult::handled;
        }
        if (!tree.contains(target)) {
            return VisitResult::destroyed;
        }

        return VisitResult::proceed;
    };

    const auto consume = [](VisitResult result) -> RouteResult {
        if (result == VisitResult::handled) {
            return RouteResult::handled;
        }
        if (result == VisitResult::destroyed) {
            return RouteResult::target_destroyed;
        }

        return RouteResult::complete;
    };

    if (strategy == RouteStrategy::direct) {
        return consume(visit(target, EventPhase::target, 0));
    }

    if (strategy == RouteStrategy::tunnel || strategy == RouteStrategy::tunnel_bubble) {
        for (auto index = path_length; index > 1; --index) {
            const auto result = visit(path[index - 1], EventPhase::capture, index - 1);
            if (result != VisitResult::proceed) {
                return consume(result);
            }
        }

        const auto result = visit(target, EventPhase::target, 0);
        if (result != VisitResult::proceed) {
            return consume(result);
        }
    }

    if (strategy == RouteStrategy::bubble || strategy == RouteStrategy::tunnel_bubble) {
        if (strategy == RouteStrategy::bubble) {
            const auto result = visit(target, EventPhase::target, 0);
            if (result != VisitResult::proceed) {
                return consume(result);
            }
        }

        for (std::size_t index = 1; index < path_length; ++index) {
            const auto result = visit(path[index], EventPhase::bubble, index);
            if (result != VisitResult::proceed) {
                return consume(result);
            }
        }
    }

    return RouteResult::complete;
}

}

namespace veyra::core {
using ::veyra::dispatch_routed_event;
using ::veyra::EventPhase;
using ::veyra::RouteDecision;
using ::veyra::RouteResult;
using ::veyra::RouteState;
using ::veyra::RouteStrategy;
}

namespace veyra::core::event {
using ::veyra::dispatch_routed_event;
using ::veyra::EventPhase;
using ::veyra::RouteDecision;
using ::veyra::RouteResult;
using ::veyra::RouteState;
using ::veyra::RouteStrategy;
}

#endif
