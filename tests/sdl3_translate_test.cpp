#include "veyra/platform/sdl3/sdl3_translate.hpp"

#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

constexpr std::uint64_t kWindow = 9;

void require(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

void check_close_and_focus() {
    SDL_Event in{};
    veyra::WindowEvent out{};

    in.type = SDL_EVENT_QUIT;

    require(veyra::translate_sdl_event(in, kWindow, out), "quit translates");
    require(
        out.kind == veyra::WindowEventKind::close_request && out.window == kWindow,
        "quit becomes a close request for the mapped window"
    );

    in = {};
    in.type = SDL_EVENT_WINDOW_CLOSE_REQUESTED;

    require(veyra::translate_sdl_event(in, kWindow, out), "close request translates");
    require(out.kind == veyra::WindowEventKind::close_request, "close keeps its kind");

    in = {};
    in.type = SDL_EVENT_WINDOW_FOCUS_GAINED;

    require(veyra::translate_sdl_event(in, kWindow, out), "focus gained translates");
    require(out.kind == veyra::WindowEventKind::focus_gained, "focus gained keeps its kind");

    in = {};
    in.type = SDL_EVENT_WINDOW_FOCUS_LOST;

    require(veyra::translate_sdl_event(in, kWindow, out), "focus lost translates");
    require(out.kind == veyra::WindowEventKind::focus_lost, "focus lost keeps its kind");

    in = {};
    in.type = SDL_EVENT_WINDOW_MINIMIZED;

    require(veyra::translate_sdl_event(in, kWindow, out), "minimize translates");
    require(
        out.kind == veyra::WindowEventKind::state_change &&
            out.data.state == veyra::WindowState::minimized,
        "minimize becomes the minimized state"
    );

    in = {};
    in.type = SDL_EVENT_WINDOW_MAXIMIZED;

    require(veyra::translate_sdl_event(in, kWindow, out), "maximize translates");
    require(
        out.data.state == veyra::WindowState::maximized,
        "maximize becomes the maximized state"
    );

    in = {};
    in.type = SDL_EVENT_WINDOW_RESTORED;

    require(veyra::translate_sdl_event(in, kWindow, out), "restore translates");
    require(out.data.state == veyra::WindowState::normal, "restore becomes the normal state");

    in = {};
    in.type = SDL_EVENT_WINDOW_ENTER_FULLSCREEN;

    require(veyra::translate_sdl_event(in, kWindow, out), "fullscreen enter translates");
    require(
        out.data.state == veyra::WindowState::fullscreen,
        "fullscreen enter becomes the fullscreen state"
    );

    in = {};
    in.type = SDL_EVENT_WINDOW_LEAVE_FULLSCREEN;

    require(veyra::translate_sdl_event(in, kWindow, out), "fullscreen leave translates");
    require(
        out.data.state == veyra::WindowState::normal,
        "fullscreen leave becomes the normal state"
    );

    in = {};
    in.type = SDL_EVENT_WINDOW_MOUSE_LEAVE;

    require(veyra::translate_sdl_event(in, kWindow, out), "mouse leave translates");
    require(out.kind == veyra::WindowEventKind::pointer_leave, "mouse leave becomes pointer leave");

    in = {};
    in.type = SDL_EVENT_RENDER_DEVICE_LOST;

    require(veyra::translate_sdl_event(in, kWindow, out), "device loss translates");
    require(out.kind == veyra::WindowEventKind::device_lost, "device loss keeps its own kind");

    in = {};
    in.type = SDL_EVENT_WINDOW_RESIZED;
    in.window.data1 = 160;
    in.window.data2 = 120;

    require(veyra::translate_sdl_event(in, kWindow, out), "resize translates");
    require(
        out.kind == veyra::WindowEventKind::resize && out.data.resize.width == 160.0F &&
            out.data.resize.height == 120.0F,
        "resize keeps logical dimensions"
    );

    in.window.data1 = 0;

    require(!veyra::translate_sdl_event(in, kWindow, out), "zero-width resize is refused");
}

void check_pointer() {
    SDL_Event in{};
    veyra::WindowEvent out{};

    in.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
    in.button.button = SDL_BUTTON_LEFT;
    in.button.down = true;
    in.button.clicks = 1;
    in.button.x = 10.0F;
    in.button.y = 20.0F;

    require(veyra::translate_sdl_event(in, kWindow, out), "mouse down translates");
    require(
        out.kind == veyra::WindowEventKind::pointer_down && out.data.pointer.position.x == 10.0F &&
            out.data.pointer.position.y == 20.0F &&
            out.data.pointer.pointer == veyra::kMousePointerId && out.data.pointer.click_count == 1,
        "mouse down keeps logical position and the mouse id"
    );
    require(
        veyra::has_button(out.data.pointer.buttons, veyra::PointerButton::primary),
        "left button maps to primary"
    );

    in.button.clicks = 2;
    SDL_SetModState(SDL_KMOD_CTRL | SDL_KMOD_SHIFT);
    require(veyra::translate_sdl_event(in, kWindow, out), "mouse double-click translates");
    require(
        out.data.pointer.click_count == 2 &&
            veyra::has_modifier(out.data.pointer.modifiers, veyra::ShortcutModifier::ctrl) &&
            veyra::has_modifier(out.data.pointer.modifiers, veyra::ShortcutModifier::shift),
        "mouse click count and modifiers are preserved"
    );
    SDL_SetModState(SDL_KMOD_NONE);

    in = {};
    in.type = SDL_EVENT_MOUSE_BUTTON_UP;
    in.button.button = SDL_BUTTON_RIGHT;
    in.button.down = false;

    require(veyra::translate_sdl_event(in, kWindow, out), "mouse up translates");
    require(
        out.kind == veyra::WindowEventKind::pointer_up &&
            veyra::has_button(out.data.pointer.buttons, veyra::PointerButton::secondary),
        "right button maps to secondary on release"
    );

    in = {};
    in.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
    in.button.button = 9;
    in.button.down = true;

    require(!veyra::translate_sdl_event(in, kWindow, out), "unknown buttons are refused");

    in = {};
    in.type = SDL_EVENT_MOUSE_MOTION;
    in.motion.state = SDL_BUTTON_LMASK | SDL_BUTTON_RMASK;
    in.motion.x = 4.0F;
    in.motion.y = 6.0F;

    require(veyra::translate_sdl_event(in, kWindow, out), "mouse motion translates");
    require(
        out.kind == veyra::WindowEventKind::pointer_move &&
            veyra::has_button(out.data.pointer.buttons, veyra::PointerButton::primary) &&
            veyra::has_button(out.data.pointer.buttons, veyra::PointerButton::secondary),
        "motion carries the chorded button mask"
    );

    in = {};
    in.type = SDL_EVENT_MOUSE_WHEEL;
    in.wheel.x = 1.0F;
    in.wheel.y = -2.0F;
    in.wheel.direction = SDL_MOUSEWHEEL_NORMAL;

    require(veyra::translate_sdl_event(in, kWindow, out), "wheel translates");
    require(
        out.kind == veyra::WindowEventKind::wheel && out.data.wheel.delta_x == 1.0F &&
            out.data.wheel.delta_y == -2.0F,
        "normal wheel keeps its deltas"
    );

    in.wheel.direction = SDL_MOUSEWHEEL_FLIPPED;

    require(veyra::translate_sdl_event(in, kWindow, out), "flipped wheel translates");
    require(
        out.data.wheel.delta_x == -1.0F && out.data.wheel.delta_y == 2.0F,
        "flipped wheel negates its deltas"
    );
}

void check_keyboard() {
    SDL_Event in{};
    veyra::WindowEvent out{};

    in.type = SDL_EVENT_KEY_DOWN;
    in.key.key = SDLK_A;
    in.key.mod = SDL_KMOD_CTRL | SDL_KMOD_SHIFT;
    in.key.down = true;
    in.key.repeat = false;

    require(veyra::translate_sdl_event(in, kWindow, out), "key down translates");
    require(
        out.kind == veyra::WindowEventKind::key_down &&
            out.data.key.code == static_cast<std::uint32_t>(SDLK_A) && !out.data.key.repeat,
        "key down keeps the raw code without repeat"
    );
    require(
        veyra::has_modifier(out.data.key.modifiers, veyra::ShortcutModifier::ctrl) &&
            veyra::has_modifier(out.data.key.modifiers, veyra::ShortcutModifier::shift) &&
            !veyra::has_modifier(out.data.key.modifiers, veyra::ShortcutModifier::alt),
        "SDL modifiers land in the shortcut vocabulary"
    );

    in.key.repeat = true;

    require(veyra::translate_sdl_event(in, kWindow, out), "key repeat translates");
    require(out.data.key.repeat, "repeat flag survives translation");

    in = {};
    in.type = SDL_EVENT_KEY_UP;
    in.key.key = SDLK_ESCAPE;
    in.key.mod = SDL_KMOD_GUI;
    in.key.down = false;

    require(veyra::translate_sdl_event(in, kWindow, out), "key up translates");
    require(
        out.kind == veyra::WindowEventKind::key_up &&
            veyra::has_modifier(out.data.key.modifiers, veyra::ShortcutModifier::super),
        "GUI modifier maps to super"
    );
}

void check_text() {
    SDL_Event in{};
    veyra::WindowEvent out{};

    in.type = SDL_EVENT_TEXT_INPUT;
    in.text.text = "hi";

    require(veyra::translate_sdl_event(in, kWindow, out), "text input translates");
    require(
        out.kind == veyra::WindowEventKind::text_commit && out.data.commit.length == 2 &&
            !out.data.commit.truncated,
        "short commit keeps its length"
    );
    require(
        std::string_view(out.data.commit.text.data(), 2) == "hi",
        "short commit keeps its bytes"
    );

    std::string overlong(veyra::kWindowTextCommitCapacity + 40, 'a');

    in.text.text = overlong.c_str();

    require(veyra::translate_sdl_event(in, kWindow, out), "overlong commit translates");
    require(
        out.data.commit.length == veyra::kWindowTextCommitCapacity && out.data.commit.truncated,
        "overlong commit truncates with a flag"
    );

    in = {};
    in.type = SDL_EVENT_TEXT_EDITING;
    in.edit.text = "h";
    in.edit.start = 1;
    in.edit.length = 0;

    require(veyra::translate_sdl_event(in, kWindow, out), "text editing translates");
    require(
        out.kind == veyra::WindowEventKind::text_editing && out.data.editing.length == 1 &&
            out.data.editing.cursor == 1 && out.data.editing.selection_length == 0,
        "editing keeps text, cursor, and selection"
    );
}

void check_refusals() {
    SDL_Event in{};
    veyra::WindowEvent out{};

    out.kind = veyra::WindowEventKind::close_request;

    in.type = SDL_EVENT_DROP_FILE;

    require(!veyra::translate_sdl_event(in, kWindow, out), "drop events are refused");
    require(
        out.kind == veyra::WindowEventKind::close_request,
        "refused events leave the output alone"
    );

    in = {};
    in.type = SDL_EVENT_WINDOW_MOVED;

    require(!veyra::translate_sdl_event(in, kWindow, out), "unmapped window events are refused");

    in = {};
    in.type = SDL_EVENT_FINGER_DOWN;

    require(!veyra::translate_sdl_event(in, kWindow, out), "touch events are refused");
}

}

int main() {
    try {
        check_close_and_focus();
        check_pointer();
        check_keyboard();
        check_text();
        check_refusals();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
