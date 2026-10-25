#include "gallery_backends.hpp"

#if defined(VEYRA_GALLERY_HAS_NOGA)
#include "veyra/render/noga_frame.hpp"
#include "veyra/render/noga_rect.hpp"
#endif
#if defined(VEYRA_GALLERY_HAS_SDLGPU)
#include "veyra/platform/sdl3_gpu_present.hpp"
#include "veyra/platform/sdl3_window.hpp"
#include "veyra/render/sdl_gpu_renderer.hpp"
#endif

#include <array>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace gallery {

bool parse_backend(std::string_view name, Backend& backend) {
    if (name == "cpu") {
        backend = Backend::cpu;
        return true;
    }
    if (name == "noga") {
        backend = Backend::noga;
        return true;
    }
    if (name == "sdlgpu") {
        backend = Backend::sdlgpu;
        return true;
    }
    return false;
}

int render_cpu(
    const std::vector<veyra::DisplayCommand>& commands,
    std::vector<veyra::SoftwarePixel>& pixels,
    std::size_t width,
    std::size_t height
) {
    std::array<veyra::SoftwareClipRect, 8> clips{};
    std::array<veyra::LayoutPoint, 8> translations{};
    const veyra::SoftwareRenderWorkspace workspace{
        .clips = clips,
        .translations = translations,
    };
    const veyra::SoftwareSurface surface{
        .width = width,
        .height = height,
        .stride = width,
        .pixels = pixels,
    };
    const auto result = veyra::render_software(commands, surface, workspace);
    if (result != veyra::SoftwareRenderResult::complete) {
        throw std::runtime_error("gallery rendering failed");
    }
    return 0;
}

#if defined(VEYRA_GALLERY_HAS_NOGA)

int render_noga(
    const std::vector<veyra::DisplayCommand>& commands,
    std::vector<veyra::SoftwarePixel>& pixels,
    std::size_t width,
    std::size_t height
) {
    auto context = veyra::NogaContext{};
    if (veyra::create_noga_context(context, width, height) != veyra::NogaContextResult::ready) {
        std::cerr << "gallery: noga device unavailable, skipping\n";
        return 77;
    }
    auto pipeline = veyra::NogaFramePipeline{};
    if (veyra::create_noga_frame_pipeline(context, pipeline) != veyra::NogaRenderResult::complete) {
        veyra::destroy_noga_context(context);
        std::cerr << "gallery: noga pipeline unavailable, skipping\n";
        return 77;
    }
    auto stats = veyra::NogaFrameStats{};
    const auto result = veyra::render_noga_frame(context, pipeline, commands, pixels, stats);
    veyra::destroy_noga_frame_pipeline(context, pipeline);
    veyra::destroy_noga_context(context);
    if (result != veyra::NogaRenderResult::complete) {
        throw std::runtime_error("gallery noga rendering failed");
    }
    return 0;
}

#endif

#if defined(VEYRA_GALLERY_HAS_SDLGPU)

int render_sdlgpu(
    const std::vector<veyra::DisplayCommand>& commands,
    std::vector<veyra::SoftwarePixel>& pixels,
    std::size_t width,
    std::size_t height
) {
    auto window = veyra::Sdl3Window("veyra gallery", 160, 120);
    if (window.creation_result() != veyra::WindowResult::ready) {
        std::cerr << "gallery: sdlgpu window unavailable, skipping\n";
        return 77;
    }
    auto presenter = veyra::SdlGpuPresenter{};
    if (presenter.create() != veyra::WindowResult::ready) {
        std::cerr << "gallery: sdlgpu device unavailable, skipping\n";
        return 77;
    }
    if (presenter.claim(window) != veyra::WindowResult::ready) {
        presenter.release();
        std::cerr << "gallery: sdlgpu window claim unavailable, skipping\n";
        return 77;
    }
    auto renderer = veyra::SdlGpuRenderer{};
    if (veyra::create_sdl_gpu_renderer(presenter, width, height, renderer) !=
        veyra::WindowResult::ready) {
        presenter.release();
        std::cerr << "gallery: sdlgpu renderer unavailable, skipping\n";
        return 77;
    }
    auto stats = veyra::SdlGpuRenderStats{};
    const auto result = veyra::render_sdl_gpu_rects(renderer, commands, pixels, stats);
    veyra::destroy_sdl_gpu_renderer(renderer);
    presenter.release();
    if (result != veyra::WindowResult::ready) {
        throw std::runtime_error("gallery sdlgpu rendering failed");
    }
    return 0;
}

#endif

int render_pixels(
    const std::vector<veyra::DisplayCommand>& commands,
    std::vector<veyra::SoftwarePixel>& pixels,
    std::size_t width,
    std::size_t height,
    Backend backend
) {
    if (backend == Backend::cpu) {
        return render_cpu(commands, pixels, width, height);
    }
    if (backend == Backend::noga) {
#if defined(VEYRA_GALLERY_HAS_NOGA)
        return render_noga(commands, pixels, width, height);
#else
        std::cerr << "gallery: noga backend not built, skipping\n";
        return 77;
#endif
    }
    if (backend == Backend::sdlgpu) {
#if defined(VEYRA_GALLERY_HAS_SDLGPU)
        return render_sdlgpu(commands, pixels, width, height);
#else
        std::cerr << "gallery: sdlgpu backend not built, skipping\n";
        return 77;
#endif
    }
    throw std::invalid_argument("gallery backend must be cpu, noga, or sdlgpu");
}

}
