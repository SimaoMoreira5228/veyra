#include "sdl_gpu_internal.hpp"

#include "software_renderer_internal.hpp"

#include <SDL3/SDL_gpu.h>

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace veyra::sdl_gpu_detail {
namespace {

WindowResult upload_texture_bytes(
    SdlGpuState& state,
    SDL_GPUTexture* texture,
    const std::uint8_t* source,
    std::size_t width,
    std::size_t height,
    std::size_t source_stride,
    std::size_t pixel_bytes
) noexcept {
    const auto row_bytes = width * pixel_bytes;
    const auto band_capacity = kSdlGpuImageStagingBytes / row_bytes;

    if (band_capacity == 0) {
        return WindowResult::unsupported_size;
    }

    auto* commands = SDL_AcquireGPUCommandBuffer(state.device);

    if (commands == nullptr) {
        return WindowResult::presentation_failed;
    }

    auto* pass = SDL_BeginGPUCopyPass(commands);

    if (pass == nullptr) {
        SDL_CancelGPUCommandBuffer(commands);

        return WindowResult::presentation_failed;
    }

    auto y = std::size_t{};

    while (y < height) {
        const auto band_rows = (height - y) < band_capacity ? (height - y) : band_capacity;
        const auto band_bytes = band_rows * row_bytes;
        auto* mapping = static_cast<std::uint8_t*>(
            SDL_MapGPUTransferBuffer(state.device, state.image_staging, true)
        );

        if (mapping == nullptr) {
            SDL_CancelGPUCommandBuffer(commands);

            return WindowResult::presentation_failed;
        }

        for (auto row = std::size_t{}; row < band_rows; ++row) {
            std::memcpy(mapping + row * row_bytes, source + (y + row) * source_stride, row_bytes);
        }

        SDL_UnmapGPUTransferBuffer(state.device, state.image_staging);

        const auto source_info = SDL_GPUTextureTransferInfo{
            .transfer_buffer = state.image_staging,
            .offset = 0,
            .pixels_per_row = static_cast<std::uint32_t>(width),
            .rows_per_layer = static_cast<std::uint32_t>(band_rows),
        };

        const auto destination = SDL_GPUTextureRegion{
            .texture = texture,
            .mip_level = 0,
            .layer = 0,
            .x = 0,
            .y = static_cast<std::uint32_t>(y),
            .z = 0,
            .w = static_cast<std::uint32_t>(width),
            .h = static_cast<std::uint32_t>(band_rows),
            .d = 1,
        };

        SDL_UploadToGPUTexture(pass, &source_info, &destination, false);

        y += band_rows;
    }

    SDL_EndGPUCopyPass(pass);

    auto* fence = SDL_SubmitGPUCommandBufferAndAcquireFence(commands);

    if (fence == nullptr) {
        return WindowResult::presentation_failed;
    }

    SDL_WaitForGPUFences(state.device, true, &fence, 1);
    SDL_ReleaseGPUFence(state.device, fence);

    return WindowResult::ready;
}

}

WindowResult ensure_sdl_image(
    SdlGpuState& state,
    const SoftwareImage& image,
    std::uint16_t& slot,
    std::size_t& uploaded_bytes
) noexcept {
    uploaded_bytes = 0;

    if (!render_detail::valid_image(image)) {
        return WindowResult::invalid_input;
    }

    for (auto index = std::size_t{}; index < kSdlGpuMaxFrameImages; ++index) {
        const auto& entry = state.images[index];

        if (entry.occupied && entry.key == &image && entry.generation == image.generation &&
            entry.width == image.width && entry.height == image.height) {
            slot = static_cast<std::uint16_t>(index);

            return WindowResult::ready;
        }
    }

    const auto index = state.image_cursor;
    state.image_cursor = (state.image_cursor + 1) % kSdlGpuMaxFrameImages;

    auto& entry = state.images[index];

    if (entry.occupied) {
        if (entry.texture != nullptr) {
            SDL_ReleaseGPUTexture(state.device, entry.texture);
        }

        entry = SdlImageEntry{};
    }

    const auto texture_info = SDL_GPUTextureCreateInfo{
        .type = SDL_GPU_TEXTURETYPE_2D,
        .format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
        .usage = SDL_GPU_TEXTUREUSAGE_SAMPLER,
        .width = static_cast<std::uint32_t>(image.width),
        .height = static_cast<std::uint32_t>(image.height),
        .layer_count_or_depth = 1,
        .num_levels = 1,
        .sample_count = SDL_GPU_SAMPLECOUNT_1,
    };

    auto* texture = SDL_CreateGPUTexture(state.device, &texture_info);

    if (texture == nullptr) {
        return WindowResult::presentation_failed;
    }

    const auto uploaded = upload_texture_bytes(
        state,
        texture,
        reinterpret_cast<const std::uint8_t*>(image.pixels.data()),
        image.width,
        image.height,
        image.stride * sizeof(SoftwarePixel),
        sizeof(SoftwarePixel)
    );

    if (uploaded != WindowResult::ready) {
        SDL_ReleaseGPUTexture(state.device, texture);

        return uploaded;
    }

    entry.key = &image;
    entry.generation = image.generation;
    entry.width = image.width;
    entry.height = image.height;
    entry.texture = texture;
    entry.occupied = true;

    slot = static_cast<std::uint16_t>(index);
    uploaded_bytes = image.width * image.height * sizeof(SoftwarePixel);

    return WindowResult::ready;
}

WindowResult ensure_sdl_mask(
    SdlGpuState& state,
    const SoftwareMask& mask,
    std::uint16_t& slot,
    std::size_t& uploaded_bytes
) noexcept {
    uploaded_bytes = 0;

    if (mask.width == 0 || mask.height == 0 || mask.stride < mask.width ||
        mask.coverage.size() < mask.height * mask.stride) {
        return WindowResult::invalid_input;
    }

    for (auto index = std::size_t{}; index < kSdlGpuMaxFrameImages; ++index) {
        const auto& entry = state.masks[index];

        if (entry.occupied && entry.key == &mask && entry.generation == mask.generation &&
            entry.width == mask.width && entry.height == mask.height) {
            slot = static_cast<std::uint16_t>(index);

            return WindowResult::ready;
        }
    }

    const auto index = state.mask_cursor;
    state.mask_cursor = (state.mask_cursor + 1) % kSdlGpuMaxFrameImages;

    auto& entry = state.masks[index];

    if (entry.occupied) {
        if (entry.texture != nullptr) {
            SDL_ReleaseGPUTexture(state.device, entry.texture);
        }

        entry = SdlMaskEntry{};
    }

    const auto texture_info = SDL_GPUTextureCreateInfo{
        .type = SDL_GPU_TEXTURETYPE_2D,
        .format = SDL_GPU_TEXTUREFORMAT_R8_UNORM,
        .usage = SDL_GPU_TEXTUREUSAGE_SAMPLER,
        .width = static_cast<std::uint32_t>(mask.width),
        .height = static_cast<std::uint32_t>(mask.height),
        .layer_count_or_depth = 1,
        .num_levels = 1,
        .sample_count = SDL_GPU_SAMPLECOUNT_1,
    };

    auto* texture = SDL_CreateGPUTexture(state.device, &texture_info);

    if (texture == nullptr) {
        return WindowResult::presentation_failed;
    }

    const auto uploaded = upload_texture_bytes(
        state,
        texture,
        mask.coverage.data(),
        mask.width,
        mask.height,
        mask.stride,
        sizeof(std::uint8_t)
    );

    if (uploaded != WindowResult::ready) {
        SDL_ReleaseGPUTexture(state.device, texture);

        return uploaded;
    }

    entry.key = &mask;
    entry.generation = mask.generation;
    entry.width = mask.width;
    entry.height = mask.height;
    entry.texture = texture;
    entry.occupied = true;

    slot = static_cast<std::uint16_t>(index);
    uploaded_bytes = mask.width * mask.height;

    return WindowResult::ready;
}

const SDL_GPUTexture* sdl_image_texture(const SdlGpuState& state, std::uint16_t slot) noexcept {
    if (slot >= kSdlGpuMaxFrameImages || !state.images[slot].occupied) {
        return nullptr;
    }

    return state.images[slot].texture;
}

const SDL_GPUTexture* sdl_mask_texture(const SdlGpuState& state, std::uint16_t slot) noexcept {
    if (slot >= kSdlGpuMaxFrameImages || !state.masks[slot].occupied) {
        return nullptr;
    }

    return state.masks[slot].texture;
}

}
