#include "veyra/app/sdl3/window_host.hpp"

#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string_view>

namespace {

void require(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

}

int main() {
    try {
        auto host = veyra::app::sdl3::Sdl3WindowHost{};

        auto owner = veyra::WindowId{};
        auto child = veyra::WindowId{};
        auto dialog = veyra::WindowId{};

        require(
            host.create("owner", 160, 120, {}, owner) == veyra::WindowSetResult::ok,
            "owner window creates"
        );
        require(host.backend(owner) != nullptr, "owner has a backend");
        require(
            host.create("child", 120, 90, {.owner = owner}, child) == veyra::WindowSetResult::ok,
            "child window creates"
        );
        require(
            host.show(child) == veyra::WindowSetResult::owner_not_visible,
            "child waits for its owner"
        );
        require(host.show(owner) == veyra::WindowSetResult::ok, "owner shows");
        require(host.show(child) == veyra::WindowSetResult::ok, "child shows");

        const auto focus_node = veyra::NodeId{.index = 7, .generation = 2};
        const auto root_node = veyra::NodeId{.index = 1, .generation = 1};
        require(host.set_focused_node(owner, focus_node), "host focus is accepted");
        require(host.set_accessibility_root(owner, root_node), "accessibility root is accepted");
        require(host.windows().focused_node(owner) == focus_node, "window set keeps focus");
        require(
            host.router().find(owner) != nullptr &&
                host.router().find(owner)->focused_node() == focus_node,
            "router keeps focus"
        );

        auto focus_event = veyra::WindowEvent{};
        focus_event.kind = veyra::WindowEventKind::focus_gained;
        focus_event.window = owner;
        require(
            host.router().route(focus_event) == veyra::WindowRouteResult::routed,
            "focus event routes"
        );
        require(host.router().find(owner)->focused(), "router tracks activation");

        require(
            host.create(
                "dialog",
                100,
                80,
                {.owner = owner, .dialog = true, .modal = true},
                dialog
            ) == veyra::WindowSetResult::ok,
            "modal dialog creates"
        );
        require(host.show(dialog) == veyra::WindowSetResult::ok, "dialog shows");
        require(host.windows().blocks_interaction(owner), "dialog blocks its owner");
        require(
            host.close_with_result(dialog, true, 7) == veyra::WindowSetResult::ok,
            "dialog closes with a result"
        );
        require(host.backend(dialog) == nullptr, "closed dialog releases its backend");

        auto outcome = veyra::DialogOutcome{};
        require(
            host.windows().take_dialog_outcome(dialog, outcome) == veyra::WindowSetResult::ok,
            "dialog outcome is available"
        );
        require(outcome.accepted && outcome.value == 7, "dialog outcome carries the result");

        require(
            host.request_close(owner, veyra::CloseReason::user) == veyra::WindowSetResult::ok,
            "owner closes"
        );
        require(
            host.backend(owner) == nullptr && host.backend(child) == nullptr,
            "tree backends are released"
        );
        require(host.windows().should_shutdown(), "last window requests shutdown");

        auto event = veyra::WindowEvent{};
        auto logical = veyra::WindowId{};
        require(!host.poll_event(event, logical), "closed host has no events");
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }

    return 0;
}
