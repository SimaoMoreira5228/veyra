#include "veyra/platform/null_window.hpp"
#include "veyra/platform/sdl3_translate.hpp"
#include "veyra/platform/sdl3_window.hpp"

#include <SDL3/SDL.h>

#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

constexpr std::uint64_t kWindow = 9;
constexpr std::uint32_t kDevice = 42;

void require(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

void check_translate_gamepad() {
    SDL_Event in{};
    veyra::WindowEvent out{};

    in.type = SDL_EVENT_GAMEPAD_AXIS_MOTION;
    in.gaxis.which = kDevice;
    in.gaxis.axis = SDL_GAMEPAD_AXIS_LEFTX;
    in.gaxis.value = 32767;

    require(veyra::translate_sdl_event(in, kWindow, out), "gamepad axis translates");
    require(
        out.kind == veyra::WindowEventKind::gamepad_axis && out.window == kWindow &&
            out.data.gamepad_axis.device == kDevice &&
            out.data.gamepad_axis.axis == SDL_GAMEPAD_AXIS_LEFTX &&
            out.data.gamepad_axis.value == 1.0F,
        "full-scale gamepad axis keeps its device, code, and unit value"
    );

    in.gaxis.value = -32768;

    require(veyra::translate_sdl_event(in, kWindow, out), "negative axis translates");
    require(
        out.data.gamepad_axis.value == -1.0F,
        "the most negative stick deflection clamps to minus one"
    );

    in.gaxis.value = 0;

    require(veyra::translate_sdl_event(in, kWindow, out), "centered axis translates");
    require(out.data.gamepad_axis.value == 0.0F, "a centered axis reads exactly zero");

    in.gaxis.axis = SDL_GAMEPAD_AXIS_COUNT;

    require(!veyra::translate_sdl_event(in, kWindow, out), "an out-of-range axis is refused");

    in = {};
    in.type = SDL_EVENT_GAMEPAD_BUTTON_DOWN;
    in.gbutton.which = kDevice;
    in.gbutton.button = SDL_GAMEPAD_BUTTON_SOUTH;
    in.gbutton.down = true;

    require(veyra::translate_sdl_event(in, kWindow, out), "gamepad press translates");
    require(
        out.kind == veyra::WindowEventKind::gamepad_button &&
            out.data.gamepad_button.device == kDevice &&
            out.data.gamepad_button.button == SDL_GAMEPAD_BUTTON_SOUTH &&
            out.data.gamepad_button.pressed,
        "a press keeps its device, code, and pressed flag"
    );

    in = {};
    in.type = SDL_EVENT_GAMEPAD_BUTTON_UP;
    in.gbutton.which = kDevice;
    in.gbutton.button = SDL_GAMEPAD_BUTTON_EAST;
    in.gbutton.down = false;

    require(veyra::translate_sdl_event(in, kWindow, out), "gamepad release translates");
    require(
        out.kind == veyra::WindowEventKind::gamepad_button &&
            out.data.gamepad_button.button == SDL_GAMEPAD_BUTTON_EAST &&
            !out.data.gamepad_button.pressed,
        "a release clears its pressed flag"
    );

    in.gbutton.button = SDL_GAMEPAD_BUTTON_COUNT;

    require(!veyra::translate_sdl_event(in, kWindow, out), "an out-of-range button is refused");

    in = {};
    in.type = SDL_EVENT_GAMEPAD_ADDED;
    in.gdevice.which = kDevice;

    require(veyra::translate_sdl_event(in, kWindow, out), "gamepad arrival translates");
    require(
        out.kind == veyra::WindowEventKind::gamepad_added &&
            out.data.gamepad_device.device == kDevice,
        "arrival keeps its device id"
    );

    in = {};
    in.type = SDL_EVENT_GAMEPAD_REMOVED;
    in.gdevice.which = kDevice;

    require(veyra::translate_sdl_event(in, kWindow, out), "gamepad removal translates");
    require(out.kind == veyra::WindowEventKind::gamepad_removed, "removal keeps its own kind");

    in = {};
    in.type = SDL_EVENT_GAMEPAD_REMAPPED;

    require(!veyra::translate_sdl_event(in, kWindow, out), "remap notices are refused");
}

void check_translate_joystick() {
    SDL_Event in{};
    veyra::WindowEvent out{};

    in.type = SDL_EVENT_JOYSTICK_AXIS_MOTION;
    in.jaxis.which = kDevice;
    in.jaxis.axis = 3;
    in.jaxis.value = -32768;

    require(veyra::translate_sdl_event(in, kWindow, out), "joystick axis translates");
    require(
        out.kind == veyra::WindowEventKind::gamepad_axis &&
            out.data.gamepad_axis.device == kDevice && out.data.gamepad_axis.axis == 3 &&
            out.data.gamepad_axis.value == -1.0F,
        "raw joystick axes share the gamepad axis payload"
    );

    in = {};
    in.type = SDL_EVENT_JOYSTICK_BUTTON_DOWN;
    in.jbutton.which = kDevice;
    in.jbutton.button = 5;
    in.jbutton.down = true;

    require(veyra::translate_sdl_event(in, kWindow, out), "joystick press translates");
    require(
        out.kind == veyra::WindowEventKind::gamepad_button && out.data.gamepad_button.button == 5 &&
            out.data.gamepad_button.pressed,
        "raw joystick buttons keep their index"
    );

    in = {};
    in.type = SDL_EVENT_JOYSTICK_BUTTON_UP;
    in.jbutton.which = kDevice;
    in.jbutton.button = 5;
    in.jbutton.down = false;

    require(veyra::translate_sdl_event(in, kWindow, out), "joystick release translates");
    require(!out.data.gamepad_button.pressed, "a raw release clears its pressed flag");

    in = {};
    in.type = SDL_EVENT_JOYSTICK_ADDED;
    in.jdevice.which = kDevice;

    require(veyra::translate_sdl_event(in, kWindow, out), "joystick arrival translates");
    require(out.kind == veyra::WindowEventKind::gamepad_added, "raw arrival shares the added kind");

    in = {};
    in.type = SDL_EVENT_JOYSTICK_REMOVED;
    in.jdevice.which = kDevice;

    require(veyra::translate_sdl_event(in, kWindow, out), "joystick removal translates");
    require(
        out.kind == veyra::WindowEventKind::gamepad_removed,
        "raw removal shares the removed kind"
    );

    in = {};
    in.type = SDL_EVENT_JOYSTICK_HAT_MOTION;

    require(!veyra::translate_sdl_event(in, kWindow, out), "hat motion has no neutral payload");

    in = {};
    in.type = SDL_EVENT_JOYSTICK_BALL_MOTION;

    require(!veyra::translate_sdl_event(in, kWindow, out), "ball motion has no neutral payload");

    in = {};
    in.type = SDL_EVENT_JOYSTICK_BATTERY_UPDATED;

    require(!veyra::translate_sdl_event(in, kWindow, out), "battery notices are refused");

    in = {};
    in.type = SDL_EVENT_GAMEPAD_TOUCHPAD_DOWN;

    require(!veyra::translate_sdl_event(in, kWindow, out), "touchpad contact is refused");

    in = {};
    in.type = SDL_EVENT_GAMEPAD_SENSOR_UPDATE;

    require(!veyra::translate_sdl_event(in, kWindow, out), "sensor updates are refused");

    in = {};
    in.type = SDL_EVENT_GAMEPAD_UPDATE_COMPLETE;

    require(!veyra::translate_sdl_event(in, kWindow, out), "update markers are refused");
}

void check_null_reports_zero() {
    veyra::NullWindow window("gamepad null", 64, 48);
    require(window.creation_result() == veyra::WindowResult::ready, "null window creates");
    require(window.gamepad_count() == 0, "null reports zero gamepads");

    veyra::WindowEvent event{};
    require(!window.poll_event(event), "null never queues gamepad events");
}

std::size_t drain(veyra::Sdl3Window& window) {
    std::size_t count = 0;
    veyra::WindowEvent event{};

    while (window.poll_event(event)) {
        ++count;
    }

    return count;
}

bool poll_kind(veyra::Sdl3Window& window, veyra::WindowEventKind kind, veyra::WindowEvent& found) {
    veyra::WindowEvent event{};

    while (window.poll_event(event)) {
        if (event.kind == kind) {
            found = event;
            return true;
        }
    }

    return false;
}

std::size_t count_kind(veyra::Sdl3Window& window, veyra::WindowEventKind kind) {
    std::size_t count = 0;
    veyra::WindowEvent event{};

    while (window.poll_event(event)) {
        if (event.kind == kind) {
            ++count;
        }
    }

    return count;
}

void check_broadcast(veyra::Sdl3Window& first, veyra::Sdl3Window& second) {
    drain(first);
    drain(second);

    SDL_Event pushed{};
    pushed.type = SDL_EVENT_GAMEPAD_BUTTON_DOWN;
    pushed.gbutton.which = kDevice;
    pushed.gbutton.button = SDL_GAMEPAD_BUTTON_SOUTH;
    pushed.gbutton.down = true;
    require(SDL_PushEvent(&pushed), "test queues a gamepad press");

    veyra::WindowEvent found{};
    require(
        poll_kind(first, veyra::WindowEventKind::gamepad_button, found),
        "first sees the press"
    );
    require(
        found.window == first.window_id() && found.data.gamepad_button.device == kDevice &&
            found.data.gamepad_button.button == SDL_GAMEPAD_BUTTON_SOUTH &&
            found.data.gamepad_button.pressed,
        "the press reaches the first window with its own id"
    );
    require(
        poll_kind(second, veyra::WindowEventKind::gamepad_button, found),
        "second sees the press"
    );
    require(
        found.window == second.window_id() && found.data.gamepad_button.device == kDevice,
        "the press reaches the second window with its own id"
    );

    pushed = {};
    pushed.type = SDL_EVENT_JOYSTICK_AXIS_MOTION;
    pushed.jaxis.which = kDevice;
    pushed.jaxis.axis = 2;
    pushed.jaxis.value = 16384;
    require(SDL_PushEvent(&pushed), "test queues a joystick axis");

    require(poll_kind(first, veyra::WindowEventKind::gamepad_axis, found), "first sees the axis");
    require(
        found.data.gamepad_axis.device == kDevice && found.data.gamepad_axis.axis == 2 &&
            found.data.gamepad_axis.value == 16384.0F / 32767.0F,
        "a raw axis keeps its device, index, and scaled value"
    );
    require(
        count_kind(second, veyra::WindowEventKind::gamepad_axis) == 1,
        "the axis reaches the second window exactly once"
    );
}

void check_virtual_joystick(veyra::Sdl3Window& window) {
    drain(window);
    require(window.gamepad_count() == 0, "no gamepads precede the virtual stick");

    SDL_VirtualJoystickDesc desc{};
    SDL_INIT_INTERFACE(&desc);
    desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
    desc.naxes = 2;
    desc.nbuttons = 2;
    desc.button_mask = (1U << SDL_GAMEPAD_BUTTON_SOUTH) | (1U << SDL_GAMEPAD_BUTTON_EAST);
    desc.axis_mask = (1U << SDL_GAMEPAD_AXIS_LEFTX) | (1U << SDL_GAMEPAD_AXIS_LEFTY);
    desc.name = "Veyra virtual";

    const SDL_JoystickID id = SDL_AttachVirtualJoystick(&desc);
    require(id != 0, "the virtual stick attaches");

    SDL_Joystick* stick = SDL_OpenJoystick(id);
    require(stick != nullptr, "the virtual stick opens");
    require(window.gamepad_count() == 1, "the open virtual stick counts as one gamepad");

    SDL_UpdateJoysticks();

    veyra::WindowEvent found{};
    require(
        poll_kind(window, veyra::WindowEventKind::gamepad_added, found),
        "arrival surfaces through the route"
    );
    require(
        found.window == window.window_id() &&
            found.data.gamepad_device.device == static_cast<std::uint32_t>(id),
        "arrival carries the virtual device id"
    );

    require(SDL_SetJoystickVirtualAxis(stick, 0, 16384), "the virtual axis takes a deflection");
    SDL_UpdateJoysticks();

    veyra::WindowEvent axis{};
    bool axis_seen = false;
    while (window.poll_event(axis)) {
        if (axis.kind == veyra::WindowEventKind::gamepad_axis &&
            axis.data.gamepad_axis.device == static_cast<std::uint32_t>(id) &&
            axis.data.gamepad_axis.axis == 0 &&
            axis.data.gamepad_axis.value == 16384.0F / 32767.0F) {
            axis_seen = true;
        }
    }
    require(axis_seen, "the deflected axis surfaces through the route");

    require(SDL_SetJoystickVirtualButton(stick, 0, true), "the virtual button presses");
    SDL_UpdateJoysticks();

    require(
        poll_kind(window, veyra::WindowEventKind::gamepad_button, found),
        "the press surfaces through the route"
    );
    require(
        found.data.gamepad_button.device == static_cast<std::uint32_t>(id) &&
            found.data.gamepad_button.button == SDL_GAMEPAD_BUTTON_SOUTH &&
            found.data.gamepad_button.pressed,
        "the press keeps its device, standard code, and flag"
    );

    require(SDL_SetJoystickVirtualButton(stick, 0, false), "the virtual button releases");
    SDL_UpdateJoysticks();

    require(
        poll_kind(window, veyra::WindowEventKind::gamepad_button, found),
        "the release surfaces through the route"
    );
    require(!found.data.gamepad_button.pressed, "the release clears its flag");

    SDL_CloseJoystick(stick);
    require(SDL_DetachVirtualJoystick(id), "the virtual stick detaches");
    SDL_UpdateJoysticks();

    require(
        poll_kind(window, veyra::WindowEventKind::gamepad_removed, found),
        "removal surfaces through the route"
    );
    require(
        found.data.gamepad_device.device == static_cast<std::uint32_t>(id),
        "removal carries the virtual device id"
    );
    require(window.gamepad_count() == 0, "detaching returns the count to zero");
}

}

int main() {
    check_translate_gamepad();
    check_translate_joystick();
    check_null_reports_zero();

    veyra::Sdl3Window first("Veyra gamepad one", 160, 120);
    veyra::Sdl3Window second("Veyra gamepad two", 120, 90);
    if (first.creation_result() != veyra::WindowResult::ready ||
        second.creation_result() != veyra::WindowResult::ready) {
        std::cerr << "SDL could not create a window in this environment\n";
        return 77;
    }

    try {
        check_broadcast(first, second);

        const char* driver = SDL_GetCurrentVideoDriver();
        if (driver != nullptr && std::string_view(driver) == "dummy") {
            check_virtual_joystick(first);
        } else {
            std::cerr << "skipping the virtual stick leg without the dummy video driver\n";
        }
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }

    return 0;
}
