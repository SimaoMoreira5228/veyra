#include "veyra/app/sdl3/window_host.hpp"
#include "veyra/core/schedule/frame_scheduler.hpp"
#include "veyra/core/tree/node_tree.hpp"
#include "veyra/platform/headless/fake_services.hpp"

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

#if defined(VEYRA_TEST_WITH_CODECS)
void check_async_decode() {
    auto bmp = std::array<std::uint8_t, 58>{};
    bmp[0] = 'B';
    bmp[1] = 'M';
    bmp[2] = 58;
    bmp[10] = 54;
    bmp[14] = 40;
    bmp[18] = 1;
    bmp[22] = 1;
    bmp[26] = 1;
    bmp[28] = 24;
    bmp[34] = 4;
    bmp[56] = 255;

    auto info = veyra::ImageDecodeInfo{};
    require(
        veyra::probe_image(bmp, {}, info) == veyra::ImageDecodeResult::complete &&
            info.width == 1 && info.height == 1,
        "the async test bitmap probes as one pixel"
    );

    auto host = veyra::app::sdl3::Sdl3WindowHost{};
    auto window = veyra::WindowId{};
    require(
        host.create("async decode", 160, 120, {}, window) == veyra::WindowSetResult::ok,
        "async decode window creates"
    );

    auto output = std::array<std::uint8_t, 4>{};
    auto frames = std::array<veyra::ImageFrameTarget, 1>{
        veyra::ImageFrameTarget{.pixels = output},
    };
    auto delays = std::array<std::uint32_t, 1>{};

    require(
        host.begin_decode(window, 50, bmp, {}, frames, delays, &veyra::decode_image),
        "a real decode request starts"
    );
    require(
        host.destroy(window) == veyra::WindowSetResult::ok,
        "decode window destroys before completion"
    );

    auto completion = veyra::app::AsyncDecodeResult{};
    require(host.wait_decode(completion, 2000), "the background decode finishes");
    require(completion.request == 50, "the completion keeps its request id");
    require(completion.cancelled, "the closed window discards the completion");
    require(host.decode_discarded(50), "the tracker keeps the discard report");
    require(!host.take_decode(50), "a discarded completion cannot be taken");

    auto live = veyra::WindowId{};
    require(
        host.create("live decode", 160, 120, {}, live) == veyra::WindowSetResult::ok,
        "live decode window creates"
    );
    auto live_output = std::array<std::uint8_t, 4>{};
    auto live_frames = std::array<veyra::ImageFrameTarget, 1>{
        veyra::ImageFrameTarget{.pixels = live_output},
    };
    auto live_delays = std::array<std::uint32_t, 1>{};
    require(
        host.begin_decode(live, 51, bmp, {}, live_frames, live_delays, &veyra::decode_image),
        "a live decode request starts"
    );
    auto live_completion = veyra::app::AsyncDecodeResult{};
    require(host.wait_decode(live_completion, 2000), "the live decode finishes");
    require(!live_completion.cancelled, "a live window keeps its completion");
    require(
        live_completion.result == veyra::ImageDecodeResult::complete &&
            live_completion.decoded_frames == 1,
        "the live decode completes one frame"
    );
    require(host.take_decode(51), "a live completion can be taken");
}

#endif

void check_decode_completion_discard() {
    auto host = veyra::app::sdl3::Sdl3WindowHost{};
    auto owner = veyra::WindowId{};
    auto child = veyra::WindowId{};
    require(
        host.create("decode owner", 160, 120, {}, owner) == veyra::WindowSetResult::ok,
        "decode owner creates"
    );
    require(
        host.create("decode child", 120, 90, {.owner = owner}, child) == veyra::WindowSetResult::ok,
        "decode child creates"
    );

    require(host.begin_decode(owner, 42), "owner decode begins");
    require(host.begin_decode(child, 43), "child decode begins");
    require(!host.begin_decode(owner, 42), "duplicate decode requests are rejected");
    require(!host.begin_decode(veyra::kInvalidWindowId, 44), "unknown windows reject decodes");
    require(!host.begin_decode(owner, 0), "zero requests are rejected");
    require(host.complete_decode(42), "owner decode completes");
    require(host.complete_decode(43), "child decode completes");

    require(host.destroy(child) == veyra::WindowSetResult::ok, "decode child destroys");
    require(host.decode_discarded(43), "destroyed child discards its completion");
    require(!host.take_decode(43), "discarded completions cannot be taken");
    require(host.take_decode(42), "live completions can be taken");

    require(host.begin_decode(owner, 44), "pending owner decode begins");
    require(
        host.request_close(owner, veyra::CloseReason::user) == veyra::WindowSetResult::ok,
        "decode owner closes"
    );
    require(host.decode_discarded(44), "closing the owner discards its pending decode");
    require(!host.take_decode(44), "cancelled pending decode cannot be taken");

    auto cascade_owner = veyra::WindowId{};
    auto cascade_child = veyra::WindowId{};
    require(
        host.create("cascade owner", 160, 120, {}, cascade_owner) == veyra::WindowSetResult::ok,
        "cascade owner creates"
    );
    require(
        host.create("cascade child", 120, 90, {.owner = cascade_owner}, cascade_child) ==
            veyra::WindowSetResult::ok,
        "cascade child creates"
    );
    require(host.begin_decode(cascade_child, 45), "cascade child decode begins");
    require(host.complete_decode(45), "cascade child decode completes");
    require(
        host.request_close(cascade_owner, veyra::CloseReason::user) == veyra::WindowSetResult::ok,
        "cascade owner closes"
    );
    require(host.decode_discarded(45), "owner close discards child completions");
    require(!host.take_decode(45), "cascade completion cannot be taken");
}

void check_activation_restores_focus() {
    auto tree = veyra::NodeTree{};
    tree.reserve(4);
    const auto first_root = tree.create();
    const auto first_focus = tree.create();
    const auto second_root = tree.create();
    const auto second_focus = tree.create();
    require(tree.append_child(first_root, first_focus), "first activation focus attaches");
    require(tree.append_child(second_root, second_focus), "second activation focus attaches");

    auto host = veyra::app::sdl3::Sdl3WindowHost{};
    auto first = veyra::WindowId{};
    auto second = veyra::WindowId{};
    require(
        host.create("first", 160, 120, {}, first) == veyra::WindowSetResult::ok,
        "first activation window creates"
    );
    require(
        host.create("second", 120, 90, {}, second) == veyra::WindowSetResult::ok,
        "second activation window creates"
    );
    require(host.bind_semantics(first, tree, first_root), "first activation root binds");
    require(host.bind_semantics(second, tree, second_root), "second activation root binds");
    require(host.set_focused_node(first, first_focus), "first activation focus is stored");
    require(host.set_focused_node(second, second_focus), "second activation focus is stored");

    require(host.activate(first) == first_focus, "first activation restores its focus");
    require(host.router().find(first)->focused(), "first activation is active");
    require(!host.router().find(second)->focused(), "second activation is inactive");

    require(host.activate(second) == second_focus, "second activation restores its focus");
    require(!host.router().find(first)->focused(), "first activation becomes inactive");
    require(host.router().find(second)->focused(), "second activation is active");

    require(host.activate(first) == first_focus, "reactivation restores the first focus");
    require(host.router().find(first)->focused(), "reactivation makes the first window active");
    require(host.activate(veyra::kInvalidWindowId).is_null(), "unknown windows cannot activate");
}

void check_seat_routing() {
    auto tree = veyra::NodeTree{};
    tree.reserve(5);
    const auto first_root = tree.create();
    const auto first_focus = tree.create();
    const auto second_root = tree.create();
    const auto second_focus = tree.create();
    tree.append_child(first_root, first_focus);
    tree.append_child(second_root, second_focus);

    auto host = veyra::app::sdl3::Sdl3WindowHost{};
    auto first = veyra::WindowId{};
    auto second = veyra::WindowId{};
    require(
        host.create("seat first", 160, 120, {}, first) == veyra::WindowSetResult::ok,
        "first seat window creates"
    );
    require(
        host.create("seat second", 120, 90, {}, second) == veyra::WindowSetResult::ok,
        "second seat window creates"
    );
    require(host.bind_semantics(first, tree, first_root), "first seat root binds");
    require(host.bind_semantics(second, tree, second_root), "second seat root binds");
    require(host.set_focused_node(first, first_focus), "first seat focus is stored");
    require(host.set_focused_node(second, second_focus), "second seat focus is stored");

    const auto default_seat = host.default_seat();
    require(default_seat != veyra::kInvalidSeatId, "the host creates a default seat");
    require(host.active_seat() == default_seat, "the default seat starts active");
    require(host.seat_count() == 1, "the host starts with one seat");

    const auto second_seat = host.create_seat();
    require(second_seat != veyra::kInvalidSeatId, "a second seat is created");
    require(second_seat != default_seat, "seat ids are distinct");
    require(host.seat_count() == 2, "both seats are live");

    require(host.activate(default_seat, first) == first_focus, "seat one activates its window");
    require(host.activate(second_seat, second) == second_focus, "seat two activates its window");
    require(host.seat_focused_node(default_seat) == first_focus, "seat one keeps its focus");
    require(host.seat_focused_node(second_seat) == second_focus, "seat two keeps its focus");

    require(
        host.capture_pointer(default_seat, first, 0, first_focus),
        "seat one captures the pointer"
    );
    require(
        host.capture_pointer(second_seat, second, 0, second_focus),
        "seat two captures the same pointer id"
    );
    require(
        host.seat_pointer_captured(default_seat, 0) && host.seat_pointer_captured(second_seat, 0),
        "both seats report their own capture"
    );
    require(host.release_pointer(default_seat, first, 0), "seat one releases its capture");
    require(
        !host.seat_pointer_captured(default_seat, 0) && host.seat_pointer_captured(second_seat, 0),
        "releasing one seat leaves the other capture alone"
    );

    require(host.set_hover(default_seat, first, first_focus), "seat one hovers");
    require(host.set_hover(second_seat, second, second_focus), "seat two hovers");
    require(
        host.seat_hovered_node(default_seat) == first_focus &&
            host.seat_hovered_node(second_seat) == second_focus,
        "each seat keeps its own hover"
    );

    require(host.set_active_seat(second_seat), "seat two becomes active");
    require(host.set_hover(second_seat, second, second_root), "the active seat updates its hover");
    require(
        host.router().find(second)->hovered_node() == second_root,
        "the router follows the active seat"
    );
    require(host.set_hover(default_seat, first, first_root), "the idle seat updates its hover");
    require(
        host.router().find(second)->hovered_node() == second_root,
        "an idle seat does not rewrite the router"
    );

    require(host.seat_set_modifiers(default_seat, 0x5), "seat one stores modifiers");
    require(host.seat_set_modifiers(second_seat, 0x2), "seat two stores modifiers");
    require(
        host.seat_modifiers(default_seat) == 0x5 && host.seat_modifiers(second_seat) == 0x2,
        "modifiers stay per seat"
    );

    require(!host.release_seat(default_seat), "the default seat cannot be released");
    require(host.release_seat(second_seat), "the extra seat releases");
    require(host.seat_count() == 1, "one seat remains");
    require(host.active_seat() == default_seat, "the active seat falls back to default");
    require(!host.seat_pointer_captured(second_seat, 0), "a released seat has no state");
    require(!host.set_active_seat(second_seat), "a released seat cannot become active");
    require(
        !host.capture_pointer(second_seat, first, 1, first_focus),
        "a released seat rejects input"
    );
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
        require(!host.needs_frame(owner), "a presented window is clean");
        require(!host.needs_frame(child), "a window that is not shown does not need a frame");

        auto damage_native = SDL_Event{};
        damage_native.type = SDL_EVENT_WINDOW_FOCUS_GAINED;
        damage_native.window.windowID = static_cast<Uint32>(host.backend(owner)->window_id());
        require(SDL_PushEvent(&damage_native), "focus gain queues for damage");
        auto damage_event = veyra::WindowEvent{};
        auto damage_window = veyra::WindowId{};
        auto focus_gained = false;
        for (auto attempt = 0; attempt < 16; ++attempt) {
            if (!host.poll_event(damage_event, damage_window)) {
                break;
            }
            if (damage_window == owner &&
                damage_event.kind == veyra::WindowEventKind::focus_gained) {
                focus_gained = true;
                break;
            }
        }
        require(focus_gained, "focus gain reaches the clean window");
        require(host.needs_frame(owner), "an event marks a presented window dirty");
        require(
            host.commit(owner, display, 160, 120) == veyra::WindowSetResult::ok,
            "damage frame commits"
        );
        require(
            host.present(owner, backing) == veyra::WindowSetResult::ok,
            "damage frame presents"
        );
        require(!host.needs_frame(owner), "presenting clears the dirty state");
        require(!host.needs_frame(veyra::kInvalidWindowId), "unknown windows never need a frame");

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

        auto push_mouse_down = [&](veyra::WindowId window) noexcept {
            auto native = SDL_Event{};
            native.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
            native.button.windowID = static_cast<Uint32>(host.backend(window)->window_id());
            native.button.button = SDL_BUTTON_LEFT;
            native.button.down = true;
            native.button.clicks = 1;
            native.button.x = 10.0F;
            native.button.y = 10.0F;
            return SDL_PushEvent(&native);
        };

        while (host.poll_event(pointer_event, pointer_window)) {
        }
        require(push_mouse_down(owner), "blocked pointer queues");
        auto blocked_seen = false;
        for (auto attempt = 0; attempt < 16; ++attempt) {
            if (!host.poll_event(pointer_event, pointer_window)) {
                break;
            }
            if (pointer_window == owner &&
                pointer_event.kind == veyra::WindowEventKind::pointer_down) {
                blocked_seen = true;
            }
        }
        require(!blocked_seen, "a modal dialog blocks owner pointer input");

        require(push_mouse_down(child), "unrelated child pointer queues");
        auto child_seen = false;
        for (auto attempt = 0; attempt < 16; ++attempt) {
            if (!host.poll_event(pointer_event, pointer_window)) {
                break;
            }
            if (pointer_window == child &&
                pointer_event.kind == veyra::WindowEventKind::pointer_down) {
                child_seen = true;
            }
        }
        require(child_seen, "a modal dialog leaves unrelated windows interactive");

        require(push_mouse_down(dialog), "dialog pointer queues");
        auto dialog_seen = false;
        for (auto attempt = 0; attempt < 16; ++attempt) {
            if (!host.poll_event(pointer_event, pointer_window)) {
                break;
            }
            if (pointer_window == dialog &&
                pointer_event.kind == veyra::WindowEventKind::pointer_down) {
                dialog_seen = true;
            }
        }
        require(dialog_seen, "the modal dialog receives its own pointer input");

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
#if defined(VEYRA_TEST_WITH_CODECS)
        check_async_decode();
#endif
        check_decode_completion_discard();
        check_activation_restores_focus();
        check_seat_routing();
        check_pipelined_present();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }

    return 0;
}
