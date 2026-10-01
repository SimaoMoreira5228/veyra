#include "veyra/core/event_dispatch.hpp"

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
    std::uint32_t value;
};

void require(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

void check_capture_target_bubble_order() {
    veyra::NodeTree tree;
    tree.reserve(3);

    const auto root = tree.create();
    const auto parent = tree.create();
    const auto target = tree.create();
    require(tree.append_child(root, parent), "parent attaches");
    require(tree.append_child(parent, target), "target attaches");

    constexpr Event event{73};
    std::array<veyra::NodeId, 3> path{};
    std::array<Visit, 5> visits{};
    std::size_t path_length = 0;
    std::size_t visit_count = 0;

    const auto result = veyra::dispatch_event(
        tree,
        target,
        event,
        path,
        path_length,
        [&visits,
         &visit_count](veyra::NodeId node, veyra::EventPhase phase, const Event& received) {
            if (visit_count == visits.size()) {
                return false;
            }
            visits[visit_count++] = {node, phase, received.value};
            return true;
        }
    );

    require(result == veyra::EventDispatchResult::complete, "event reaches the end of its route");
    require(path_length == 3, "dispatch reports route length");
    require(visit_count == visits.size(), "dispatch visits each expected phase");
    require(visits[0].node == root, "capture begins at the root");
    require(visits[0].phase == veyra::EventPhase::capture, "root receives capture phase");
    require(visits[1].node == parent, "capture walks down the route");
    require(visits[1].phase == veyra::EventPhase::capture, "parent receives capture phase");
    require(visits[2].node == target, "target is visited after capture");
    require(visits[2].phase == veyra::EventPhase::target, "target receives target phase");
    require(visits[3].node == parent, "bubble walks back through the parent");
    require(visits[3].phase == veyra::EventPhase::bubble, "parent receives bubble phase");
    require(visits[4].node == root, "bubble ends at the root");
    require(visits[4].phase == veyra::EventPhase::bubble, "root receives bubble phase");
    require(visits[4].value == event.value, "event payload is passed unchanged");
}

void check_stopping_and_route_errors() {
    veyra::NodeTree tree;
    tree.reserve(3);

    const auto root = tree.create();
    const auto parent = tree.create();
    const auto target = tree.create();
    require(tree.append_child(root, parent), "parent attaches");
    require(tree.append_child(parent, target), "target attaches");

    constexpr Event event{91};
    std::array<veyra::NodeId, 3> path{};
    std::size_t path_length = 0;
    std::size_t visit_count = 0;

    const auto stopped = veyra::dispatch_event(
        tree,
        target,
        event,
        path,
        path_length,
        [&visit_count, parent](veyra::NodeId node, veyra::EventPhase, const Event&) {
            ++visit_count;
            return node != parent;
        }
    );
    require(stopped == veyra::EventDispatchResult::stopped, "handler can stop propagation");
    require(visit_count == 2, "stopped event does not visit later phases");

    std::array short_path{veyra::NodeId{90, 90}, veyra::NodeId{91, 91}};
    visit_count = 0;
    const auto insufficient = veyra::dispatch_event(
        tree,
        target,
        event,
        short_path,
        path_length,
        [&visit_count](veyra::NodeId, veyra::EventPhase, const Event&) {
            ++visit_count;
            return true;
        }
    );
    require(
        insufficient == veyra::EventDispatchResult::insufficient_path,
        "short path buffer is reported"
    );
    require(path_length == 3, "short path reports required capacity");
    require(short_path[0].index == 90, "short path does not partially write");
    require(visit_count == 0, "handlers do not run without a complete path");

    const auto invalid = veyra::dispatch_event(
        tree,
        {},
        event,
        path,
        path_length,
        [](veyra::NodeId, veyra::EventPhase, const Event&) {
            return true;
        }
    );
    require(invalid == veyra::EventDispatchResult::invalid_target, "invalid target is reported");
    require(path_length == 0, "invalid target reports an empty route");
}

void check_target_destruction_stops_dispatch() {
    veyra::NodeTree tree;
    tree.reserve(2);

    const auto root = tree.create();
    const auto target = tree.create();
    require(tree.append_child(root, target), "target attaches");

    constexpr Event event{12};
    std::array<veyra::NodeId, 2> path{};
    std::size_t path_length = 0;
    std::size_t visit_count = 0;

    const auto result = veyra::dispatch_event(
        tree,
        target,
        event,
        path,
        path_length,
        [&tree, target, &visit_count](veyra::NodeId, veyra::EventPhase, const Event&) {
            ++visit_count;
            require(tree.destroy_subtree(target), "target is destroyed during capture");
            return true;
        }
    );

    require(
        result == veyra::EventDispatchResult::target_destroyed,
        "destroyed target stops dispatch"
    );
    require(visit_count == 1, "destroyed target receives no later phases");
}

}

int main() {
    try {
        check_capture_target_bubble_order();
        check_stopping_and_route_errors();
        check_target_destruction_stops_dispatch();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
