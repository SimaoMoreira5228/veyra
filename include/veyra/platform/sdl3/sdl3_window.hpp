#ifndef VEYRA_PLATFORM_SDL3_WINDOW_HPP
#define VEYRA_PLATFORM_SDL3_WINDOW_HPP

#include "veyra/platform/core/window.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>

struct SDL_Window;
struct SDL_Renderer;
struct SDL_Texture;
typedef union SDL_Event SDL_Event;

namespace veyra {

class Sdl3Window {
  public:
    static constexpr std::size_t kEventCapacity = 32;

    Sdl3Window(const char* title, int width, int height) noexcept;
    ~Sdl3Window();

    Sdl3Window(const Sdl3Window&) = delete;
    Sdl3Window& operator=(const Sdl3Window&) = delete;
    Sdl3Window(Sdl3Window&&) = delete;
    Sdl3Window& operator=(Sdl3Window&&) = delete;

    WindowResult creation_result() const noexcept;
    WindowResult show() noexcept;
    WindowResult set_state(WindowState state) noexcept;
    WindowState window_state() const noexcept;
    WindowId window_id() const noexcept;
    float logical_width() const noexcept;
    float logical_height() const noexcept;
    float scale_x() const noexcept;
    float scale_y() const noexcept;
    std::uint64_t surface_generation() const noexcept;
    WindowCapabilities capabilities() const noexcept;
    std::uint64_t dropped_events() const noexcept;

    bool poll_event(WindowEvent& event) noexcept;
    WindowResult
    acquire_surface(SoftwareSurface& surface, std::span<SoftwarePixel> backing) noexcept;
    WindowResult present() noexcept;
    WindowResult start_text_input() noexcept;
    WindowResult stop_text_input() noexcept;
    WindowResult set_text_input_area(TextInputArea area) noexcept;
    WindowResult clear_text_input_area() noexcept;
    WindowResult set_clipboard_text(const char* text) noexcept;
    WindowResult get_clipboard_text(std::span<char> output, std::size_t& length) noexcept;

    WindowResult claim_for_gpu_device(void* device) noexcept;
    void release_from_gpu_device(void* device) noexcept;
    void* window_handle() noexcept;
    std::uint32_t gamepad_count() const noexcept;

  private:
    struct WindowDeleter {
        void operator()(SDL_Window* window) const noexcept;
    };

    struct RendererDeleter {
        void operator()(SDL_Renderer* renderer) const noexcept;
    };

    struct TextureDeleter {
        void operator()(SDL_Texture* texture) const noexcept;
    };

    using Window = std::unique_ptr<SDL_Window, WindowDeleter>;
    using Renderer = std::unique_ptr<SDL_Renderer, RendererDeleter>;
    using Texture = std::unique_ptr<SDL_Texture, TextureDeleter>;

    void enqueue(WindowEvent event) noexcept;
    bool dequeue(WindowEvent& event) noexcept;
    void refresh_geometry() noexcept;
    static Sdl3Window* find_window(WindowId id) noexcept;
    static void route_event(const SDL_Event& event) noexcept;

    WindowResult creation_result_{WindowResult::invalid_state};
    WindowId window_id_{kInvalidWindowId};
    Window window_;
    Renderer renderer_;
    Texture texture_;
    std::span<SoftwarePixel> acquired_pixels_;
    Sdl3Window* next_{};
    float logical_width_{};
    float logical_height_{};
    float scale_x_{1.0F};
    float scale_y_{1.0F};
    int surface_width_{};
    int surface_height_{};
    int pixel_width_{};
    int pixel_height_{};
    std::uint64_t surface_generation_{};
    std::uint64_t dropped_events_{};
    std::array<WindowEvent, kEventCapacity> events_{};
    std::size_t event_head_{};
    std::size_t event_count_{};
    bool video_initialized_{};
    bool gamepad_initialized_{};
    bool frame_acquired_{};
};

static_assert(WindowBackend<Sdl3Window>);
static_assert(SoftwareSurfacePresenter<Sdl3Window>);

}

namespace veyra::platform {
using ::veyra::Sdl3Window;
}

namespace veyra::platform::sdl3 {
using ::veyra::Sdl3Window;
}

#endif
