#include "veyra/platform/sdl3_window.hpp"

#include "veyra/platform/sdl3_translate.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <limits>
#include <utility>

namespace veyra {
namespace {
Sdl3Window* windows{};

bool is_gamepad_event(std::uint32_t type) noexcept {
    switch (type) {
    case SDL_EVENT_GAMEPAD_AXIS_MOTION:
    case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
    case SDL_EVENT_GAMEPAD_BUTTON_UP:
    case SDL_EVENT_GAMEPAD_ADDED:
    case SDL_EVENT_GAMEPAD_REMOVED:
    case SDL_EVENT_JOYSTICK_AXIS_MOTION:
    case SDL_EVENT_JOYSTICK_BUTTON_DOWN:
    case SDL_EVENT_JOYSTICK_BUTTON_UP:
    case SDL_EVENT_JOYSTICK_ADDED:
    case SDL_EVENT_JOYSTICK_REMOVED:
        return true;
    default:
        return false;
    }
}
}

Sdl3Window* Sdl3Window::find_window(WindowId id) noexcept {
    for (auto* window = windows; window != nullptr; window = window->next_) {
        if (window->window_id_ == id) {
            return window;
        }
    }
    return nullptr;
}

void Sdl3Window::route_event(const SDL_Event& event) noexcept {
    if (event.type == SDL_EVENT_QUIT) {
        for (auto* window = windows; window != nullptr; window = window->next_) {
            WindowEvent close{};
            close.kind = WindowEventKind::close_request;
            close.window = window->window_id();
            window->enqueue(close);
        }
        return;
    }

    if (is_gamepad_event(event.type)) {
        WindowEvent translated{};
        if (translate_sdl_event(event, kInvalidWindowId, translated)) {
            for (auto* window = windows; window != nullptr; window = window->next_) {
                window->enqueue(translated);
            }
        }
        return;
    }

    SDL_Window* native_window = SDL_GetWindowFromEvent(&event);
    if (native_window != nullptr) {
        const auto id = static_cast<WindowId>(SDL_GetWindowID(native_window));
        auto* window = find_window(id);
        if (window != nullptr) {
            if (event.type == SDL_EVENT_WINDOW_RESIZED ||
                event.type == SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED ||
                event.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED) {
                window->refresh_geometry();
            }

            WindowEvent translated{};
            if (event.type == SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED) {
                translated.kind = WindowEventKind::scale_change;
                translated.window = id;
                translated.data.scale = {.x = window->scale_x_, .y = window->scale_y_};
                window->enqueue(translated);
            } else if (translate_sdl_event(event, id, translated)) {
                window->enqueue(translated);
            }
        }
    }

    if ((event.type == SDL_EVENT_DROP_FILE || event.type == SDL_EVENT_DROP_TEXT) &&
        event.drop.data != nullptr) {
        SDL_free(const_cast<char*>(event.drop.data));
    }
}

Sdl3Window::Sdl3Window(const char* title, int width, int height) noexcept {
    if (title == nullptr || width <= 0 || height <= 0) {
        creation_result_ = WindowResult::invalid_input;
        return;
    }

    video_initialized_ = SDL_InitSubSystem(SDL_INIT_VIDEO);
    if (!video_initialized_) {
        creation_result_ = WindowResult::creation_failed;
        return;
    }

    gamepad_initialized_ = SDL_InitSubSystem(SDL_INIT_GAMEPAD);

    SDL_Window* native_window = nullptr;
    SDL_Renderer* native_renderer = nullptr;
    const auto flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;
    if (!SDL_CreateWindowAndRenderer(
            title,
            width,
            height,
            flags,
            &native_window,
            &native_renderer
        )) {
        if (native_renderer != nullptr) {
            SDL_DestroyRenderer(native_renderer);
        }
        if (native_window != nullptr) {
            SDL_DestroyWindow(native_window);
        }
        creation_result_ = WindowResult::creation_failed;
        return;
    }

    window_.reset(native_window);
    renderer_.reset(native_renderer);
    window_id_ = static_cast<WindowId>(SDL_GetWindowID(window_.get()));
    if (window_id_ == kInvalidWindowId) {
        creation_result_ = WindowResult::creation_failed;
        return;
    }

    next_ = windows;
    windows = this;
    creation_result_ = WindowResult::ready;
    refresh_geometry();
    surface_generation_ = 1;
}

Sdl3Window::~Sdl3Window() {
    if (window_ != nullptr) {
        if (windows == this) {
            windows = next_;
        } else {
            for (auto* window = windows; window != nullptr; window = window->next_) {
                if (window->next_ == this) {
                    window->next_ = next_;
                    break;
                }
            }
        }
    }
    texture_.reset();
    renderer_.reset();
    window_.reset();
    if (gamepad_initialized_) {
        SDL_QuitSubSystem(SDL_INIT_GAMEPAD);
    }
    if (video_initialized_) {
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
    }
}

WindowResult Sdl3Window::creation_result() const noexcept {
    return creation_result_;
}

WindowId Sdl3Window::window_id() const noexcept {
    return window_id_;
}

float Sdl3Window::logical_width() const noexcept {
    return logical_width_;
}

float Sdl3Window::logical_height() const noexcept {
    return logical_height_;
}

float Sdl3Window::scale_x() const noexcept {
    return scale_x_;
}

float Sdl3Window::scale_y() const noexcept {
    return scale_y_;
}

std::uint64_t Sdl3Window::surface_generation() const noexcept {
    return surface_generation_;
}

WindowCapabilities Sdl3Window::capabilities() const noexcept {
    const bool ready = creation_result_ == WindowResult::ready;

    std::uint32_t texture_formats = 0;

    if (ready && texture_ != nullptr) {
        texture_formats |= texture_format_flag(WindowTextureFormat::rgba8_unorm);
    }

    return {
        .clipboard = ready,
        .text_input = ready,
        .text_preedit = ready,
        .text_input_area = ready,
        .texture_formats = texture_formats,
    };
}

std::uint64_t Sdl3Window::dropped_events() const noexcept {
    return dropped_events_;
}

std::uint32_t Sdl3Window::gamepad_count() const noexcept {
    if (creation_result_ != WindowResult::ready || !gamepad_initialized_) {
        return 0;
    }

    int count = 0;
    SDL_JoystickID* ids = SDL_GetGamepads(&count);
    if (ids != nullptr) {
        SDL_free(ids);
    }
    if (count <= 0) {
        return 0;
    }

    return static_cast<std::uint32_t>(count);
}

bool Sdl3Window::poll_event(WindowEvent& event) noexcept {
    if (creation_result_ != WindowResult::ready) {
        return false;
    }
    if (dequeue(event)) {
        return true;
    }

    SDL_Event native_event{};
    while (SDL_PollEvent(&native_event)) {
        route_event(native_event);
    }
    return dequeue(event);
}

WindowResult
Sdl3Window::acquire_surface(SoftwareSurface& surface, std::span<SoftwarePixel> backing) noexcept {
    if (frame_acquired_) {
        return WindowResult::invalid_state;
    }
    surface = {};
    if (creation_result_ != WindowResult::ready) {
        return creation_result_;
    }

    refresh_geometry();
    int width = 0;
    int height = 0;
    if (!SDL_GetCurrentRenderOutputSize(renderer_.get(), &width, &height)) {
        return WindowResult::presentation_failed;
    }
    if (width <= 0 || height <= 0) {
        return WindowResult::suspended;
    }

    const auto pixel_width = static_cast<std::size_t>(width);
    const auto pixel_height = static_cast<std::size_t>(height);
    if (pixel_width >
            static_cast<std::size_t>(std::numeric_limits<int>::max()) / sizeof(SoftwarePixel) ||
        pixel_height > std::numeric_limits<std::size_t>::max() / pixel_width) {
        return WindowResult::unsupported_size;
    }
    const auto pixel_count = pixel_width * pixel_height;
    if (backing.size() < pixel_count) {
        return WindowResult::insufficient_capacity;
    }

    if (width != surface_width_ || height != surface_height_) {
        Texture next_texture(SDL_CreateTexture(
            renderer_.get(),
            SDL_PIXELFORMAT_RGBA32,
            SDL_TEXTUREACCESS_STREAMING,
            width,
            height
        ));
        if (!next_texture || !SDL_SetTextureBlendMode(next_texture.get(), SDL_BLENDMODE_NONE)) {
            return WindowResult::presentation_failed;
        }
        texture_ = std::move(next_texture);
        surface_width_ = width;
        surface_height_ = height;
    }

    surface = {
        .width = pixel_width,
        .height = pixel_height,
        .stride = pixel_width,
        .pixels = backing.first(pixel_count),
    };
    acquired_pixels_ = surface.pixels;
    frame_acquired_ = true;
    return WindowResult::ready;
}

WindowResult Sdl3Window::present() noexcept {
    if (!frame_acquired_) {
        return WindowResult::invalid_state;
    }
    frame_acquired_ = false;

    if (!SDL_UpdateTexture(
            texture_.get(),
            nullptr,
            acquired_pixels_.data(),
            surface_width_ * static_cast<int>(sizeof(SoftwarePixel))
        ) ||
        !SDL_RenderTexture(renderer_.get(), texture_.get(), nullptr, nullptr) ||
        !SDL_RenderPresent(renderer_.get())) {
        acquired_pixels_ = {};
        return WindowResult::presentation_failed;
    }
    acquired_pixels_ = {};
    return WindowResult::ready;
}

WindowResult Sdl3Window::start_text_input() noexcept {
    if (creation_result_ != WindowResult::ready) {
        return creation_result_;
    }
    return SDL_StartTextInput(window_.get()) ? WindowResult::ready
                                             : WindowResult::text_input_failed;
}

WindowResult Sdl3Window::stop_text_input() noexcept {
    if (creation_result_ != WindowResult::ready) {
        return creation_result_;
    }
    return SDL_StopTextInput(window_.get()) ? WindowResult::ready : WindowResult::text_input_failed;
}

WindowResult Sdl3Window::set_text_input_area(TextInputArea area) noexcept {
    if (area.width <= 0 || area.height <= 0 || area.cursor < 0 ||
        static_cast<std::int64_t>(area.x) + area.width > std::numeric_limits<int>::max() ||
        static_cast<std::int64_t>(area.y) + area.height > std::numeric_limits<int>::max() ||
        static_cast<std::int64_t>(area.x) + area.cursor > std::numeric_limits<int>::max()) {
        return WindowResult::invalid_input;
    }
    if (creation_result_ != WindowResult::ready) {
        return creation_result_;
    }
    const SDL_Rect rect{.x = area.x, .y = area.y, .w = area.width, .h = area.height};
    return SDL_SetTextInputArea(window_.get(), &rect, area.cursor)
        ? WindowResult::ready
        : WindowResult::text_input_failed;
}

WindowResult Sdl3Window::clear_text_input_area() noexcept {
    if (creation_result_ != WindowResult::ready) {
        return creation_result_;
    }
    return SDL_SetTextInputArea(window_.get(), nullptr, 0) ? WindowResult::ready
                                                           : WindowResult::text_input_failed;
}

WindowResult Sdl3Window::set_clipboard_text(const char* text) noexcept {
    if (text == nullptr) {
        return WindowResult::invalid_input;
    }
    if (creation_result_ != WindowResult::ready) {
        return creation_result_;
    }
    return SDL_SetClipboardText(text) ? WindowResult::ready : WindowResult::clipboard_failed;
}

WindowResult Sdl3Window::get_clipboard_text(std::span<char> output, std::size_t& length) noexcept {
    if (creation_result_ != WindowResult::ready) {
        return creation_result_;
    }
    std::unique_ptr<char, decltype(&SDL_free)> text(SDL_GetClipboardText(), SDL_free);
    if (!text) {
        return WindowResult::clipboard_failed;
    }
    const auto text_length = std::strlen(text.get());
    length = text_length;
    if (text_length > output.size()) {
        return WindowResult::insufficient_capacity;
    }
    std::copy_n(text.get(), text_length, output.begin());
    return WindowResult::ready;
}

void Sdl3Window::enqueue(WindowEvent event) noexcept {
    if (event_count_ == kEventCapacity) {
        ++dropped_events_;
        return;
    }
    event.window = window_id_;
    events_[(event_head_ + event_count_) % kEventCapacity] = event;
    ++event_count_;
}

bool Sdl3Window::dequeue(WindowEvent& event) noexcept {
    if (event_count_ == 0) {
        return false;
    }
    event = events_[event_head_];
    event_head_ = (event_head_ + 1) % kEventCapacity;
    --event_count_;
    return true;
}

void Sdl3Window::refresh_geometry() noexcept {
    int width = 0;
    int height = 0;
    if (SDL_GetWindowSize(window_.get(), &width, &height)) {
        logical_width_ = static_cast<float>(width);
        logical_height_ = static_cast<float>(height);
    }
    const float scale = SDL_GetWindowDisplayScale(window_.get());
    if (scale > 0.0F) {
        scale_x_ = scale;
        scale_y_ = scale;
    }
    int pixel_width = 0;
    int pixel_height = 0;
    if (SDL_GetWindowSizeInPixels(window_.get(), &pixel_width, &pixel_height) &&
        (pixel_width != pixel_width_ || pixel_height != pixel_height_)) {
        pixel_width_ = pixel_width;
        pixel_height_ = pixel_height;
        if (surface_generation_ != 0) {
            ++surface_generation_;
        }
    }
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
