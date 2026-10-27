#ifndef VEYRA_RENDER_SDL_GPU_RENDERER_HPP
#define VEYRA_RENDER_SDL_GPU_RENDERER_HPP

#include "veyra/platform/window.hpp"
#include "veyra/render/cpu/software_renderer.hpp"

#include <cstddef>
#include <span>

namespace veyra {

class SdlGpuPresenter;

constexpr std::size_t kSdlGpuMaxRects = 256;
constexpr std::size_t kSdlGpuMaxDraws = 16;
constexpr std::size_t kSdlGpuMaxFrameImages = 64;

struct SdlGpuRenderStats {
    std::size_t submitted{};
    std::size_t draws{};
    std::size_t rect_draws{};
    std::size_t rounded_draws{};
    std::size_t image_draws{};
    std::size_t mask_draws{};
    std::size_t stroke_draws{};
    std::size_t rounded_stroke_draws{};
    std::size_t glyph_draws{};
    std::size_t uploads{};
    std::size_t upload_bytes{};
};

struct SdlGpuRenderer {
    void* renderer_state{};
    std::size_t width{};
    std::size_t height{};
    bool ready{};
};

WindowResult create_sdl_gpu_renderer(
    SdlGpuPresenter& presenter,
    std::size_t width,
    std::size_t height,
    SdlGpuRenderer& renderer
) noexcept;

void destroy_sdl_gpu_renderer(SdlGpuRenderer& renderer) noexcept;

WindowResult render_sdl_gpu_rects(
    SdlGpuRenderer& renderer,
    std::span<const DisplayCommand> commands,
    std::span<SoftwarePixel> output,
    SdlGpuRenderStats& stats
) noexcept;

WindowResult
resize_sdl_gpu_renderer(SdlGpuRenderer& renderer, std::size_t width, std::size_t height) noexcept;

WindowResult present_sdl_gpu_frame(SdlGpuRenderer& renderer) noexcept;

bool sdl_gpu_renderer_ready(const SdlGpuRenderer& renderer) noexcept;

}
#endif
