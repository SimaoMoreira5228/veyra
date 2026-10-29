#include "veyra/app/sdl3/window_host.hpp"
#include "veyra/core/schedule/frame_scheduler.hpp"
#include "veyra/core/tree/node_tree.hpp"

#include <SDL3/SDL.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <thread>

namespace {

void require(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

void check_pipelined_present() {
    auto host = veyra::app::sdl3::Sdl3WindowHost{};
    auto wake_scheduler = veyra::FrameScheduler{};
    auto wake_count = std::atomic<int>{};
    struct Observer {
        veyra::FrameScheduler* scheduler;
        std::atomic<int>* count;
    };
    auto observer = Observer{&wake_scheduler, &wake_count};
    host.set_present_observer(
        [](void* user) noexcept {
            auto* state = static_cast<Observer*>(user);
            state->scheduler->request_immediate();
            state->count->fetch_add(1, std::memory_order_relaxed);
        },
        &observer
    );
    auto first = veyra::WindowId{};
    auto second = veyra::WindowId{};
    require(
        host.create("first", 160, 120, {}, first) == veyra::WindowSetResult::ok,
        "pipeline first window creates"
    );
    require(
        host.create("second", 120, 90, {}, second) == veyra::WindowSetResult::ok,
        "pipeline second window creates"
    );
    require(host.show(first) == veyra::WindowSetResult::ok, "pipeline first shows");
    require(host.show(second) == veyra::WindowSetResult::ok, "pipeline second shows");

    auto first_width = std::uint32_t{};
    auto first_height = std::uint32_t{};
    auto second_width = std::uint32_t{};
    auto second_height = std::uint32_t{};
    require(host.surface_size(first, first_width, first_height), "first size is available");
    require(host.surface_size(second, second_width, second_height), "second size is available");

    auto first_backing = std::array<veyra::SoftwarePixel, std::size_t{160} * 120>{};
    auto second_backing = std::array<veyra::SoftwarePixel, std::size_t{120} * 90>{};
    auto first_display = std::array<veyra::DisplayCommand, 1>{veyra::DisplayCommand{veyra::FillRect{
        .bounds = {.width = 160.0F, .height = 120.0F},
        .color = {.red = 10, .green = 20, .blue = 30},
    }}};
    auto second_display =
        std::array<veyra::DisplayCommand, 1>{veyra::DisplayCommand{veyra::FillRect{
            .bounds = {.width = 120.0F, .height = 90.0F},
            .color = {.red = 30, .green = 20, .blue = 10},
        }}};

    require(
        host.commit(first, first_display, first_width, first_height) == veyra::WindowSetResult::ok,
        "first frame commits"
    );
    require(
        host.commit(second, second_display, second_width, second_height) ==
            veyra::WindowSetResult::ok,
        "second frame commits"
    );
    require(
        host.present_async(first, first_backing) == veyra::WindowSetResult::ok,
        "first frame submits without waiting"
    );
    require(
        host.present_async(second, second_backing) == veyra::WindowSetResult::ok,
        "second frame submits before the first completes"
    );
    require(host.wait_presented(), "both submitted frames complete");
    require(wake_count.load() == 2, "both completions wake the loop");
    require(wake_scheduler.immediate_pending(), "completion requests an immediate frame");

    auto first_result = veyra::WindowSetResult::presentation_failed;
    auto second_result = veyra::WindowSetResult::presentation_failed;
    require(host.take_present_result(first, first_result), "first result is available");
    require(host.take_present_result(second, second_result), "second result is available");
    require(first_result == veyra::WindowSetResult::ok, "first frame presents");
    require(second_result == veyra::WindowSetResult::ok, "second frame presents");
    require(host.backend(first)->visible(), "first frame shows its window");
    require(host.backend(second)->visible(), "second frame shows its window");

    auto native_event = SDL_Event{};
    native_event.type = SDL_EVENT_RENDER_DEVICE_LOST;
    native_event.render.windowID = static_cast<Uint32>(host.backend(first)->window_id());
    require(SDL_PushEvent(&native_event), "device loss queues for the pipeline");

    auto event = veyra::WindowEvent{};
    auto window = veyra::WindowId{};
    auto lost = false;
    for (auto attempt = 0; attempt < 16; ++attempt) {
        if (!host.poll_event(event, window)) {
            break;
        }
        if (window == first && event.kind == veyra::WindowEventKind::device_lost) {
            lost = true;
            break;
        }
    }
    require(lost, "device loss reaches the pipeline window");
    require(host.take_device_lost(first), "device loss is reported");

    require(
        host.commit(first, first_display, first_width, first_height) == veyra::WindowSetResult::ok,
        "unrecovered frame commits"
    );
    require(
        host.present_async(first, first_backing) == veyra::WindowSetResult::ok,
        "unrecovered frame submits"
    );
    require(host.wait_presented(), "unrecovered frame completes");
    first_result = veyra::WindowSetResult::ok;
    require(host.take_present_result(first, first_result), "unrecovered result is available");
    require(
        first_result == veyra::WindowSetResult::presentation_failed,
        "an unrecovered device fails presentation"
    );

    require(
        host.recover_device(first) == veyra::WindowSetResult::ok,
        "the renderer is recreated after device loss"
    );

    require(
        host.commit(first, first_display, first_width, first_height) == veyra::WindowSetResult::ok,
        "recovered frame commits"
    );
    require(
        host.present_async(first, first_backing) == veyra::WindowSetResult::ok,
        "recovered frame submits"
    );
    require(host.wait_presented(), "recovered frame completes");
    first_result = veyra::WindowSetResult::presentation_failed;
    require(host.take_present_result(first, first_result), "recovered result is available");
    require(first_result == veyra::WindowSetResult::ok, "the recovered frame presents");
    require(!host.take_device_lost(first), "recovery clears the loss report");
}

}

int main() {
    try {
        auto host = veyra::app::sdl3::Sdl3WindowHost{};
        require(host.render_thread_active(), "the host starts a render worker");
        require(
            host.render_thread_id() != std::this_thread::get_id(),
            "the render worker runs on its own thread"
        );

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

        auto backing = std::array<veyra::SoftwarePixel, std::size_t{160} * 120>{};
        auto command = veyra::DisplayCommand{veyra::FillRect{
            .bounds = {.width = 160.0F, .height = 120.0F},
            .color = {.red = 20, .green = 40, .blue = 60},
        }};
        auto display = std::array<veyra::DisplayCommand, 1>{command};
        require(!host.backend(owner)->visible(), "new windows start hidden");
        require(
            host.commit(owner, display, 160, 120) == veyra::WindowSetResult::ok,
            "a frame commits"
        );
        require(host.show(owner) == veyra::WindowSetResult::ok, "owner shows");
        require(!host.backend(owner)->visible(), "the first frame is presented before showing");
        const auto presents_before = host.render_thread_presents();
        require(
            host.present(owner, backing) == veyra::WindowSetResult::ok,
            "the committed frame presents"
        );
        require(
            host.render_thread_presents() == presents_before + 1,
            "presentation runs on the render thread"
        );
        require(host.backend(owner)->visible(), "presenting the first frame shows the window");
        require(host.show(child) == veyra::WindowSetResult::ok, "child shows");
        require(
            host.present(owner, backing) == veyra::WindowSetResult::stale_frame,
            "a consumed commit is not presented twice"
        );

        auto too_many =
            std::array<veyra::DisplayCommand, veyra::FrameCommit::kCommandCapacity + 1>{};
        require(
            host.commit(owner, too_many, 160, 120) == veyra::WindowSetResult::capacity,
            "commit capacity is enforced"
        );
        require(
            host.commit(owner, display, 0, 120) == veyra::WindowSetResult::invalid_geometry,
            "zero-width frames are rejected"
        );
        require(
            host.commit(veyra::kInvalidWindowId, display, 160, 120) ==
                veyra::WindowSetResult::invalid_window,
            "unknown windows reject commits"
        );
        require(
            host.present(veyra::kInvalidWindowId, backing) ==
                veyra::WindowSetResult::invalid_window,
            "unknown windows reject presentation"
        );

        require(
            host.backend(owner)->resize(200, 150) == veyra::WindowResult::ready,
            "owner resizes"
        );
        require(
            host.commit(owner, display, 160, 120) == veyra::WindowSetResult::ok,
            "old-size frame commits"
        );
        auto larger_backing = std::array<veyra::SoftwarePixel, std::size_t{200} * 150>{};
        require(
            host.present(owner, larger_backing) == veyra::WindowSetResult::stale_frame,
            "a frame from the old size is not presented after resize"
        );
        require(
            host.commit(owner, display, 200, 150) == veyra::WindowSetResult::ok,
            "resized frame commits"
        );
        require(
            host.present(owner, larger_backing) == veyra::WindowSetResult::ok,
            "the resized frame presents"
        );

        auto short_backing = std::array<veyra::SoftwarePixel, 4>{};
        require(
            host.commit(owner, display, 200, 150) == veyra::WindowSetResult::ok,
            "frame recommits"
        );
        require(
            host.present(owner, short_backing) == veyra::WindowSetResult::presentation_failed,
            "a short backing buffer fails presentation"
        );
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

        native_event = {};
        native_event.type = SDL_EVENT_RENDER_DEVICE_LOST;
        native_event.render.windowID = static_cast<Uint32>(host.backend(owner)->window_id());
        require(SDL_PushEvent(&native_event), "device loss queues");
        auto lost = false;
        for (auto attempt = 0; attempt < 16; ++attempt) {
            if (!host.poll_event(state_event, state_window)) {
                break;
            }
            if (state_window == owner && state_event.kind == veyra::WindowEventKind::device_lost) {
                lost = true;
                break;
            }
        }
        require(lost, "device loss reaches the window");
        require(host.take_device_lost(owner), "device loss is reported");
        require(!host.take_device_lost(owner), "device loss is consumed once");

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

        require(host.capture_pointer(owner, 7, focus_node), "host captures the pointer");
        require(host.capture_pointer(owner, 8, focus_node), "host captures a second pointer");
        require(host.set_hover(owner, focus_node), "host sets the hover target");
        auto mouse_native = SDL_Event{};
        mouse_native.type = SDL_EVENT_WINDOW_MOUSE_LEAVE;
        mouse_native.window.windowID = static_cast<Uint32>(host.backend(owner)->window_id());
        require(SDL_PushEvent(&mouse_native), "mouse leave queues");

        auto pointer_event = veyra::WindowEvent{};
        auto pointer_window = veyra::WindowId{};
        auto leave_seen = false;
        for (auto attempt = 0; attempt < 16; ++attempt) {
            if (!host.poll_event(pointer_event, pointer_window)) {
                break;
            }
            if (pointer_window == owner &&
                pointer_event.kind == veyra::WindowEventKind::pointer_leave) {
                leave_seen = true;
                break;
            }
        }
        require(leave_seen, "mouse leave reaches the window");
        require(host.router().find(owner)->hovered_node().is_null(), "mouse leave clears hover");
        require(host.router().find(owner)->capture_count() == 2, "mouse leave keeps both captures");
        require(host.router().find(owner)->pointer_captured(7), "first capture survives leave");
        require(host.router().find(owner)->pointer_captured(8), "second capture survives leave");

        mouse_native = {};
        mouse_native.type = SDL_EVENT_WINDOW_FOCUS_LOST;
        mouse_native.window.windowID = static_cast<Uint32>(host.backend(owner)->window_id());
        require(SDL_PushEvent(&mouse_native), "focus loss queues");
        auto focus_seen = false;
        for (auto attempt = 0; attempt < 16; ++attempt) {
            if (!host.poll_event(pointer_event, pointer_window)) {
                break;
            }
            if (pointer_window == owner &&
                pointer_event.kind == veyra::WindowEventKind::focus_lost) {
                focus_seen = true;
                break;
            }
        }
        require(focus_seen, "focus loss reaches the window");
        require(host.router().find(owner)->capture_count() == 0, "focus loss drops every capture");
        require(host.router().find(owner)->hovered_node().is_null(), "focus loss drops hover");

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
        check_pipelined_present();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }

    return 0;
}
