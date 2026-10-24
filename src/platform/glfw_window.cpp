#include "veyra/platform/glfw_window.hpp"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace veyra {
namespace {

std::size_t glfw_window_count{};
GlfwWindow* instances{};

static_assert(GLFW_GAMEPAD_AXIS_LAST + 1 == 6, "glfw gamepad axes fit the window cache");
static_assert(GLFW_GAMEPAD_BUTTON_LAST + 1 == 15, "glfw gamepad buttons fit the window cache");

std::uint32_t mouse_button_flag(int button) noexcept {
    switch (button) {
    case GLFW_MOUSE_BUTTON_LEFT:
        return button_flag(PointerButton::primary);
    case GLFW_MOUSE_BUTTON_MIDDLE:
        return button_flag(PointerButton::middle);
    case GLFW_MOUSE_BUTTON_RIGHT:
        return button_flag(PointerButton::secondary);
    case GLFW_MOUSE_BUTTON_4:
        return button_flag(PointerButton::back);
    case GLFW_MOUSE_BUTTON_5:
        return button_flag(PointerButton::forward);
    default:
        return 0;
    }
}

bool valid_codepoint(unsigned int codepoint) noexcept {
    return codepoint <= 0x10FFFFU && !(codepoint >= 0xD800U && codepoint <= 0xDFFFU);
}

WindowTextCommit encode_codepoint(unsigned int codepoint) noexcept {
    WindowTextCommit commit{};
    if (!valid_codepoint(codepoint)) {
        return commit;
    }

    if (codepoint <= 0x7FU) {
        commit.text[0] = static_cast<char>(codepoint);
        commit.length = 1;
    } else if (codepoint <= 0x7FFU) {
        commit.text[0] = static_cast<char>(0xC0U | (codepoint >> 6U));
        commit.text[1] = static_cast<char>(0x80U | (codepoint & 0x3FU));
        commit.length = 2;
    } else if (codepoint <= 0xFFFFU) {
        commit.text[0] = static_cast<char>(0xE0U | (codepoint >> 12U));
        commit.text[1] = static_cast<char>(0x80U | ((codepoint >> 6U) & 0x3FU));
        commit.text[2] = static_cast<char>(0x80U | (codepoint & 0x3FU));
        commit.length = 3;
    } else {
        commit.text[0] = static_cast<char>(0xF0U | (codepoint >> 18U));
        commit.text[1] = static_cast<char>(0x80U | ((codepoint >> 12U) & 0x3FU));
        commit.text[2] = static_cast<char>(0x80U | ((codepoint >> 6U) & 0x3FU));
        commit.text[3] = static_cast<char>(0x80U | (codepoint & 0x3FU));
        commit.length = 4;
    }

    return commit;
}

}

GlfwWindow::GlfwWindow(
    WindowId id,
    const char* title,
    int width,
    int height,
    bool visible
) noexcept {
    if (id == kInvalidWindowId || title == nullptr || width <= 0 || height <= 0) {
        creation_result_ = WindowResult::invalid_input;
        return;
    }

    if (glfw_window_count == 0 && glfwInit() != GLFW_TRUE) {
        creation_result_ = WindowResult::creation_failed;
        return;
    }

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_VISIBLE, visible ? GLFW_TRUE : GLFW_FALSE);
    window_.reset(glfwCreateWindow(width, height, title, nullptr, nullptr));
    if (window_ == nullptr) {
        if (glfw_window_count == 0) {
            glfwTerminate();
        }
        creation_result_ = WindowResult::creation_failed;
        return;
    }

    ++glfw_window_count;
    window_id_ = id;
    creation_result_ = WindowResult::ready;
    if (instances == nullptr) {
        glfwSetJoystickCallback(on_joystick);
    }
    next_ = instances;
    instances = this;
    glfwSetWindowUserPointer(window_.get(), this);
    glfwSetWindowCloseCallback(window_.get(), on_close);
    glfwSetWindowSizeCallback(window_.get(), on_resize);
    glfwSetWindowContentScaleCallback(window_.get(), on_content_scale);
    glfwSetFramebufferSizeCallback(window_.get(), on_framebuffer_resize);
    glfwSetCursorPosCallback(window_.get(), on_cursor);
    glfwSetMouseButtonCallback(window_.get(), on_mouse_button);
    glfwSetScrollCallback(window_.get(), on_scroll);
    glfwSetKeyCallback(window_.get(), on_key);
    glfwSetCharCallback(window_.get(), on_character);
    glfwSetWindowFocusCallback(window_.get(), on_focus);

    int logical_width = 0;
    int logical_height = 0;
    glfwGetWindowSize(window_.get(), &logical_width, &logical_height);
    logical_width_ = static_cast<float>(logical_width);
    logical_height_ = static_cast<float>(logical_height);
    glfwGetWindowContentScale(window_.get(), &scale_x_, &scale_y_);
    glfwGetFramebufferSize(window_.get(), &framebuffer_width_, &framebuffer_height_);
    surface_generation_ = 1;
}

GlfwWindow::~GlfwWindow() {
    if (window_ == nullptr) {
        return;
    }

    if (instances == this) {
        instances = next_;
    } else {
        for (auto* instance = instances; instance != nullptr; instance = instance->next_) {
            if (instance->next_ == this) {
                instance->next_ = next_;
                break;
            }
        }
    }
    if (instances == nullptr) {
        glfwSetJoystickCallback(nullptr);
    }
    next_ = nullptr;
    window_.reset();
    --glfw_window_count;
    if (glfw_window_count == 0) {
        glfwTerminate();
    }
}

WindowResult GlfwWindow::creation_result() const noexcept {
    return creation_result_;
}

WindowId GlfwWindow::window_id() const noexcept {
    return window_id_;
}

float GlfwWindow::logical_width() const noexcept {
    return logical_width_;
}

float GlfwWindow::logical_height() const noexcept {
    return logical_height_;
}

float GlfwWindow::scale_x() const noexcept {
    return scale_x_;
}

float GlfwWindow::scale_y() const noexcept {
    return scale_y_;
}

std::uint64_t GlfwWindow::surface_generation() const noexcept {
    return surface_generation_;
}

WindowCapabilities GlfwWindow::capabilities() const noexcept {
    if (creation_result_ != WindowResult::ready || glfwGetPlatform() == GLFW_PLATFORM_NULL) {
        return {};
    }

    return {
        .clipboard = true,
        .text_input = true,
        .text_preedit = false,
        .text_input_area = false,
    };
}

std::uint64_t GlfwWindow::dropped_events() const noexcept {
    return dropped_events_;
}

bool GlfwWindow::should_close() const noexcept {
    return window_ != nullptr && glfwWindowShouldClose(window_.get()) == GLFW_TRUE;
}

void GlfwWindow::cancel_close_request() noexcept {
    if (window_ != nullptr) {
        glfwSetWindowShouldClose(window_.get(), GLFW_FALSE);
    }
}

bool GlfwWindow::poll_event(WindowEvent& event) noexcept {
    if (creation_result_ != WindowResult::ready) {
        return false;
    }
    if (dequeue(event)) {
        return true;
    }

    glfwPollEvents();
    poll_gamepads();
    return dequeue(event);
}

WindowResult GlfwWindow::start_text_input() noexcept {
    if (creation_result_ != WindowResult::ready) {
        return creation_result_;
    }
    if (!capabilities().text_input) {
        return WindowResult::unsupported;
    }

    text_input_active_ = true;
    return WindowResult::ready;
}

WindowResult GlfwWindow::stop_text_input() noexcept {
    if (creation_result_ != WindowResult::ready) {
        return creation_result_;
    }
    if (!capabilities().text_input) {
        return WindowResult::unsupported;
    }

    text_input_active_ = false;
    return WindowResult::ready;
}

WindowResult GlfwWindow::set_text_input_area(TextInputArea area) noexcept {
    static_cast<void>(area);
    if (creation_result_ != WindowResult::ready) {
        return creation_result_;
    }

    return WindowResult::unsupported;
}

WindowResult GlfwWindow::clear_text_input_area() noexcept {
    if (creation_result_ != WindowResult::ready) {
        return creation_result_;
    }

    return WindowResult::unsupported;
}

WindowResult GlfwWindow::set_clipboard_text(const char* text) noexcept {
    if (text == nullptr) {
        return WindowResult::invalid_input;
    }
    if (creation_result_ != WindowResult::ready) {
        return creation_result_;
    }
    if (!capabilities().clipboard) {
        return WindowResult::unsupported;
    }

    glfwSetClipboardString(window_.get(), text);
    return WindowResult::ready;
}

WindowResult GlfwWindow::get_clipboard_text(std::span<char> output, std::size_t& length) noexcept {
    if (creation_result_ != WindowResult::ready) {
        return creation_result_;
    }
    if (!capabilities().clipboard) {
        return WindowResult::unsupported;
    }

    const auto* text = glfwGetClipboardString(window_.get());
    if (text == nullptr) {
        return WindowResult::clipboard_failed;
    }

    const auto text_length = std::strlen(text);
    length = text_length;
    if (text_length > output.size()) {
        return WindowResult::insufficient_capacity;
    }

    std::copy_n(text, text_length, output.begin());
    return WindowResult::ready;
}

void GlfwWindow::enqueue(WindowEvent event) noexcept {
    if (event_count_ == kEventCapacity) {
        ++dropped_events_;
        return;
    }

    event.window = window_id_;
    const auto index = (event_head_ + event_count_) % kEventCapacity;
    events_[index] = event;
    ++event_count_;
}

bool GlfwWindow::dequeue(WindowEvent& event) noexcept {
    if (event_count_ == 0) {
        return false;
    }

    event = events_[event_head_];
    event_head_ = (event_head_ + 1) % kEventCapacity;
    --event_count_;
    return true;
}

GlfwWindow* GlfwWindow::self(GLFWwindow* window) noexcept {
    return static_cast<GlfwWindow*>(glfwGetWindowUserPointer(window));
}

std::uint32_t GlfwWindow::translate_modifiers(int modifiers) noexcept {
    std::uint32_t translated = 0;
    if ((modifiers & GLFW_MOD_CONTROL) != 0) {
        translated |= modifier_flag(ShortcutModifier::ctrl);
    }
    if ((modifiers & GLFW_MOD_ALT) != 0) {
        translated |= modifier_flag(ShortcutModifier::alt);
    }
    if ((modifiers & GLFW_MOD_SHIFT) != 0) {
        translated |= modifier_flag(ShortcutModifier::shift);
    }
    if ((modifiers & GLFW_MOD_SUPER) != 0) {
        translated |= modifier_flag(ShortcutModifier::super);
    }
    return translated;
}

void GlfwWindow::on_close(GLFWwindow* window) noexcept {
    auto* instance = self(window);
    WindowEvent event{};
    event.kind = WindowEventKind::close_request;
    instance->enqueue(event);
}

void GlfwWindow::on_resize(GLFWwindow* window, int width, int height) noexcept {
    if (width <= 0 || height <= 0) {
        return;
    }

    auto* instance = self(window);
    instance->logical_width_ = static_cast<float>(width);
    instance->logical_height_ = static_cast<float>(height);
    WindowEvent event{};
    event.kind = WindowEventKind::resize;
    event.data.resize = {.width = instance->logical_width_, .height = instance->logical_height_};
    instance->enqueue(event);
}

void GlfwWindow::on_content_scale(GLFWwindow* window, float x, float y) noexcept {
    if (!std::isfinite(x) || !std::isfinite(y) || x <= 0.0F || y <= 0.0F) {
        return;
    }

    auto* instance = self(window);
    if (instance->scale_x_ == x && instance->scale_y_ == y) {
        return;
    }

    instance->scale_x_ = x;
    instance->scale_y_ = y;
    WindowEvent event{};
    event.kind = WindowEventKind::scale_change;
    event.data.scale = {.x = x, .y = y};
    instance->enqueue(event);
}

void GlfwWindow::on_framebuffer_resize(GLFWwindow* window, int width, int height) noexcept {
    auto* instance = self(window);
    if (instance->framebuffer_width_ == width && instance->framebuffer_height_ == height) {
        return;
    }

    instance->framebuffer_width_ = width;
    instance->framebuffer_height_ = height;
    ++instance->surface_generation_;
}

void GlfwWindow::on_cursor(GLFWwindow* window, double x, double y) noexcept {
    if (!std::isfinite(x) || !std::isfinite(y) || std::abs(x) > std::numeric_limits<float>::max() ||
        std::abs(y) > std::numeric_limits<float>::max()) {
        return;
    }

    auto* instance = self(window);
    WindowEvent event{};
    event.kind = WindowEventKind::pointer_move;
    event.data.pointer = {
        .position = {.x = static_cast<float>(x), .y = static_cast<float>(y)},
        .buttons = instance->buttons_,
        .modifiers = instance->modifiers_,
    };
    instance->enqueue(event);
}

void GlfwWindow::on_mouse_button(
    GLFWwindow* window,
    int button,
    int action,
    int modifiers
) noexcept {
    const auto flag = mouse_button_flag(button);
    if (flag == 0 || (action != GLFW_PRESS && action != GLFW_RELEASE)) {
        return;
    }

    auto* instance = self(window);
    double x = 0.0;
    double y = 0.0;
    glfwGetCursorPos(window, &x, &y);
    if (!std::isfinite(x) || !std::isfinite(y) || std::abs(x) > std::numeric_limits<float>::max() ||
        std::abs(y) > std::numeric_limits<float>::max()) {
        return;
    }

    const auto kind =
        action == GLFW_PRESS ? WindowEventKind::pointer_down : WindowEventKind::pointer_up;
    if (action == GLFW_PRESS) {
        instance->buttons_ |= flag;
    } else {
        instance->buttons_ &= ~flag;
    }
    instance->modifiers_ = translate_modifiers(modifiers);

    WindowEvent event{};
    event.kind = kind;
    event.data.pointer = {
        .position = {.x = static_cast<float>(x), .y = static_cast<float>(y)},
        .buttons = flag,
        .modifiers = instance->modifiers_,
    };
    instance->enqueue(event);
}

void GlfwWindow::on_scroll(GLFWwindow* window, double x, double y) noexcept {
    if (!std::isfinite(x) || !std::isfinite(y) || std::abs(x) > std::numeric_limits<float>::max() ||
        std::abs(y) > std::numeric_limits<float>::max()) {
        return;
    }

    auto* instance = self(window);
    WindowEvent event{};
    event.kind = WindowEventKind::wheel;
    event.data.wheel = {
        .delta_x = static_cast<float>(x),
        .delta_y = static_cast<float>(y),
        .modifiers = instance->modifiers_,
    };
    instance->enqueue(event);
}

void GlfwWindow::on_key(
    GLFWwindow* window,
    int key,
    int scancode,
    int action,
    int modifiers
) noexcept {
    static_cast<void>(scancode);
    if (key == GLFW_KEY_UNKNOWN ||
        (action != GLFW_PRESS && action != GLFW_RELEASE && action != GLFW_REPEAT)) {
        return;
    }

    auto* instance = self(window);
    instance->modifiers_ = translate_modifiers(modifiers);
    WindowEvent event{};
    event.kind = action == GLFW_RELEASE ? WindowEventKind::key_up : WindowEventKind::key_down;
    event.data.key = {
        .code = static_cast<std::uint32_t>(key),
        .modifiers = instance->modifiers_,
        .repeat = action == GLFW_REPEAT,
    };
    instance->enqueue(event);
}

void GlfwWindow::on_character(GLFWwindow* window, unsigned int codepoint) noexcept {
    auto* instance = self(window);
    if (!instance->text_input_active_) {
        return;
    }

    const auto commit = encode_codepoint(codepoint);
    if (commit.length == 0) {
        return;
    }

    WindowEvent event{};
    event.kind = WindowEventKind::text_commit;
    event.data.commit = commit;
    instance->enqueue(event);
}

void GlfwWindow::on_focus(GLFWwindow* window, int focused) noexcept {
    WindowEvent event{};
    event.kind = focused == GLFW_TRUE ? WindowEventKind::focus_gained : WindowEventKind::focus_lost;
    self(window)->enqueue(event);
}

void GlfwWindow::on_joystick(int id, int action) noexcept {
    if (id < GLFW_JOYSTICK_1 || id > GLFW_JOYSTICK_LAST) {
        return;
    }
    if (action != GLFW_CONNECTED && action != GLFW_DISCONNECTED) {
        return;
    }

    WindowEvent event{};
    event.kind = action == GLFW_CONNECTED ? WindowEventKind::gamepad_added
                                          : WindowEventKind::gamepad_removed;
    event.data.gamepad_device = {.device = static_cast<std::uint32_t>(id)};
    for (auto* instance = instances; instance != nullptr; instance = instance->next_) {
        instance->enqueue(event);
    }
}

void GlfwWindow::poll_gamepads() noexcept {
    std::uint16_t present = 0;

    for (int id = GLFW_JOYSTICK_1; id <= GLFW_JOYSTICK_LAST; ++id) {
        if (glfwJoystickPresent(id) != GLFW_TRUE || glfwJoystickIsGamepad(id) != GLFW_TRUE) {
            continue;
        }

        const auto bit = static_cast<std::uint16_t>(1U << id);
        present |= bit;

        GLFWgamepadstate state{};
        if (glfwGetGamepadState(id, &state) != GLFW_TRUE) {
            continue;
        }

        const auto device = static_cast<std::uint32_t>(id);
        if ((gamepads_ & bit) == 0) {
            for (std::size_t index = 0; index < kGamepadAxes; ++index) {
                gamepad_axes_[index] = state.axes[index];
            }
            for (std::size_t index = 0; index < kGamepadButtons; ++index) {
                gamepad_pressed_[index] = state.buttons[index] == GLFW_PRESS;
            }
            continue;
        }

        for (std::size_t index = 0; index < kGamepadAxes; ++index) {
            if (state.axes[index] == gamepad_axes_[index]) {
                continue;
            }
            gamepad_axes_[index] = state.axes[index];
            WindowEvent event{};
            event.kind = WindowEventKind::gamepad_axis;
            event.data.gamepad_axis = {
                .device = device,
                .axis = static_cast<std::uint32_t>(index),
                .value = state.axes[index],
            };
            enqueue(event);
        }
        for (std::size_t index = 0; index < kGamepadButtons; ++index) {
            const bool pressed = state.buttons[index] == GLFW_PRESS;
            if (pressed == gamepad_pressed_[index]) {
                continue;
            }
            gamepad_pressed_[index] = pressed;
            WindowEvent event{};
            event.kind = WindowEventKind::gamepad_button;
            event.data.gamepad_button = {
                .device = device,
                .button = static_cast<std::uint32_t>(index),
                .pressed = pressed,
            };
            enqueue(event);
        }
    }

    gamepads_ = present;
}

std::uint32_t GlfwWindow::gamepad_count() const noexcept {
    if (creation_result_ != WindowResult::ready) {
        return 0;
    }

    std::uint32_t count = 0;
    for (int id = GLFW_JOYSTICK_1; id <= GLFW_JOYSTICK_LAST; ++id) {
        if (glfwJoystickPresent(id) == GLFW_TRUE && glfwJoystickIsGamepad(id) == GLFW_TRUE) {
            ++count;
        }
    }

    return count;
}

void GlfwWindow::WindowDeleter::operator()(GLFWwindow* window) const noexcept {
    glfwDestroyWindow(window);
}

}
