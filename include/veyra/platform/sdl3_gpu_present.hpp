#ifndef VEYRA_PLATFORM_SDL3_GPU_PRESENT_HPP
#define VEYRA_PLATFORM_SDL3_GPU_PRESENT_HPP

#include "veyra/platform/sdl3_window.hpp"
#include "veyra/platform/window.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

namespace veyra {

class SdlGpuPresenter {
  public:
    SdlGpuPresenter() noexcept = default;
    ~SdlGpuPresenter();

    SdlGpuPresenter(const SdlGpuPresenter&) = delete;
    SdlGpuPresenter& operator=(const SdlGpuPresenter&) = delete;
    SdlGpuPresenter(SdlGpuPresenter&&) = delete;
    SdlGpuPresenter& operator=(SdlGpuPresenter&&) = delete;

    WindowResult create() noexcept;
    WindowResult create(const char* driver) noexcept;
    bool ready() const noexcept;
    const char* device_driver() const noexcept;
    std::uint64_t generation() const noexcept;
    bool claimed() const noexcept;

    WindowResult claim(Sdl3Window& window) noexcept;
    void release() noexcept;

    WindowResult
    acquire_surface(SoftwareSurface& surface, std::span<SoftwarePixel> backing) noexcept;
    WindowResult present() noexcept;

  private:
    void destroy_resources() noexcept;
    void destroy_device() noexcept;
    WindowResult ensure_resources(std::size_t width, std::size_t height) noexcept;
    WindowResult present_pixels() noexcept;

    void* device_{};
    Sdl3Window* window_{};
    void* upload_{};
    void* staging_{};
    std::size_t staging_bytes_{};
    std::size_t pending_width_{};
    std::size_t pending_height_{};
    std::size_t texture_width_{};
    std::size_t texture_height_{};
    std::span<SoftwarePixel> acquired_pixels_{};
    std::uint64_t generation_{};
    bool frame_acquired_{};
};

static_assert(SoftwareSurfacePresenter<SdlGpuPresenter>);

}

#endif
