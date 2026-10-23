#include "veyra/render/sdl_gpu_renderer.hpp"

#include "sdl_gpu_internal.hpp"

#include <SDL3/SDL_gpu.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string_view>

namespace veyra {
namespace {

sdl_gpu_detail::SdlGpuState* state_of_renderer(SdlGpuRenderer& renderer) noexcept {
    return static_cast<sdl_gpu_detail::SdlGpuState*>(renderer.renderer_state);
}

bool device_alive(const sdl_gpu_detail::SdlGpuState& state) noexcept {
    return state.presenter != nullptr && state.presenter->ready() &&
        state.presenter->gpu_device() == state.device;
}

void destroy_samplers(sdl_gpu_detail::SdlGpuState& state) noexcept {
    if (state.device == nullptr) {
        return;
    }

    if (state.nearest_sampler != nullptr) {
        SDL_ReleaseGPUSampler(state.device, state.nearest_sampler);
        state.nearest_sampler = nullptr;
    }

    if (state.linear_sampler != nullptr) {
        SDL_ReleaseGPUSampler(state.device, state.linear_sampler);
        state.linear_sampler = nullptr;
    }
}

void destroy_caches(sdl_gpu_detail::SdlGpuState& state) noexcept {
    if (state.device == nullptr) {
        return;
    }

    for (auto& entry : state.images) {
        if (entry.occupied && entry.texture != nullptr) {
            SDL_ReleaseGPUTexture(state.device, entry.texture);
        }

        entry = sdl_gpu_detail::SdlImageEntry{};
    }

    for (auto& entry : state.masks) {
        if (entry.occupied && entry.texture != nullptr) {
            SDL_ReleaseGPUTexture(state.device, entry.texture);
        }

        entry = sdl_gpu_detail::SdlMaskEntry{};
    }

    state.image_cursor = 0;
    state.mask_cursor = 0;
}

struct KindUpload {
    const void* base{};
    std::size_t stride{};
};

KindUpload kind_upload_base(
    const sdl_gpu_detail::SdlGpuState& state,
    sdl_gpu_detail::SdlDrawPipe pipe
) noexcept {
    using sdl_gpu_detail::SdlDrawPipe;

    if (pipe == SdlDrawPipe::rect) {
        return {state.rect_hosts.data(), sizeof(sdl_gpu_detail::RectInstance)};
    }

    if (pipe == SdlDrawPipe::rounded) {
        return {state.rounded_hosts.data(), sizeof(sdl_gpu_detail::RoundedRectInstance)};
    }

    if (pipe == SdlDrawPipe::image) {
        return {state.image_hosts.data(), sizeof(sdl_gpu_detail::ImageInstance)};
    }

    if (pipe == SdlDrawPipe::mask) {
        return {state.mask_hosts.data(), sizeof(sdl_gpu_detail::MaskInstance)};
    }

    if (pipe == SdlDrawPipe::stroke) {
        return {state.stroke_hosts.data(), sizeof(sdl_gpu_detail::StrokeInstance)};
    }

    if (pipe == SdlDrawPipe::rounded_stroke) {
        return {
            state.rounded_stroke_hosts.data(),
            sizeof(sdl_gpu_detail::RoundedStrokeInstance),
        };
    }

    return {state.glyph_hosts.data(), sizeof(sdl_gpu_detail::GlyphInstance)};
}

WindowResult submit_frame(
    sdl_gpu_detail::SdlGpuState& state,
    std::span<SoftwarePixel> output,
    SdlGpuRenderStats& stats
) noexcept {
    auto* commands = SDL_AcquireGPUCommandBuffer(state.device);

    if (commands == nullptr) {
        return WindowResult::presentation_failed;
    }

    auto used = std::array<std::size_t, sdl_gpu_detail::kSdlGpuPipeCount>{};

    for (auto run = std::size_t{}; run < stats.draws; ++run) {
        const auto& entry = state.runs[run];
        const auto index = static_cast<std::size_t>(entry.pipe);

        used[index] =
            entry.first + entry.count > used[index] ? entry.first + entry.count : used[index];
    }

    {
        auto* pass = SDL_BeginGPUCopyPass(commands);

        if (pass == nullptr) {
            SDL_CancelGPUCommandBuffer(commands);

            return WindowResult::presentation_failed;
        }

        for (auto index = std::size_t{}; index < sdl_gpu_detail::kSdlGpuPipeCount; ++index) {
            if (used[index] == 0) {
                continue;
            }

            const auto pipe = static_cast<sdl_gpu_detail::SdlDrawPipe>(index);
            const auto upload = kind_upload_base(state, pipe);
            const auto bytes = used[index] * upload.stride;

            auto* mapping = SDL_MapGPUTransferBuffer(state.device, state.vertex_staging, true);

            if (mapping == nullptr) {
                SDL_CancelGPUCommandBuffer(commands);

                return WindowResult::presentation_failed;
            }

            std::memcpy(mapping, upload.base, bytes);
            SDL_UnmapGPUTransferBuffer(state.device, state.vertex_staging);

            const auto source = SDL_GPUTransferBufferLocation{
                .transfer_buffer = state.vertex_staging,
                .offset = 0,
            };

            const auto destination = SDL_GPUBufferRegion{
                .buffer = state.vertex_buffers[index],
                .offset = 0,
                .size = static_cast<std::uint32_t>(bytes),
            };

            SDL_UploadToGPUBuffer(pass, &source, &destination, false);
        }

        SDL_EndGPUCopyPass(pass);
    }

    const auto extent = std::array<float, 2>{
        static_cast<float>(state.width),
        static_cast<float>(state.height),
    };

    SDL_PushGPUVertexUniformData(commands, 0, extent.data(), sizeof(float) * 2);

    {
        const auto target = SDL_GPUColorTargetInfo{
            .texture = state.target,
            .clear_color = {0.0F, 0.0F, 0.0F, 1.0F},
            .load_op = SDL_GPU_LOADOP_CLEAR,
            .store_op = SDL_GPU_STOREOP_STORE,
        };

        auto* pass = SDL_BeginGPURenderPass(commands, &target, 1, nullptr);

        if (pass == nullptr) {
            SDL_CancelGPUCommandBuffer(commands);

            return WindowResult::presentation_failed;
        }

        const auto viewport = SDL_GPUViewport{
            .x = 0.0F,
            .y = 0.0F,
            .w = static_cast<float>(state.width),
            .h = static_cast<float>(state.height),
            .min_depth = 0.0F,
            .max_depth = 1.0F,
        };

        SDL_SetGPUViewport(pass, &viewport);

        auto bound = sdl_gpu_detail::kSdlGpuPipeCount;

        for (auto run = std::size_t{}; run < stats.draws; ++run) {
            const auto& entry = state.runs[run];
            const auto index = static_cast<std::size_t>(entry.pipe);

            if (bound != index) {
                SDL_BindGPUGraphicsPipeline(pass, state.pipelines[index]);

                const auto binding = SDL_GPUBufferBinding{
                    .buffer = state.vertex_buffers[index],
                    .offset = 0,
                };

                SDL_BindGPUVertexBuffers(pass, 0, &binding, 1);
                bound = index;
            }

            const auto scissor = SDL_Rect{
                .x = static_cast<int>(entry.clip.left),
                .y = static_cast<int>(entry.clip.top),
                .w = static_cast<int>(entry.clip.right - entry.clip.left),
                .h = static_cast<int>(entry.clip.bottom - entry.clip.top),
            };

            SDL_SetGPUScissor(pass, &scissor);

            if (entry.pipe == sdl_gpu_detail::SdlDrawPipe::image) {
                const auto* texture = sdl_gpu_detail::sdl_image_texture(state, entry.texture);
                auto* sampler = entry.sampling == 0 ? state.nearest_sampler : state.linear_sampler;
                const auto binding = SDL_GPUTextureSamplerBinding{
                    .texture = const_cast<SDL_GPUTexture*>(texture),
                    .sampler = sampler,
                };

                SDL_BindGPUFragmentSamplers(pass, 0, &binding, 1);
                ++stats.image_draws;
            } else if (entry.pipe == sdl_gpu_detail::SdlDrawPipe::mask) {
                const auto* texture = sdl_gpu_detail::sdl_mask_texture(state, entry.texture);
                const auto binding = SDL_GPUTextureSamplerBinding{
                    .texture = const_cast<SDL_GPUTexture*>(texture),
                    .sampler = state.nearest_sampler,
                };

                SDL_BindGPUFragmentSamplers(pass, 0, &binding, 1);
                ++stats.mask_draws;
            } else if (entry.pipe == sdl_gpu_detail::SdlDrawPipe::glyph) {
                const auto* texture = sdl_gpu_detail::sdl_mask_texture(state, entry.texture);
                const auto binding = SDL_GPUTextureSamplerBinding{
                    .texture = const_cast<SDL_GPUTexture*>(texture),
                    .sampler = state.nearest_sampler,
                };

                SDL_BindGPUFragmentSamplers(pass, 0, &binding, 1);
                ++stats.glyph_draws;
            } else if (entry.pipe == sdl_gpu_detail::SdlDrawPipe::rect) {
                ++stats.rect_draws;
            } else if (entry.pipe == sdl_gpu_detail::SdlDrawPipe::rounded) {
                ++stats.rounded_draws;
            } else if (entry.pipe == sdl_gpu_detail::SdlDrawPipe::stroke) {
                ++stats.stroke_draws;
            } else {
                ++stats.rounded_stroke_draws;
            }

            SDL_DrawGPUPrimitives(
                pass,
                6,
                static_cast<std::uint32_t>(entry.count),
                0,
                static_cast<std::uint32_t>(entry.first)
            );
        }

        SDL_EndGPURenderPass(pass);
    }

    {
        auto* pass = SDL_BeginGPUCopyPass(commands);

        if (pass == nullptr) {
            SDL_CancelGPUCommandBuffer(commands);

            return WindowResult::presentation_failed;
        }

        const auto source = SDL_GPUTextureRegion{
            .texture = state.target,
            .mip_level = 0,
            .layer = 0,
            .w = static_cast<std::uint32_t>(state.width),
            .h = static_cast<std::uint32_t>(state.height),
            .d = 1,
        };

        const auto destination = SDL_GPUTextureTransferInfo{
            .transfer_buffer = state.download,
            .pixels_per_row = static_cast<std::uint32_t>(state.width),
            .rows_per_layer = static_cast<std::uint32_t>(state.height),
        };

        SDL_DownloadFromGPUTexture(pass, &source, &destination);
        SDL_EndGPUCopyPass(pass);
    }

    auto* fence = SDL_SubmitGPUCommandBufferAndAcquireFence(commands);

    if (fence == nullptr) {
        return WindowResult::presentation_failed;
    }

    SDL_WaitForGPUFences(state.device, true, &fence, 1);
    SDL_ReleaseGPUFence(state.device, fence);

    const auto* mapped = static_cast<const std::uint8_t*>(
        SDL_MapGPUTransferBuffer(state.device, state.download, false)
    );

    if (mapped == nullptr) {
        return WindowResult::presentation_failed;
    }

    std::memcpy(output.data(), mapped, state.width * state.height * sizeof(SoftwarePixel));
    SDL_UnmapGPUTransferBuffer(state.device, state.download);

    return WindowResult::ready;
}

}

WindowResult create_sdl_gpu_renderer(
    SdlGpuPresenter& presenter,
    std::size_t width,
    std::size_t height,
    SdlGpuRenderer& renderer
) noexcept {
    if (!presenter.ready() || !presenter.claimed() || width == 0 || height == 0) {
        return WindowResult::invalid_input;
    }

    if (renderer.ready) {
        return WindowResult::invalid_input;
    }

    if (std::string_view(presenter.device_driver()) != "vulkan") {
        return WindowResult::unsupported;
    }

    auto* stored = new (std::nothrow) sdl_gpu_detail::SdlGpuState{};

    if (stored == nullptr) {
        return WindowResult::creation_failed;
    }

    stored->device = static_cast<SDL_GPUDevice*>(presenter.gpu_device());
    stored->presenter = &presenter;
    stored->width = width;
    stored->height = height;

    if (!sdl_gpu_detail::create_sdl_samplers(*stored)) {
        destroy_samplers(*stored);
        delete stored;

        return WindowResult::creation_failed;
    }

    if (!sdl_gpu_detail::create_sdl_pipelines(*stored)) {
        sdl_gpu_detail::destroy_sdl_pipelines(*stored);
        destroy_samplers(*stored);
        delete stored;

        return WindowResult::creation_failed;
    }

    if (!sdl_gpu_detail::create_sdl_target(*stored, width, height)) {
        sdl_gpu_detail::destroy_sdl_target(*stored);
        sdl_gpu_detail::destroy_sdl_pipelines(*stored);
        destroy_samplers(*stored);
        delete stored;

        return WindowResult::creation_failed;
    }

    renderer.renderer_state = stored;
    renderer.width = width;
    renderer.height = height;
    renderer.ready = true;

    return WindowResult::ready;
}

void destroy_sdl_gpu_renderer(SdlGpuRenderer& renderer) noexcept {
    auto* stored = state_of_renderer(renderer);

    if (stored == nullptr) {
        return;
    }

    if (device_alive(*stored)) {
        destroy_caches(*stored);
        sdl_gpu_detail::destroy_sdl_target(*stored);
        sdl_gpu_detail::destroy_sdl_pipelines(*stored);
        destroy_samplers(*stored);
    }

    delete stored;

    renderer.renderer_state = nullptr;
    renderer.width = 0;
    renderer.height = 0;
    renderer.ready = false;
}

WindowResult render_sdl_gpu_rects(
    SdlGpuRenderer& renderer,
    std::span<const DisplayCommand> commands,
    std::span<SoftwarePixel> output,
    SdlGpuRenderStats& stats
) noexcept {
    auto* stored = state_of_renderer(renderer);

    if (stored == nullptr || !renderer.ready) {
        return WindowResult::invalid_input;
    }

    if (!device_alive(*stored)) {
        return WindowResult::invalid_input;
    }

    if (output.size() != stored->width * stored->height) {
        return WindowResult::invalid_input;
    }

    stats = SdlGpuRenderStats{};

    const auto emitted = sdl_gpu_detail::emit_sdl_frame(*stored, commands, stats);

    if (emitted != WindowResult::ready) {
        return emitted;
    }

    return submit_frame(*stored, output, stats);
}

WindowResult
resize_sdl_gpu_renderer(SdlGpuRenderer& renderer, std::size_t width, std::size_t height) noexcept {
    auto* stored = state_of_renderer(renderer);

    if (stored == nullptr || !renderer.ready) {
        return WindowResult::invalid_input;
    }

    if (!device_alive(*stored)) {
        return WindowResult::invalid_input;
    }

    if (width == 0 || height == 0) {
        return WindowResult::invalid_input;
    }

    if (width == stored->width && height == stored->height) {
        return WindowResult::ready;
    }

    sdl_gpu_detail::destroy_sdl_target(*stored);

    if (!sdl_gpu_detail::create_sdl_target(*stored, width, height)) {
        renderer.ready = false;
        renderer.width = 0;
        renderer.height = 0;

        return WindowResult::presentation_failed;
    }

    stored->width = width;
    stored->height = height;
    renderer.width = width;
    renderer.height = height;

    return WindowResult::ready;
}

WindowResult present_sdl_gpu_frame(SdlGpuRenderer& renderer) noexcept {
    auto* stored = state_of_renderer(renderer);

    if (stored == nullptr || !renderer.ready) {
        return WindowResult::invalid_input;
    }

    if (!device_alive(*stored)) {
        return WindowResult::invalid_input;
    }

    if (!stored->presenter->claimed()) {
        return WindowResult::invalid_input;
    }

    return stored->presenter->present_texture(stored->target, stored->width, stored->height);
}

bool sdl_gpu_renderer_ready(const SdlGpuRenderer& renderer) noexcept {
    return renderer.ready && renderer.renderer_state != nullptr;
}

}
