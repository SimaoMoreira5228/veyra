#include "veyra/app/sdl3/window_host.hpp"
#include "veyra/platform/headless/fake_services.hpp"

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

void check_service_dialog() {
    auto services = veyra::FakeServices{};
    auto host = veyra::app::sdl3::Sdl3WindowHost{};
    host.set_dialog_service(&services);

    auto owner = veyra::WindowId{};
    require(
        host.create("dialog service", 160, 120, {}, owner) == veyra::WindowSetResult::ok,
        "dialog service window creates"
    );
    require(
        host.open_dialog(owner, veyra::DialogKind::confirm) == veyra::ServiceResult::ok,
        "the service dialog opens"
    );
    require(host.has_pending_dialog(owner), "the service dialog is pending");
    require(
        host.open_dialog(veyra::kInvalidWindowId, veyra::DialogKind::alert) ==
            veyra::ServiceResult::unavailable,
        "unknown owners cannot open service dialogs"
    );

    auto native = SDL_Event{};
    native.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
    native.button.windowID = static_cast<Uint32>(host.backend(owner)->window_id());
    native.button.button = SDL_BUTTON_LEFT;
    native.button.down = true;
    native.button.clicks = 1;
    native.button.x = 10.0F;
    native.button.y = 10.0F;
    require(SDL_PushEvent(&native), "service dialog pointer queues");

    auto event = veyra::WindowEvent{};
    auto window = veyra::WindowId{};
    while (host.poll_event(event, window)) {
        require(
            !(window == owner && event.kind == veyra::WindowEventKind::pointer_down),
            "a pending service dialog blocks its owner"
        );
    }

    require(
        services.dialog_answer_pending({.choice = 2, .cancelled = false}) ==
            veyra::ServiceResult::ok,
        "the service dialog answer resolves"
    );
    auto completed = false;
    for (auto attempt = 0; attempt < 16; ++attempt) {
        if (!host.poll_event(event, window)) {
            break;
        }
        if (window == owner && event.kind == veyra::WindowEventKind::dialog_complete) {
            completed = true;
            require(event.data.dialog.choice == 2, "the completion keeps the answer");
            require(!event.data.dialog.cancelled, "the completion keeps cancellation");
            require(event.data.dialog.handle != 0, "the completion keeps the handle");
            break;
        }
    }
    require(completed, "the service dialog completes through polling");
    require(!host.has_pending_dialog(owner), "the completed dialog leaves the pending set");

    require(SDL_PushEvent(&native), "pointer after completion queues");
    auto delivered = false;
    for (auto attempt = 0; attempt < 16; ++attempt) {
        if (!host.poll_event(event, window)) {
            break;
        }
        if (window == owner && event.kind == veyra::WindowEventKind::pointer_down) {
            delivered = true;
            break;
        }
    }
    require(delivered, "input resumes after the service dialog completes");
}

}

int main() {
    try {
        check_service_dialog();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }

    return 0;
}
