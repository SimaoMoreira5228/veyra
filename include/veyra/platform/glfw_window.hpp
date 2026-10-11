#ifndef VEYRA_PLATFORM_GLFW_WINDOW_HPP
#define VEYRA_PLATFORM_GLFW_WINDOW_HPP

#include "veyra/platform/window.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>

struct GLFWwindow;

namespace veyra {

class GlfwWindow {
  public:
    static constexpr std::size_t kEventCapacity = 32;

    GlfwWindow(WindowId id, const char* title, int width, int height, bool visible = true) noexcept;
    ~GlfwWindow();

    GlfwWindow(const GlfwWindow&) = delete;
    GlfwWindow& operator=(const GlfwWindow&) = delete;
    GlfwWindow(GlfwWindow&&) = delete;
    GlfwWindow& operator=(GlfwWindow&&) = delete;

    WindowResult creation_result() const noexcept;
    WindowId window_id() const noexcept;
    float logical_width() const noexcept;
    float logical_height() const noexcept;
    float scale_x() const noexcept;
    float scale_y() const noexcept;
    std::uint64_t surface_generation() const noexcept;
    WindowCapabilities capabilities() const noexcept;
    std::uint64_t dropped_events() const noexcept;
    bool should_close() const noexcept;
    void cancel_close_request() noexcept;

    bool poll_event(WindowEvent& event) noexcept;
    WindowResult start_text_input() noexcept;
    WindowResult stop_text_input() noexcept;
    WindowResult set_text_input_area(TextInputArea area) noexcept;
    WindowResult clear_text_input_area() noexcept;
    WindowResult set_clipboard_text(const char* text) noexcept;
    WindowResult get_clipboard_text(std::span<char> output, std::size_t& length) noexcept;

  private:
    struct WindowDeleter {
        void operator()(GLFWwindow* window) const noexcept;
    };

    WindowResult creation_result_{WindowResult::invalid_state};
    WindowId window_id_{kInvalidWindowId};
    std::unique_ptr<GLFWwindow, WindowDeleter> window_;
    float logical_width_{};
    float logical_height_{};
    float scale_x_{1.0F};
    float scale_y_{1.0F};
    int framebuffer_width_{};
    int framebuffer_height_{};
    std::uint64_t surface_generation_{};
    std::uint64_t dropped_events_{};
    std::uint32_t buttons_{};
    std::uint32_t modifiers_{};
    std::array<WindowEvent, kEventCapacity> events_{};
    std::size_t event_head_{};
    std::size_t event_count_{};
    bool text_input_active_{};

    void enqueue(WindowEvent event) noexcept;
    bool dequeue(WindowEvent& event) noexcept;

    static GlfwWindow* self(GLFWwindow* window) noexcept;
    static std::uint32_t translate_modifiers(int modifiers) noexcept;
    static void on_close(GLFWwindow* window) noexcept;
    static void on_resize(GLFWwindow* window, int width, int height) noexcept;
    static void on_content_scale(GLFWwindow* window, float x, float y) noexcept;
    static void on_framebuffer_resize(GLFWwindow* window, int width, int height) noexcept;
    static void on_cursor(GLFWwindow* window, double x, double y) noexcept;
    static void on_mouse_button(GLFWwindow* window, int button, int action, int modifiers) noexcept;
    static void on_scroll(GLFWwindow* window, double x, double y) noexcept;
    static void
    on_key(GLFWwindow* window, int key, int scancode, int action, int modifiers) noexcept;
    static void on_character(GLFWwindow* window, unsigned int codepoint) noexcept;
    static void on_focus(GLFWwindow* window, int focused) noexcept;
};

static_assert(WindowBackend<GlfwWindow>);

}

#endif
