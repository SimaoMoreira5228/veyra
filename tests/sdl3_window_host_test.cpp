#include "veyra/app/sdl3/window_host.hpp"
#include "veyra/core/tree/node_tree.hpp"

#include <SDL3/SDL.h>

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
        auto info = veyra::WindowInfo{};
        require(host.windows().info(owner, info), "owner info is available");
        require(info.width == 160.0F && info.height == 120.0F, "host reports the client size");
        require(info.scale_x > 0.0F && info.scale_y > 0.0F, "host reports the scale");
        require(
            host.router().find(owner)->logical_width() == info.width,
            "window and router agree on width"
        );
        require(
            host.router().find(owner)->scale_generation() == info.scale_generation,
            "window and router agree on scale generation"
        );
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
        require(host.router().mark_presented(owner), "owner is marked presented");
        require(
            host.set_state(veyra::kInvalidWindowId, veyra::WindowState::minimized) ==
                veyra::WindowSetResult::invalid_window,
            "unknown windows reject state"
        );

        auto native_event = SDL_Event{};
        auto state_event = veyra::WindowEvent{};
        auto state_window = veyra::WindowId{};
        while (host.poll_event(state_event, state_window)) {
        }

        const auto check_state = [&](std::uint32_t type,
                                     veyra::WindowState expected,
                                     std::string_view message) {
            native_event = {};
            native_event.type = type;
            native_event.window.windowID = static_cast<Uint32>(host.backend(owner)->window_id());
            require(SDL_PushEvent(&native_event), "state event queues");

            auto found = false;
            for (auto attempt = 0; attempt < 16; ++attempt) {
                if (!host.poll_event(state_event, state_window)) {
                    break;
                }
                if (state_window == owner &&
                    state_event.kind == veyra::WindowEventKind::state_change &&
                    state_event.data.state == expected) {
                    found = true;
                    break;
                }
            }
            require(found, message);
            require(
                host.windows().info(owner, info) && info.state == expected,
                "the window record follows the platform state"
            );
        };

        check_state(
            SDL_EVENT_WINDOW_MINIMIZED,
            veyra::WindowState::minimized,
            "minimize state reaches the window record"
        );
        check_state(
            SDL_EVENT_WINDOW_MAXIMIZED,
            veyra::WindowState::maximized,
            "maximize state reaches the window record"
        );
        check_state(
            SDL_EVENT_WINDOW_ENTER_FULLSCREEN,
            veyra::WindowState::fullscreen,
            "fullscreen state reaches the window record"
        );
        check_state(
            SDL_EVENT_WINDOW_RESTORED,
            veyra::WindowState::normal,
            "restore state reaches the window record"
        );

        auto tree = veyra::NodeTree{};
        tree.reserve(2);
        const auto root_node = tree.create();
        const auto focus_node = tree.create();
        require(tree.append_child(root_node, focus_node), "focus node attaches");
        require(tree.set_focusable(focus_node, true), "focus node is focusable");
        require(host.bind_semantics(owner, tree, root_node), "host binds a semantic scope");
        require(host.set_focused_node(owner, focus_node), "host focus is accepted");
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
