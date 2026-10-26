#include "sdl_gpu_internal.hpp"

#include "glyph_sdl_frag_spv.hpp"
#include "glyph_sdl_vert_spv.hpp"
#include "image_sdl_frag_spv.hpp"
#include "image_sdl_vert_spv.hpp"
#include "mask_sdl_frag_spv.hpp"
#include "mask_sdl_vert_spv.hpp"
#include "rect_sdl_frag_spv.hpp"
#include "rect_sdl_vert_spv.hpp"
#include "rounded_rect_sdl_frag_spv.hpp"
#include "rounded_rect_sdl_vert_spv.hpp"
#include "rounded_stroke_sdl_frag_spv.hpp"
#include "rounded_stroke_sdl_vert_spv.hpp"
#include "stroke_sdl_frag_spv.hpp"
#include "stroke_sdl_vert_spv.hpp"

#include <SDL3/SDL_gpu.h>

#include <array>
#include <cstddef>
#include <cstdint>

namespace veyra::sdl_gpu_detail {
namespace {

bool create_shader(
    SDL_GPUDevice* device,
    const std::uint32_t* code,
    std::size_t words,
    bool vertex,
    std::uint32_t samplers,
    SDL_GPUShader*& shader
) noexcept {
    const auto info = SDL_GPUShaderCreateInfo{
        .code_size = words * sizeof(std::uint32_t),
        .code = reinterpret_cast<const std::uint8_t*>(code),
        .entrypoint = "main",
        .format = SDL_GPU_SHADERFORMAT_SPIRV,
        .stage = vertex ? SDL_GPU_SHADERSTAGE_VERTEX : SDL_GPU_SHADERSTAGE_FRAGMENT,
        .num_samplers = vertex ? 0U : samplers,
        .num_storage_textures = 0,
        .num_storage_buffers = 0,
        .num_uniform_buffers = vertex ? 1U : 0U,
    };

    shader = SDL_CreateGPUShader(device, &info);

    return shader != nullptr;
}

bool create_pipeline(
    SDL_GPUDevice* device,
    SDL_GPUShader* vertex,
    SDL_GPUShader* fragment,
    const SDL_GPUVertexBufferDescription* buffers,
    std::uint32_t buffer_total,
    const SDL_GPUVertexAttribute* attributes,
    std::uint32_t attribute_total,
    SDL_GPUGraphicsPipeline*& pipeline
) noexcept {
    const auto blend = SDL_GPUColorTargetBlendState{
        .src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA,
        .dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
        .color_blend_op = SDL_GPU_BLENDOP_ADD,
        .src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE,
        .dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
        .alpha_blend_op = SDL_GPU_BLENDOP_ADD,
        .enable_blend = true,
        .enable_color_write_mask = false,
    };

    const auto color = SDL_GPUColorTargetDescription{
        .format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
        .blend_state = blend,
    };

    const auto vertex_input = SDL_GPUVertexInputState{
        .vertex_buffer_descriptions = buffers,
        .num_vertex_buffers = buffer_total,
        .vertex_attributes = attributes,
        .num_vertex_attributes = attribute_total,
    };

    const auto rasterizer = SDL_GPURasterizerState{
        .fill_mode = SDL_GPU_FILLMODE_FILL,
        .cull_mode = SDL_GPU_CULLMODE_NONE,
        .front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE,
    };

    const auto multisample = SDL_GPUMultisampleState{
        .sample_count = SDL_GPU_SAMPLECOUNT_1,
    };

    const auto targets = SDL_GPUGraphicsPipelineTargetInfo{
        .color_target_descriptions = &color,
        .num_color_targets = 1,
    };

    const auto info = SDL_GPUGraphicsPipelineCreateInfo{
        .vertex_shader = vertex,
        .fragment_shader = fragment,
        .vertex_input_state = vertex_input,
        .primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST,
        .rasterizer_state = rasterizer,
        .multisample_state = multisample,
        .target_info = targets,
    };

    pipeline = SDL_CreateGPUGraphicsPipeline(device, &info);

    return pipeline != nullptr;
}

bool create_kind_pipeline(
    SDL_GPUDevice* device,
    const std::uint32_t* vertex_code,
    std::size_t vertex_words,
    const std::uint32_t* fragment_code,
    std::size_t fragment_words,
    std::uint32_t fragment_samplers,
    std::uint32_t stride,
    const SDL_GPUVertexAttribute* attributes,
    std::uint32_t attribute_total,
    SDL_GPUGraphicsPipeline*& pipeline
) noexcept {
    auto vertex = static_cast<SDL_GPUShader*>(nullptr);
    auto fragment = static_cast<SDL_GPUShader*>(nullptr);

    if (!create_shader(device, vertex_code, vertex_words, true, 0, vertex)) {
        return false;
    }

    if (!create_shader(device, fragment_code, fragment_words, false, fragment_samplers, fragment)) {
        SDL_ReleaseGPUShader(device, vertex);

        return false;
    }

    const auto buffer = SDL_GPUVertexBufferDescription{
        .slot = 0,
        .pitch = stride,
        .input_rate = SDL_GPU_VERTEXINPUTRATE_INSTANCE,
    };

    const auto created = create_pipeline(
        device,
        vertex,
        fragment,
        &buffer,
        1,
        attributes,
        attribute_total,
        pipeline
    );

    SDL_ReleaseGPUShader(device, vertex);
    SDL_ReleaseGPUShader(device, fragment);

    return created;
}

}

bool create_sdl_pipelines(SdlGpuState& state) noexcept {
    auto* device = state.device;

    const auto rect_attributes = std::array<SDL_GPUVertexAttribute, 2>{
        SDL_GPUVertexAttribute{
            .location = 0,
            .buffer_slot = 0,
            .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4,
            .offset = 0,
        },
        SDL_GPUVertexAttribute{
            .location = 1,
            .buffer_slot = 0,
            .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4,
            .offset = 16,
        },
    };

    if (!create_kind_pipeline(
            device,
            vulkan_detail::kRectSdlVertSpv,
            vulkan_detail::kRectSdlVertSpvWords,
            vulkan_detail::kRectSdlFragSpv,
            vulkan_detail::kRectSdlFragSpvWords,
            0,
            sizeof(RectInstance),
            rect_attributes.data(),
            2,
            state.pipelines[static_cast<std::size_t>(SdlDrawPipe::rect)]
        )) {
        return false;
    }

    const auto rounded_attributes = std::array<SDL_GPUVertexAttribute, 4>{
        SDL_GPUVertexAttribute{
            .location = 0,
            .buffer_slot = 0,
            .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4,
            .offset = 0,
        },
        SDL_GPUVertexAttribute{
            .location = 1,
            .buffer_slot = 0,
            .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4,
            .offset = 16,
        },
        SDL_GPUVertexAttribute{
            .location = 2,
            .buffer_slot = 0,
            .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4,
            .offset = 32,
        },
        SDL_GPUVertexAttribute{
            .location = 3,
            .buffer_slot = 0,
            .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT,
            .offset = 48,
        },
    };

    if (!create_kind_pipeline(
            device,
            vulkan_detail::kRoundedRectSdlVertSpv,
            vulkan_detail::kRoundedRectSdlVertSpvWords,
            vulkan_detail::kRoundedRectSdlFragSpv,
            vulkan_detail::kRoundedRectSdlFragSpvWords,
            0,
            sizeof(RoundedRectInstance),
            rounded_attributes.data(),
            4,
            state.pipelines[static_cast<std::size_t>(SdlDrawPipe::rounded)]
        )) {
        return false;
    }

    const auto image_attributes = std::array<SDL_GPUVertexAttribute, 3>{
        SDL_GPUVertexAttribute{
            .location = 0,
            .buffer_slot = 0,
            .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4,
            .offset = 0,
        },
        SDL_GPUVertexAttribute{
            .location = 1,
            .buffer_slot = 0,
            .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4,
            .offset = 16,
        },
        SDL_GPUVertexAttribute{
            .location = 2,
            .buffer_slot = 0,
            .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT,
            .offset = 32,
        },
    };

    if (!create_kind_pipeline(
            device,
            vulkan_detail::kImageSdlVertSpv,
            vulkan_detail::kImageSdlVertSpvWords,
            vulkan_detail::kImageSdlFragSpv,
            vulkan_detail::kImageSdlFragSpvWords,
            1,
            sizeof(ImageInstance),
            image_attributes.data(),
            3,
            state.pipelines[static_cast<std::size_t>(SdlDrawPipe::image)]
        )) {
        return false;
    }

    const auto mask_attributes = std::array<SDL_GPUVertexAttribute, 3>{
        SDL_GPUVertexAttribute{
            .location = 0,
            .buffer_slot = 0,
            .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4,
            .offset = 0,
        },
        SDL_GPUVertexAttribute{
            .location = 1,
            .buffer_slot = 0,
            .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2,
            .offset = 16,
        },
        SDL_GPUVertexAttribute{
            .location = 2,
            .buffer_slot = 0,
            .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4,
            .offset = 32,
        },
    };

    if (!create_kind_pipeline(
            device,
            vulkan_detail::kMaskSdlVertSpv,
            vulkan_detail::kMaskSdlVertSpvWords,
            vulkan_detail::kMaskSdlFragSpv,
            vulkan_detail::kMaskSdlFragSpvWords,
            1,
            sizeof(MaskInstance),
            mask_attributes.data(),
            3,
            state.pipelines[static_cast<std::size_t>(SdlDrawPipe::mask)]
        )) {
        return false;
    }

    const auto stroke_attributes = std::array<SDL_GPUVertexAttribute, 4>{
        SDL_GPUVertexAttribute{
            .location = 0,
            .buffer_slot = 0,
            .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4,
            .offset = 0,
        },
        SDL_GPUVertexAttribute{
            .location = 1,
            .buffer_slot = 0,
            .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4,
            .offset = 16,
        },
        SDL_GPUVertexAttribute{
            .location = 2,
            .buffer_slot = 0,
            .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4,
            .offset = 32,
        },
        SDL_GPUVertexAttribute{
            .location = 3,
            .buffer_slot = 0,
            .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT,
            .offset = 48,
        },
    };

    if (!create_kind_pipeline(
            device,
            vulkan_detail::kStrokeSdlVertSpv,
            vulkan_detail::kStrokeSdlVertSpvWords,
            vulkan_detail::kStrokeSdlFragSpv,
            vulkan_detail::kStrokeSdlFragSpvWords,
            0,
            sizeof(StrokeInstance),
            stroke_attributes.data(),
            4,
            state.pipelines[static_cast<std::size_t>(SdlDrawPipe::stroke)]
        )) {
        return false;
    }

    const auto rounded_stroke_attributes = std::array<SDL_GPUVertexAttribute, 5>{
        SDL_GPUVertexAttribute{
            .location = 0,
            .buffer_slot = 0,
            .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4,
            .offset = 0,
        },
        SDL_GPUVertexAttribute{
            .location = 1,
            .buffer_slot = 0,
            .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4,
            .offset = 16,
        },
        SDL_GPUVertexAttribute{
            .location = 2,
            .buffer_slot = 0,
            .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4,
            .offset = 32,
        },
        SDL_GPUVertexAttribute{
            .location = 3,
            .buffer_slot = 0,
            .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT,
            .offset = 48,
        },
        SDL_GPUVertexAttribute{
            .location = 4,
            .buffer_slot = 0,
            .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT,
            .offset = 52,
        },
    };

    if (!create_kind_pipeline(
            device,
            vulkan_detail::kRoundedStrokeSdlVertSpv,
            vulkan_detail::kRoundedStrokeSdlVertSpvWords,
            vulkan_detail::kRoundedStrokeSdlFragSpv,
            vulkan_detail::kRoundedStrokeSdlFragSpvWords,
            0,
            sizeof(RoundedStrokeInstance),
            rounded_stroke_attributes.data(),
            5,
            state.pipelines[static_cast<std::size_t>(SdlDrawPipe::rounded_stroke)]
        )) {
        return false;
    }

    const auto glyph_attributes = std::array<SDL_GPUVertexAttribute, 3>{
        SDL_GPUVertexAttribute{
            .location = 0,
            .buffer_slot = 0,
            .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4,
            .offset = 0,
        },
        SDL_GPUVertexAttribute{
            .location = 1,
            .buffer_slot = 0,
            .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4,
            .offset = 16,
        },
        SDL_GPUVertexAttribute{
            .location = 2,
            .buffer_slot = 0,
            .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4,
            .offset = 32,
        },
    };

    if (!create_kind_pipeline(
            device,
            vulkan_detail::kGlyphSdlVertSpv,
            vulkan_detail::kGlyphSdlVertSpvWords,
            vulkan_detail::kGlyphSdlFragSpv,
            vulkan_detail::kGlyphSdlFragSpvWords,
            1,
            sizeof(GlyphInstance),
            glyph_attributes.data(),
            3,
            state.pipelines[static_cast<std::size_t>(SdlDrawPipe::glyph)]
        )) {
        return false;
    }

    return true;
}

void destroy_sdl_pipelines(SdlGpuState& state) noexcept {
    if (state.device == nullptr) {
        return;
    }

    for (auto index = std::size_t{}; index < kSdlGpuPipeCount; ++index) {
        if (state.pipelines[index] != nullptr) {
            SDL_ReleaseGPUGraphicsPipeline(state.device, state.pipelines[index]);
            state.pipelines[index] = nullptr;
        }
    }
}

bool create_sdl_samplers(SdlGpuState& state) noexcept {
    const auto nearest = SDL_GPUSamplerCreateInfo{
        .min_filter = SDL_GPU_FILTER_NEAREST,
        .mag_filter = SDL_GPU_FILTER_NEAREST,
        .mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST,
        .address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE,
        .address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE,
        .address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE,
    };

    state.nearest_sampler = SDL_CreateGPUSampler(state.device, &nearest);

    if (state.nearest_sampler == nullptr) {
        return false;
    }

    const auto linear = SDL_GPUSamplerCreateInfo{
        .min_filter = SDL_GPU_FILTER_LINEAR,
        .mag_filter = SDL_GPU_FILTER_LINEAR,
        .mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST,
        .address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE,
        .address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE,
        .address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE,
    };

    state.linear_sampler = SDL_CreateGPUSampler(state.device, &linear);

    if (state.linear_sampler == nullptr) {
        SDL_ReleaseGPUSampler(state.device, state.nearest_sampler);
        state.nearest_sampler = nullptr;

        return false;
    }

    return true;
}

bool create_sdl_target(SdlGpuState& state, std::size_t width, std::size_t height) noexcept {
    if (width == 0 || height == 0 || width > 0xFFFF || height > 0xFFFF) {
        return false;
    }

    const auto pixels = width * height;

    if (pixels / width != height) {
        return false;
    }

    const auto bytes = pixels * sizeof(SoftwarePixel);

    if (bytes / sizeof(SoftwarePixel) != pixels) {
        return false;
    }

    const auto target_info = SDL_GPUTextureCreateInfo{
        .type = SDL_GPU_TEXTURETYPE_2D,
        .format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
        .usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET,
        .width = static_cast<std::uint32_t>(width),
        .height = static_cast<std::uint32_t>(height),
        .layer_count_or_depth = 1,
        .num_levels = 1,
        .sample_count = SDL_GPU_SAMPLECOUNT_1,
    };

    state.target = SDL_CreateGPUTexture(state.device, &target_info);

    if (state.target == nullptr) {
        return false;
    }

    const auto strides = std::array<std::size_t, kSdlGpuPipeCount>{
        sizeof(RectInstance),
        sizeof(RoundedRectInstance),
        sizeof(ImageInstance),
        sizeof(MaskInstance),
        sizeof(StrokeInstance),
        sizeof(RoundedStrokeInstance),
        sizeof(GlyphInstance),
    };

    for (auto index = std::size_t{}; index < kSdlGpuPipeCount; ++index) {
        const auto buffer_info = SDL_GPUBufferCreateInfo{
            .usage = SDL_GPU_BUFFERUSAGE_VERTEX,
            .size = static_cast<std::uint32_t>(strides[index] * kSdlGpuBudget),
        };

        state.vertex_buffers[index] = SDL_CreateGPUBuffer(state.device, &buffer_info);
        state.vertex_bytes[index] = strides[index] * kSdlGpuBudget;

        if (state.vertex_buffers[index] == nullptr) {
            return false;
        }
    }

    const auto vertex_staging_info = SDL_GPUTransferBufferCreateInfo{
        .usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
        .size = static_cast<std::uint32_t>(kSdlGpuVertexStagingBytes),
    };

    state.vertex_staging = SDL_CreateGPUTransferBuffer(state.device, &vertex_staging_info);

    if (state.vertex_staging == nullptr) {
        return false;
    }

    const auto image_staging_info = SDL_GPUTransferBufferCreateInfo{
        .usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
        .size = static_cast<std::uint32_t>(kSdlGpuImageStagingBytes),
    };

    state.image_staging = SDL_CreateGPUTransferBuffer(state.device, &image_staging_info);

    if (state.image_staging == nullptr) {
        return false;
    }

    const auto download_info = SDL_GPUTransferBufferCreateInfo{
        .usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD,
        .size = static_cast<std::uint32_t>(bytes),
    };

    state.download = SDL_CreateGPUTransferBuffer(state.device, &download_info);

    if (state.download == nullptr) {
        return false;
    }

    return true;
}

void destroy_sdl_target(SdlGpuState& state) noexcept {
    if (state.device == nullptr) {
        return;
    }

    for (auto index = std::size_t{}; index < kSdlGpuPipeCount; ++index) {
        if (state.vertex_buffers[index] != nullptr) {
            SDL_ReleaseGPUBuffer(state.device, state.vertex_buffers[index]);
            state.vertex_buffers[index] = nullptr;
            state.vertex_bytes[index] = 0;
        }
    }

    if (state.vertex_staging != nullptr) {
        SDL_ReleaseGPUTransferBuffer(state.device, state.vertex_staging);
        state.vertex_staging = nullptr;
    }

    if (state.image_staging != nullptr) {
        SDL_ReleaseGPUTransferBuffer(state.device, state.image_staging);
        state.image_staging = nullptr;
    }

    if (state.download != nullptr) {
        SDL_ReleaseGPUTransferBuffer(state.device, state.download);
        state.download = nullptr;
    }

    if (state.target != nullptr) {
        SDL_ReleaseGPUTexture(state.device, state.target);
        state.target = nullptr;
    }
}

}
