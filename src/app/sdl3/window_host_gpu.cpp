#include "veyra/app/sdl3/window_host.hpp"

#if defined(VEYRA_APP_WITH_SDL_GPU)

namespace veyra::app::sdl3 {

bool Sdl3WindowHost::enable_gpu(std::size_t index) noexcept {
    auto& gpu = gpu_[index];
    gpu.emplace();

    auto& backend = backends_[index];
    if (!backend.has_value() || gpu->presenter.create() != WindowResult::ready ||
        gpu->presenter.claim(*backend) != WindowResult::ready ||
        create_sdl_gpu_renderer(
            gpu->presenter,
            backend->pixel_width(),
            backend->pixel_height(),
            gpu->renderer
        ) != WindowResult::ready) {
        gpu.reset();
        return false;
    }

    gpu->ready = true;
    return true;
}

bool Sdl3WindowHost::gpu_present_locked(
    std::size_t index,
    std::span<SoftwarePixel> backing,
    std::span<const DisplayCommand> commands,
    std::uint32_t width,
    std::uint32_t height
) noexcept {
    auto& gpu_optional = gpu_[index];
    if (!gpu_optional.has_value() || !gpu_optional->ready) {
        return false;
    }
    auto& backend_optional = backends_[index];
    if (!backend_optional.has_value()) {
        gpu_optional.reset();
        return false;
    }
    auto* backend = &*backend_optional;

    {
        auto& gpu = *gpu_optional;
        if ((gpu.renderer.width != width || gpu.renderer.height != height) &&
            resize_sdl_gpu_renderer(gpu.renderer, width, height) != WindowResult::ready) {
            gpu_optional.reset();
            gpu_fallback_ = true;
            backend->recreate_renderer();
            return false;
        }
    }

    auto& gpu = *gpu_optional;
    auto stats = SdlGpuRenderStats{};
    const auto pixel_count = static_cast<std::size_t>(width) * height;
    if (render_sdl_gpu_rects(gpu.renderer, commands, backing.first(pixel_count), stats) ==
            WindowResult::ready &&
        present_sdl_gpu_frame(gpu.renderer) == WindowResult::ready) {
        return true;
    }

    gpu_optional.reset();
    gpu_fallback_ = true;
    backend->recreate_renderer();
    return false;
}

void Sdl3WindowHost::set_present_path(PresentPath path) noexcept {
    std::lock_guard lock(state_mutex_);
    want_gpu_ = path == PresentPath::gpu;
}

bool Sdl3WindowHost::gpu_active(WindowId window) const noexcept {
    std::lock_guard lock(state_mutex_);
    const auto index = slot_for(window);
    if (index == kCapacity) {
        return false;
    }
    const auto& gpu = gpu_[index];
    return gpu.has_value() && gpu->ready;
}

bool Sdl3WindowHost::gpu_fallback() const noexcept {
    std::lock_guard lock(state_mutex_);
    return gpu_fallback_;
}

}

#else

namespace veyra::app::sdl3 {

void Sdl3WindowHost::set_present_path(PresentPath path) noexcept {
    static_cast<void>(path);
}

bool Sdl3WindowHost::gpu_active(WindowId window) const noexcept {
    static_cast<void>(window);
    return false;
}

bool Sdl3WindowHost::gpu_fallback() const noexcept {
    return false;
}

}

#endif
