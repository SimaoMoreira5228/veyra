#include "veyra/core/event/event_dispatch.hpp"

#include <array>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

struct Event {
    std::uint32_t value;
};

struct Visit {
    veyra::NodeId node;
    veyra::EventPhase phase;
    bool class_handler;
    std::uint32_t value;
};

veyra::RouteDecision
continue_route(veyra::NodeId, const Event&, const veyra::RouteState&) noexcept {
    return veyra::RouteDecision::continue_route;
}

void require(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

struct Tree {
    veyra::NodeTree nodes;
    veyra::NodeId root;
    veyra::NodeId parent;
    veyra::NodeId target;

    Tree() {
        nodes.reserve(3);
        root = nodes.create();
        parent = nodes.create();
        target = nodes.create();
        nodes.append_child(root, parent);
        nodes.append_child(parent, target);
    }
};

void check_full_route_order() {
    auto tree = Tree{};
    constexpr Event event{73};
    auto path = std::array<veyra::NodeId, 3>{};
    auto visits = std::array<Visit, 10>{};
    auto path_length = std::size_t{};
    auto visit_count = std::size_t{};

    const auto result = veyra::dispatch_routed_event(
        tree.nodes,
        tree.target,
        {},
        event,
        veyra::RouteStrategy::tunnel_bubble,
        path,
        path_length,
        [&visits,
         &visit_count](veyra::NodeId node, const Event& received, const veyra::RouteState& state) {
            if (visit_count < visits.size()) {
                visits[visit_count] = {node, state.phase, true, received.value};
            }
            ++visit_count;
            return veyra::RouteDecision::continue_route;
        },
        [&visits,
         &visit_count](veyra::NodeId node, const Event& received, const veyra::RouteState& state) {
            if (visit_count < visits.size()) {
                visits[visit_count] = {node, state.phase, false, received.value};
            }
            ++visit_count;
            return veyra::RouteDecision::continue_route;
        }
    );

    require(result == veyra::RouteResult::complete, "full route completes");
    require(path_length == 3, "the route reports its path");
    require(visit_count == visits.size(), "class and instance handlers both visit every node");
    require(visits[0].node == tree.root && visits[0].class_handler, "class handler runs first");
    require(visits[1].node == tree.root && !visits[1].class_handler, "instance handler follows");
    require(visits[0].phase == veyra::EventPhase::capture, "capture starts at the root");
    require(visits[2].node == tree.parent, "capture walks down");
    require(visits[4].node == tree.target, "target is reached after capture");
    require(visits[4].phase == veyra::EventPhase::target, "target phase is used at the target");
    require(visits[6].node == tree.parent, "bubble walks back through the parent");
    require(visits[6].phase == veyra::EventPhase::bubble, "parent receives bubble phase");
    require(visits[8].node == tree.root, "bubble ends at the root");
    require(visits[4].value == event.value, "the payload is passed unchanged");
}

void check_direct_and_single_leg_strategies() {
    auto tree = Tree{};
    constexpr Event event{4};
    auto path = std::array<veyra::NodeId, 3>{};
    auto path_length = std::size_t{};
    auto visits = std::array<Visit, 8>{};
    auto visit_count = std::size_t{};

    const auto record =
        [&visits,
         &visit_count](veyra::NodeId node, const Event& received, const veyra::RouteState& state) {
            if (visit_count < visits.size()) {
                visits[visit_count] = {node, state.phase, state.class_handler, received.value};
            }
            ++visit_count;
            return veyra::RouteDecision::continue_route;
        };

    require(
        veyra::dispatch_routed_event(
            tree.nodes,
            tree.target,
            {},
            event,
            veyra::RouteStrategy::direct,
            path,
            path_length,
            continue_route,
            record
        ) == veyra::RouteResult::complete,
        "direct route completes"
    );
    require(visit_count == 1 && visits[0].node == tree.target, "direct reaches only the target");
    require(visits[0].phase == veyra::EventPhase::target, "direct uses the target phase");

    visit_count = 0;
    require(
        veyra::dispatch_routed_event(
            tree.nodes,
            tree.target,
            {},
            event,
            veyra::RouteStrategy::tunnel,
            path,
            path_length,
            continue_route,
            record
        ) == veyra::RouteResult::complete,
        "tunnel route completes"
    );
    require(visit_count == 3, "tunnel visits root, parent, and target");
    require(
        visits[0].node == tree.root && visits[0].phase == veyra::EventPhase::capture,
        "tunnel starts at the root"
    );
    require(
        visits[2].node == tree.target && visits[2].phase == veyra::EventPhase::target,
        "tunnel ends at the target"
    );

    visit_count = 0;
    require(
        veyra::dispatch_routed_event(
            tree.nodes,
            tree.target,
            {},
            event,
            veyra::RouteStrategy::bubble,
            path,
            path_length,
            continue_route,
            record
        ) == veyra::RouteResult::complete,
        "bubble route completes"
    );
    require(visit_count == 3, "bubble visits target, parent, and root");
    require(
        visits[0].node == tree.target && visits[0].phase == veyra::EventPhase::target,
        "bubble starts at the target"
    );
    require(
        visits[2].node == tree.root && visits[2].phase == veyra::EventPhase::bubble,
        "bubble ends at the root"
    );
}

void check_route_state_and_source() {
    auto tree = Tree{};
    constexpr Event event{9};
    auto path = std::array<veyra::NodeId, 3>{};
    auto path_length = std::size_t{};

    const auto result = veyra::dispatch_routed_event(
        tree.nodes,
        tree.target,
        tree.root,
        event,
        veyra::RouteStrategy::bubble,
        path,
        path_length,
        continue_route,
        [&tree](veyra::NodeId node, const Event&, const veyra::RouteState& state) {
            require(state.source == tree.root, "the logical source is carried");
            require(state.original_source == tree.target, "the hit target is the original source");
            require(!state.class_handler, "instance handlers see the class flag clear");
            if (node == tree.target) {
                require(state.on_target() && state.depth == 0, "target state is queryable");
            } else if (node == tree.parent) {
                require(state.on_bubble() && state.depth == 1, "bubble state is queryable");
            } else {
                require(state.on_bubble() && state.depth == 2, "root depth is queryable");
            }
            return veyra::RouteDecision::continue_route;
        }
    );

    require(result == veyra::RouteResult::complete, "source-aware route completes");

    dispatch_routed_event(
        tree.nodes,
        tree.target,
        {},
        event,
        veyra::RouteStrategy::direct,
        path,
        path_length,
        continue_route,
        [&tree](veyra::NodeId, const Event&, const veyra::RouteState& state) {
            require(state.source == tree.target, "a null source defaults to the target");
            return veyra::RouteDecision::continue_route;
        }
    );
}

void check_handled_stops_both_handlers() {
    auto tree = Tree{};
    constexpr Event event{12};
    auto path = std::array<veyra::NodeId, 3>{};
    auto path_length = std::size_t{};
    auto instance_count = std::size_t{};

    const auto result = veyra::dispatch_routed_event(
        tree.nodes,
        tree.target,
        {},
        event,
        veyra::RouteStrategy::tunnel_bubble,
        path,
        path_length,
        [&tree](veyra::NodeId node, const Event&, const veyra::RouteState&) {
            return node == tree.parent ? veyra::RouteDecision::handled
                                       : veyra::RouteDecision::continue_route;
        },
        [&instance_count](veyra::NodeId, const Event&, const veyra::RouteState&) {
            ++instance_count;
            return veyra::RouteDecision::continue_route;
        }
    );

    require(result == veyra::RouteResult::handled, "a class handler can handle the route");
    require(instance_count == 1, "the handled node skips its instance handler and later phases");
}

void check_instance_handled_stops_later_phases() {
    auto tree = Tree{};
    constexpr Event event{13};
    auto path = std::array<veyra::NodeId, 3>{};
    auto path_length = std::size_t{};
    auto visit_count = std::size_t{};

    const auto result = veyra::dispatch_routed_event(
        tree.nodes,
        tree.target,
        {},
        event,
        veyra::RouteStrategy::tunnel_bubble,
        path,
        path_length,
        continue_route,
        [&tree, &visit_count](veyra::NodeId node, const Event&, const veyra::RouteState&) {
            ++visit_count;
            return node == tree.parent ? veyra::RouteDecision::handled
                                       : veyra::RouteDecision::continue_route;
        }
    );

    require(result == veyra::RouteResult::handled, "an instance handler can handle the route");
    require(visit_count == 2, "the target and later phases never run");
}

void check_route_errors() {
    auto tree = Tree{};
    constexpr Event event{21};
    auto path = std::array<veyra::NodeId, 3>{};
    auto path_length = std::size_t{};
    auto visit_count = std::size_t{};
    const auto handler = [&visit_count](veyra::NodeId, const Event&, const veyra::RouteState&) {
        ++visit_count;
        return veyra::RouteDecision::continue_route;
    };

    auto short_path = std::array{veyra::NodeId{90, 90}, veyra::NodeId{91, 91}};
    require(
        veyra::dispatch_routed_event(
            tree.nodes,
            tree.target,
            {},
            event,
            veyra::RouteStrategy::direct,
            short_path,
            path_length,
            continue_route,
            handler
        ) == veyra::RouteResult::insufficient_path,
        "a short path is reported"
    );
    require(path_length == 3, "the short path reports required capacity");
    require(short_path[0].index == 90, "the short path is not partially written");
    require(visit_count == 0, "handlers do not run without a path");

    require(
        veyra::dispatch_routed_event(
            tree.nodes,
            {},
            {},
            event,
            veyra::RouteStrategy::direct,
            path,
            path_length,
            continue_route,
            handler
        ) == veyra::RouteResult::invalid_target,
        "an invalid target is reported"
    );
    require(path_length == 0, "an invalid target reports an empty path");
}

void check_target_destruction_stops_dispatch() {
    auto tree = Tree{};
    constexpr Event event{31};
    auto path = std::array<veyra::NodeId, 3>{};
    auto path_length = std::size_t{};
    auto visit_count = std::size_t{};

    const auto result = veyra::dispatch_routed_event(
        tree.nodes,
        tree.target,
        {},
        event,
        veyra::RouteStrategy::tunnel_bubble,
        path,
        path_length,
        continue_route,
        [&tree, &visit_count](veyra::NodeId, const Event&, const veyra::RouteState&) {
            ++visit_count;
            require(tree.nodes.destroy_subtree(tree.target), "target is destroyed during capture");
            return veyra::RouteDecision::continue_route;
        }
    );

    require(result == veyra::RouteResult::target_destroyed, "destroyed target stops dispatch");
    require(visit_count == 1, "no later phase runs after destruction");
}

}

int main() {
    try {
        check_full_route_order();
        check_direct_and_single_leg_strategies();
        check_route_state_and_source();
        check_handled_stops_both_handlers();
        check_instance_handled_stops_later_phases();
        check_route_errors();
        check_target_destruction_stops_dispatch();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';

        return 1;
    }

    std::cout << "routed event checks passed\n";

    return 0;
}
