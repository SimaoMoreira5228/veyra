#ifndef VEYRA_PLATFORM_SDL3_WINDOW_HPP
#define VEYRA_PLATFORM_SDL3_WINDOW_HPP

#include "veyra/render/software_renderer.hpp"

#include <SDL3/SDL.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>

namespace veyra {

enum class SdlWindowResult : std::uint8_t {
    ready,
    suspended,
    invalid_input,
    initialization_failed,
    creation_failed,
    unsupported_size,
    invalid_state,
    text_input_failed,
    presentation_failed,
};

class Sdl3Window {
  public:
    Sdl3Window(const char* title, int width, int height) noexcept;
    ~Sdl3Window();

    Sdl3Window(const Sdl3Window&) = delete;
    Sdl3Window& operator=(const Sdl3Window&) = delete;
    Sdl3Window(Sdl3Window&&) = delete;
    Sdl3Window& operator=(Sdl3Window&&) = delete;

    SdlWindowResult creation_result() const noexcept;
    SdlWindowResult acquire_surface(SoftwareSurface& surface) noexcept;
    SdlWindowResult present() noexcept;
    SdlWindowResult start_text_input() noexcept;
    SdlWindowResult stop_text_input() noexcept;
    SdlWindowResult set_text_input_area(const SDL_Rect& area, int cursor) noexcept;
    SdlWindowResult clear_text_input_area() noexcept;
    bool poll_event(SDL_Event& event) noexcept;
    SDL_Window* native_window() const noexcept;

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

    bool video_initialized_{};
    SdlWindowResult creation_result_{SdlWindowResult::invalid_state};
    Window window_;
    Renderer renderer_;
    Texture texture_;
    std::span<SoftwarePixel> locked_pixels_;
    int surface_width_{};
    int surface_height_{};
    bool frame_acquired_{};
};

}

#endif
