#include "veyra/platform/sdl3_window.hpp"

#include <algorithm>
#include <chrono>
#include <limits>
#include <memory>
#include <type_traits>
#include <utility>

namespace veyra {
namespace {

static_assert(sizeof(SoftwarePixel) == 4);
static_assert(std::is_trivially_copyable_v<SoftwarePixel>);
static_assert(std::is_trivially_destructible_v<SoftwarePixel>);

}

Sdl3Window::Sdl3Window(const char* title, int width, int height) noexcept {
    if (title == nullptr || width <= 0 || height <= 0) {
        creation_result_ = SdlWindowResult::invalid_input;
        return;
    }

    video_initialized_ = SDL_InitSubSystem(SDL_INIT_VIDEO);
    if (!video_initialized_) {
        creation_result_ = SdlWindowResult::initialization_failed;
        return;
    }

    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
    const auto flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;
    if (!SDL_CreateWindowAndRenderer(title, width, height, flags, &window, &renderer)) {
        if (renderer != nullptr) {
            SDL_DestroyRenderer(renderer);
        }
        if (window != nullptr) {
            SDL_DestroyWindow(window);
        }
        creation_result_ = SdlWindowResult::creation_failed;
        return;
    }

    window_.reset(window);
    renderer_.reset(renderer);
    creation_result_ = SdlWindowResult::ready;
}

Sdl3Window::~Sdl3Window() {
    if (frame_acquired_) {
        std::destroy_n(locked_pixels_.begin(), locked_pixels_.size());
        SDL_UnlockTexture(texture_.get());
    }
    texture_.reset();
    renderer_.reset();
    window_.reset();
    if (video_initialized_) {
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
    }
}

SdlWindowResult Sdl3Window::creation_result() const noexcept {
    return creation_result_;
}

SdlWindowResult Sdl3Window::acquire_surface(SoftwareSurface& surface) noexcept {
    if (frame_acquired_) {
        return SdlWindowResult::invalid_state;
    }
    surface = {};
    if (creation_result_ != SdlWindowResult::ready) {
        return creation_result_;
    }

    int width = 0;
    int height = 0;
    if (!SDL_GetCurrentRenderOutputSize(renderer_.get(), &width, &height)) {
        return SdlWindowResult::presentation_failed;
    }
    if (width <= 0 || height <= 0) {
        return SdlWindowResult::suspended;
    }

    const auto pixel_width = static_cast<std::size_t>(width);
    const auto pixel_height = static_cast<std::size_t>(height);
    const auto maximum_pitch = static_cast<std::size_t>(std::numeric_limits<int>::max());
    if (pixel_width > maximum_pitch / sizeof(SoftwarePixel) ||
        pixel_height > std::numeric_limits<std::size_t>::max() / pixel_width) {
        return SdlWindowResult::unsupported_size;
    }

    if (width != surface_width_ || height != surface_height_) {
        auto* raw_texture = SDL_CreateTexture(
            renderer_.get(),
            SDL_PIXELFORMAT_RGBA32,
            SDL_TEXTUREACCESS_STREAMING,
            width,
            height
        );
        Texture next_texture(raw_texture);
        if (!next_texture) {
            return SdlWindowResult::presentation_failed;
        }
        if (!SDL_SetTextureBlendMode(next_texture.get(), SDL_BLENDMODE_NONE)) {
            return SdlWindowResult::presentation_failed;
        }

        texture_ = std::move(next_texture);
        surface_width_ = width;
        surface_height_ = height;
    }

    void* locked_memory = nullptr;
    int pitch = 0;
    if (!SDL_LockTexture(texture_.get(), nullptr, &locked_memory, &pitch)) {
        return SdlWindowResult::presentation_failed;
    }

    if (locked_memory == nullptr || pitch <= 0 ||
        pitch % static_cast<int>(sizeof(SoftwarePixel)) != 0) {
        SDL_UnlockTexture(texture_.get());
        return SdlWindowResult::unsupported_size;
    }

    const auto stride = static_cast<std::size_t>(pitch) / sizeof(SoftwarePixel);
    if (stride < pixel_width || pixel_height > std::numeric_limits<std::size_t>::max() / stride) {
        SDL_UnlockTexture(texture_.get());
        return SdlWindowResult::unsupported_size;
    }

    const auto pixel_count = stride * pixel_height;
    auto* pixels = std::start_lifetime_as_array<SoftwarePixel>(locked_memory, pixel_count);
    locked_pixels_ = {pixels, pixel_count};
    surface = {
        .width = pixel_width,
        .height = pixel_height,
        .stride = stride,
        .pixels = locked_pixels_,
    };
    frame_acquired_ = true;
    return SdlWindowResult::ready;
}

SdlWindowResult Sdl3Window::present() noexcept {
    if (!frame_acquired_) {
        return SdlWindowResult::invalid_state;
    }

    std::destroy_n(locked_pixels_.begin(), locked_pixels_.size());
    SDL_UnlockTexture(texture_.get());
    locked_pixels_ = {};
    frame_acquired_ = false;

    if (!SDL_RenderTexture(renderer_.get(), texture_.get(), nullptr, nullptr) ||
        !SDL_RenderPresent(renderer_.get())) {
        return SdlWindowResult::presentation_failed;
    }

    return SdlWindowResult::ready;
}

SdlWindowResult Sdl3Window::start_text_input() noexcept {
    if (creation_result_ != SdlWindowResult::ready) {
        return creation_result_;
    }
    return SDL_StartTextInput(window_.get()) ? SdlWindowResult::ready
                                             : SdlWindowResult::text_input_failed;
}

SdlWindowResult Sdl3Window::stop_text_input() noexcept {
    if (creation_result_ != SdlWindowResult::ready) {
        return creation_result_;
    }
    return SDL_StopTextInput(window_.get()) ? SdlWindowResult::ready
                                            : SdlWindowResult::text_input_failed;
}

SdlWindowResult Sdl3Window::set_text_input_area(const SDL_Rect& area, int cursor) noexcept {
    if (area.w <= 0 || area.h <= 0 || cursor < 0 ||
        static_cast<std::int64_t>(area.x) + area.w > std::numeric_limits<int>::max() ||
        static_cast<std::int64_t>(area.y) + area.h > std::numeric_limits<int>::max() ||
        static_cast<std::int64_t>(area.x) + cursor > std::numeric_limits<int>::max()) {
        return SdlWindowResult::invalid_input;
    }
    if (creation_result_ != SdlWindowResult::ready) {
        return creation_result_;
    }
    return SDL_SetTextInputArea(window_.get(), &area, cursor) ? SdlWindowResult::ready
                                                              : SdlWindowResult::text_input_failed;
}

SdlWindowResult Sdl3Window::clear_text_input_area() noexcept {
    if (creation_result_ != SdlWindowResult::ready) {
        return creation_result_;
    }
    return SDL_SetTextInputArea(window_.get(), nullptr, 0) ? SdlWindowResult::ready
                                                           : SdlWindowResult::text_input_failed;
}

bool Sdl3Window::poll_event(SDL_Event& event) noexcept {
    if (creation_result_ != SdlWindowResult::ready) {
        return false;
    }
    return SDL_PollEvent(&event);
}

bool Sdl3Window::wait_event(SDL_Event& event) noexcept {
    if (creation_result_ != SdlWindowResult::ready) {
        return false;
    }

    return SDL_WaitEvent(&event);
}

bool Sdl3Window::wait_event_until(SDL_Event& event, FrameScheduler::Deadline deadline) noexcept {
    if (creation_result_ != SdlWindowResult::ready) {
        return false;
    }

    while (true) {
        const auto now = FrameScheduler::Clock::now();
        if (deadline <= now) {
            return SDL_PollEvent(&event);
        }

        const auto remaining = std::chrono::ceil<std::chrono::milliseconds>(deadline - now);
        const auto maximum_timeout = std::chrono::milliseconds{std::numeric_limits<Sint32>::max()};
        const auto timeout = static_cast<Sint32>(std::min(remaining, maximum_timeout).count());
        if (SDL_WaitEventTimeout(&event, timeout)) {
            return true;
        }
    }
}

SDL_Window* Sdl3Window::native_window() const noexcept {
    return window_.get();
}

void Sdl3Window::WindowDeleter::operator()(SDL_Window* window) const noexcept {
    SDL_DestroyWindow(window);
}

void Sdl3Window::RendererDeleter::operator()(SDL_Renderer* renderer) const noexcept {
    SDL_DestroyRenderer(renderer);
}

void Sdl3Window::TextureDeleter::operator()(SDL_Texture* texture) const noexcept {
    SDL_DestroyTexture(texture);
}

}
