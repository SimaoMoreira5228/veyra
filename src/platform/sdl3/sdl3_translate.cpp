#include "veyra/platform/sdl3/sdl3_translate.hpp"

#include <algorithm>
#include <span>

namespace veyra {
namespace {

std::uint32_t translate_modifiers(SDL_Keymod mods) noexcept {
    std::uint32_t mask = 0;

    if ((mods & SDL_KMOD_CTRL) != 0) {
        mask |= modifier_flag(ShortcutModifier::ctrl);
    }

    if ((mods & SDL_KMOD_ALT) != 0) {
        mask |= modifier_flag(ShortcutModifier::alt);
    }

    if ((mods & SDL_KMOD_SHIFT) != 0) {
        mask |= modifier_flag(ShortcutModifier::shift);
    }

    if ((mods & SDL_KMOD_GUI) != 0) {
        mask |= modifier_flag(ShortcutModifier::super);
    }

    return mask;
}

std::uint32_t translate_button(Uint8 button) noexcept {
    switch (button) {
    case SDL_BUTTON_LEFT:
        return button_flag(PointerButton::primary);
    case SDL_BUTTON_MIDDLE:
        return button_flag(PointerButton::middle);
    case SDL_BUTTON_RIGHT:
        return button_flag(PointerButton::secondary);
    case SDL_BUTTON_X1:
        return button_flag(PointerButton::back);
    case SDL_BUTTON_X2:
        return button_flag(PointerButton::forward);
    default:
        return 0;
    }
}

std::uint32_t translate_button_state(SDL_MouseButtonFlags state) noexcept {
    std::uint32_t mask = 0;

    if ((state & SDL_BUTTON_LMASK) != 0) {
        mask |= button_flag(PointerButton::primary);
    }

    if ((state & SDL_BUTTON_MMASK) != 0) {
        mask |= button_flag(PointerButton::middle);
    }

    if ((state & SDL_BUTTON_RMASK) != 0) {
        mask |= button_flag(PointerButton::secondary);
    }

    if ((state & SDL_BUTTON_X1MASK) != 0) {
        mask |= button_flag(PointerButton::back);
    }

    if ((state & SDL_BUTTON_X2MASK) != 0) {
        mask |= button_flag(PointerButton::forward);
    }

    return mask;
}

void copy_commit_text(const char* source, WindowTextCommit& commit) noexcept {
    commit.length = 0;
    commit.truncated = false;

    if (source == nullptr) {
        return;
    }

    std::span<char> target(commit.text);

    while (source[commit.length] != '\0') {
        if (commit.length >= target.size()) {
            commit.truncated = true;
            return;
        }

        target[commit.length] = source[commit.length];
        ++commit.length;
    }
}

void copy_preedit_text(const char* source, WindowTextEditing& editing) noexcept {
    editing.length = 0;
    editing.truncated = false;

    if (source == nullptr) {
        return;
    }

    std::span<char> target(editing.text);

    while (source[editing.length] != '\0') {
        if (editing.length >= target.size()) {
            editing.truncated = true;
            return;
        }

        target[editing.length] = source[editing.length];
        ++editing.length;
    }
}

bool translate_close(WindowEvent& out) noexcept {
    out.kind = WindowEventKind::close_request;

    return true;
}

bool translate_resize(const SDL_WindowEvent& in, WindowEvent& out) noexcept {
    if (in.data1 <= 0 || in.data2 <= 0) {
        return false;
    }

    out.kind = WindowEventKind::resize;
    out.data.resize = {
        .width = static_cast<float>(in.data1),
        .height = static_cast<float>(in.data2),
    };

    return true;
}

bool translate_state(WindowState state, WindowEvent& out) noexcept {
    out.kind = WindowEventKind::state_change;
    out.data.state = state;

    return true;
}

bool translate_focus(bool gained, WindowEvent& out) noexcept {
    out.kind = gained ? WindowEventKind::focus_gained : WindowEventKind::focus_lost;

    return true;
}

bool translate_mouse_button(const SDL_MouseButtonEvent& in, WindowEvent& out) noexcept {
    const std::uint32_t button = translate_button(in.button);

    if (button == 0) {
        return false;
    }

    out.kind = in.down ? WindowEventKind::pointer_down : WindowEventKind::pointer_up;
    out.data.pointer = {
        .position =
            {
                .x = in.x,
                .y = in.y,
            },
        .pointer = kMousePointerId,
        .buttons = button,
        .modifiers = translate_modifiers(SDL_GetModState()),
        .click_count = in.clicks,
    };

    return true;
}

bool translate_mouse_motion(const SDL_MouseMotionEvent& in, WindowEvent& out) noexcept {
    out.kind = WindowEventKind::pointer_move;
    out.data.pointer = {
        .position =
            {
                .x = in.x,
                .y = in.y,
            },
        .pointer = kMousePointerId,
        .buttons = translate_button_state(in.state),
        .modifiers = 0,
    };

    return true;
}

bool translate_mouse_wheel(const SDL_MouseWheelEvent& in, WindowEvent& out) noexcept {
    float delta_x = in.x;
    float delta_y = in.y;

    if (in.direction == SDL_MOUSEWHEEL_FLIPPED) {
        delta_x = -delta_x;
        delta_y = -delta_y;
    }

    out.kind = WindowEventKind::wheel;
    out.data.wheel = {
        .delta_x = delta_x,
        .delta_y = delta_y,
        .modifiers = 0,
    };

    return true;
}

bool translate_key(const SDL_KeyboardEvent& in, WindowEvent& out) noexcept {
    out.kind = in.down ? WindowEventKind::key_down : WindowEventKind::key_up;
    out.data.key = {
        .code = static_cast<std::uint32_t>(in.key),
        .modifiers = translate_modifiers(in.mod),
        .repeat = in.repeat,
    };

    return true;
}

bool translate_text_input(const SDL_TextInputEvent& in, WindowEvent& out) noexcept {
    out.kind = WindowEventKind::text_commit;
    copy_commit_text(in.text, out.data.commit);

    return true;
}

bool translate_text_editing(const SDL_TextEditingEvent& in, WindowEvent& out) noexcept {
    out.kind = WindowEventKind::text_editing;
    copy_preedit_text(in.text, out.data.editing);
    out.data.editing.cursor = in.start;
    out.data.editing.selection_length = in.length;

    return true;
}

float normalize_stick(Sint16 value) noexcept {
    constexpr float kPositive = 32767.0F;
    constexpr float kNegative = 32768.0F;

    const float scaled =
        value < 0 ? static_cast<float>(value) / kNegative : static_cast<float>(value) / kPositive;

    return std::clamp(scaled, -1.0F, 1.0F);
}

bool translate_gamepad_axis(const SDL_GamepadAxisEvent& in, WindowEvent& out) noexcept {
    if (in.axis >= SDL_GAMEPAD_AXIS_COUNT) {
        return false;
    }

    out.kind = WindowEventKind::gamepad_axis;
    out.data.gamepad_axis = {
        .device = static_cast<std::uint32_t>(in.which),
        .axis = static_cast<std::uint32_t>(in.axis),
        .value = normalize_stick(in.value),
    };

    return true;
}

bool translate_gamepad_button(const SDL_GamepadButtonEvent& in, WindowEvent& out) noexcept {
    if (in.button >= SDL_GAMEPAD_BUTTON_COUNT) {
        return false;
    }

    out.kind = WindowEventKind::gamepad_button;
    out.data.gamepad_button = {
        .device = static_cast<std::uint32_t>(in.which),
        .button = static_cast<std::uint32_t>(in.button),
        .pressed = in.down,
    };

    return true;
}

bool translate_gamepad_device(
    bool added,
    const SDL_GamepadDeviceEvent& in,
    WindowEvent& out
) noexcept {
    out.kind = added ? WindowEventKind::gamepad_added : WindowEventKind::gamepad_removed;
    out.data.gamepad_device = {.device = static_cast<std::uint32_t>(in.which)};

    return true;
}

bool translate_joy_axis(const SDL_JoyAxisEvent& in, WindowEvent& out) noexcept {
    out.kind = WindowEventKind::gamepad_axis;
    out.data.gamepad_axis = {
        .device = static_cast<std::uint32_t>(in.which),
        .axis = static_cast<std::uint32_t>(in.axis),
        .value = normalize_stick(in.value),
    };

    return true;
}

bool translate_joy_button(const SDL_JoyButtonEvent& in, WindowEvent& out) noexcept {
    out.kind = WindowEventKind::gamepad_button;
    out.data.gamepad_button = {
        .device = static_cast<std::uint32_t>(in.which),
        .button = static_cast<std::uint32_t>(in.button),
        .pressed = in.down,
    };

    return true;
}

bool translate_joy_device(bool added, const SDL_JoyDeviceEvent& in, WindowEvent& out) noexcept {
    out.kind = added ? WindowEventKind::gamepad_added : WindowEventKind::gamepad_removed;
    out.data.gamepad_device = {.device = static_cast<std::uint32_t>(in.which)};

    return true;
}

}

bool translate_sdl_event(const SDL_Event& in, WindowId window, WindowEvent& out) noexcept {
    WindowEvent mapped{};

    mapped.window = window;

    bool handled = false;

    switch (in.type) {
    case SDL_EVENT_QUIT:
    case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
        handled = translate_close(mapped);
        break;
    case SDL_EVENT_WINDOW_RESIZED:
        handled = translate_resize(in.window, mapped);
        break;
    case SDL_EVENT_WINDOW_FOCUS_GAINED:
        handled = translate_focus(true, mapped);
        break;
    case SDL_EVENT_WINDOW_FOCUS_LOST:
        handled = translate_focus(false, mapped);
        break;
    case SDL_EVENT_WINDOW_MINIMIZED:
        handled = translate_state(WindowState::minimized, mapped);
        break;
    case SDL_EVENT_WINDOW_MAXIMIZED:
        handled = translate_state(WindowState::maximized, mapped);
        break;
    case SDL_EVENT_WINDOW_RESTORED:
        handled = translate_state(WindowState::normal, mapped);
        break;
    case SDL_EVENT_WINDOW_ENTER_FULLSCREEN:
        handled = translate_state(WindowState::fullscreen, mapped);
        break;
    case SDL_EVENT_WINDOW_LEAVE_FULLSCREEN:
        handled = translate_state(WindowState::normal, mapped);
        break;
    case SDL_EVENT_RENDER_DEVICE_LOST:
        mapped.kind = WindowEventKind::device_lost;
        handled = true;
        break;
    case SDL_EVENT_WINDOW_MOUSE_LEAVE:
        mapped.kind = WindowEventKind::pointer_leave;
        handled = true;
        break;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP:
        handled = translate_mouse_button(in.button, mapped);
        break;
    case SDL_EVENT_MOUSE_MOTION:
        handled = translate_mouse_motion(in.motion, mapped);
        break;
    case SDL_EVENT_MOUSE_WHEEL:
        handled = translate_mouse_wheel(in.wheel, mapped);
        break;
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP:
        handled = translate_key(in.key, mapped);
        break;
    case SDL_EVENT_TEXT_INPUT:
        handled = translate_text_input(in.text, mapped);
        break;
    case SDL_EVENT_TEXT_EDITING:
        handled = translate_text_editing(in.edit, mapped);
        break;
    case SDL_EVENT_GAMEPAD_AXIS_MOTION:
        handled = translate_gamepad_axis(in.gaxis, mapped);
        break;
    case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
    case SDL_EVENT_GAMEPAD_BUTTON_UP:
        handled = translate_gamepad_button(in.gbutton, mapped);
        break;
    case SDL_EVENT_GAMEPAD_ADDED:
        handled = translate_gamepad_device(true, in.gdevice, mapped);
        break;
    case SDL_EVENT_GAMEPAD_REMOVED:
        handled = translate_gamepad_device(false, in.gdevice, mapped);
        break;
    case SDL_EVENT_JOYSTICK_AXIS_MOTION:
        handled = translate_joy_axis(in.jaxis, mapped);
        break;
    case SDL_EVENT_JOYSTICK_BUTTON_DOWN:
    case SDL_EVENT_JOYSTICK_BUTTON_UP:
        handled = translate_joy_button(in.jbutton, mapped);
        break;
    case SDL_EVENT_JOYSTICK_ADDED:
        handled = translate_joy_device(true, in.jdevice, mapped);
        break;
    case SDL_EVENT_JOYSTICK_REMOVED:
        handled = translate_joy_device(false, in.jdevice, mapped);
        break;
    default:
        handled = false;
        break;
    }

    if (!handled) {
        return false;
    }

    out = mapped;

    return true;
}

}
