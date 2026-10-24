#include "veyra/render/noga_frame.hpp"

#include "noga/frame_shared.h"
#include "noga_internal.hpp"

#include "glyph_frame_frag_spv.hpp"
#include "glyph_frame_vert_spv.hpp"
#include "image_frame_frag_spv.hpp"
#include "image_frame_vert_spv.hpp"
#include "mask_frame_frag_spv.hpp"
#include "mask_frame_vert_spv.hpp"
#include "rect_frame_frag_spv.hpp"
#include "rect_frame_vert_spv.hpp"
#include "rounded_rect_frame_frag_spv.hpp"
#include "rounded_rect_frame_vert_spv.hpp"
#include "rounded_stroke_frame_frag_spv.hpp"
#include "rounded_stroke_frame_vert_spv.hpp"
#include "stroke_frame_frag_spv.hpp"
#include "stroke_frame_vert_spv.hpp"

#include <NoGraphicsAPI/NoGraphicsAPI.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <new>
#include <span>

namespace veyra {
namespace noga_detail {

namespace {

constexpr std::size_t kPipeStride[kFramePipeCount] = {
    sizeof(NogaFrameRect),
    sizeof(NogaFrameRounded),
    sizeof(NogaFrameImage),
    sizeof(NogaFrameMask),
    sizeof(NogaFrameStroke),
    sizeof(NogaFrameRoundedStroke),
    sizeof(NogaFrameGlyph),
};

constexpr std::size_t kPackedRootBytes = 32;

gpu::PSO* create_frame_pso(
    gpu::Device* device,
    std::span<const std::uint32_t> vertex,
    std::span<const std::uint32_t> fragment
) noexcept {
    const auto colors = std::array<gpu::ColorTargetDesc, 1>{
        gpu::ColorTargetDesc{
            .format = gpu::Format::rgba8_unorm,
            .blend = {
                .enabled = true,
                .color =
                    {
                        .source = gpu::BlendFactor::source_alpha,
                        .destination = gpu::BlendFactor::one_minus_source_alpha,
                        .operation = gpu::BlendOp::add,
                    },
                .alpha = {
                    .source = gpu::BlendFactor::one,
                    .destination = gpu::BlendFactor::one_minus_source_alpha,
                    .operation = gpu::BlendOp::add,
                },
            },
        },
    };

    return gpu::create_graphics_pso(
        device,
        {
            .vertex =
                {.code = {vertex.data(), vertex.size() * sizeof(std::uint32_t)},
                 .entry_point = "vertexMain"},
            .fragment =
                {.code = {fragment.data(), fragment.size() * sizeof(std::uint32_t)},
                 .entry_point = "fragmentMain"},
            .color_targets = {colors.data(), colors.size()},
        }
    );
}

}

}

NogaRenderResult
create_noga_frame_pipeline(NogaContext& context, NogaFramePipeline& pipeline) noexcept {
    auto* state = noga_detail::state_of(context);

    if (state == nullptr || !context.ready) {
        return NogaRenderResult::invalid_input;
    }

    if (pipeline.ready) {
        return NogaRenderResult::invalid_input;
    }

    auto* stored = new (std::nothrow) noga_detail::FramePipelines{};

    if (stored == nullptr) {
        return NogaRenderResult::device_error;
    }

    stored->upload = gpu::create_gpu_heap(state->device, noga_detail::kFrameUploadBytes);

    if (stored->upload.owner == nullptr) {
        delete stored;

        return NogaRenderResult::device_error;
    }

    stored->textures = gpu::create_texture_descriptor_heap(state->device, kNogaMaxFrameImages * 2);

    if (stored->textures == nullptr) {
        gpu::destroy_gpu_heap(stored->upload);
        delete stored;

        return NogaRenderResult::device_error;
    }

    stored->samplers = gpu::create_sampler_descriptor_heap(state->device, 2);

    if (stored->samplers == nullptr) {
        gpu::destroy_texture_descriptor_heap(stored->textures);
        gpu::destroy_gpu_heap(stored->upload);
        delete stored;

        return NogaRenderResult::device_error;
    }

    gpu::write_sampler_descriptor(
        stored->samplers,
        0,
        {.min_filter = gpu::Filter::nearest,
         .mag_filter = gpu::Filter::nearest,
         .address_u = gpu::AddressMode::clamp_to_edge,
         .address_v = gpu::AddressMode::clamp_to_edge}
    );
    gpu::write_sampler_descriptor(
        stored->samplers,
        1,
        {.min_filter = gpu::Filter::linear,
         .mag_filter = gpu::Filter::linear,
         .address_u = gpu::AddressMode::clamp_to_edge,
         .address_v = gpu::AddressMode::clamp_to_edge}
    );

    auto offset = std::size_t{};

    for (auto index = std::size_t{}; index < noga_detail::kFramePipeCount; ++index) {
        stored->pipe_offsets[index] = offset;
        stored->pipe_strides[index] = noga_detail::kPipeStride[index];
        offset += noga_detail::kFramePipeCapacity * noga_detail::kPipeStride[index];
    }

    stored->root_base = (offset + 15) & ~static_cast<std::size_t>(15);
    stored->staging_base = stored->root_base + kNogaMaxSubmitted * noga_detail::kPackedRootBytes;

    const auto vertex_words =
        std::array<std::span<const std::uint32_t>, noga_detail::kFramePipeCount>{
            std::span<const std::uint32_t>{
                noga_detail::kRectFrameVertSpv,
                std::size(noga_detail::kRectFrameVertSpv)
            },
            std::span<const std::uint32_t>{
                noga_detail::kRoundedRectFrameVertSpv,
                std::size(noga_detail::kRoundedRectFrameVertSpv)
            },
            std::span<const std::uint32_t>{
                noga_detail::kImageFrameVertSpv,
                std::size(noga_detail::kImageFrameVertSpv)
            },
            std::span<const std::uint32_t>{
                noga_detail::kMaskFrameVertSpv,
                std::size(noga_detail::kMaskFrameVertSpv)
            },
            std::span<const std::uint32_t>{
                noga_detail::kStrokeFrameVertSpv,
                std::size(noga_detail::kStrokeFrameVertSpv)
            },
            std::span<const std::uint32_t>{
                noga_detail::kRoundedStrokeFrameVertSpv,
                std::size(noga_detail::kRoundedStrokeFrameVertSpv)
            },
            std::span<const std::uint32_t>{
                noga_detail::kGlyphFrameVertSpv,
                std::size(noga_detail::kGlyphFrameVertSpv)
            },
        };

    const auto fragment_words =
        std::array<std::span<const std::uint32_t>, noga_detail::kFramePipeCount>{
            std::span<const std::uint32_t>{
                noga_detail::kRectFrameFragSpv,
                std::size(noga_detail::kRectFrameFragSpv)
            },
            std::span<const std::uint32_t>{
                noga_detail::kRoundedRectFrameFragSpv,
                std::size(noga_detail::kRoundedRectFrameFragSpv)
            },
            std::span<const std::uint32_t>{
                noga_detail::kImageFrameFragSpv,
                std::size(noga_detail::kImageFrameFragSpv)
            },
            std::span<const std::uint32_t>{
                noga_detail::kMaskFrameFragSpv,
                std::size(noga_detail::kMaskFrameFragSpv)
            },
            std::span<const std::uint32_t>{
                noga_detail::kStrokeFrameFragSpv,
                std::size(noga_detail::kStrokeFrameFragSpv)
            },
            std::span<const std::uint32_t>{
                noga_detail::kRoundedStrokeFrameFragSpv,
                std::size(noga_detail::kRoundedStrokeFrameFragSpv)
            },
            std::span<const std::uint32_t>{
                noga_detail::kGlyphFrameFragSpv,
                std::size(noga_detail::kGlyphFrameFragSpv)
            },
        };

    for (auto index = std::size_t{}; index < noga_detail::kFramePipeCount; ++index) {
        stored->pipes[index] = noga_detail::create_frame_pso(
            state->device,
            vertex_words[index],
            fragment_words[index]
        );

        if (stored->pipes[index] == nullptr) {
            for (auto done = std::size_t{}; done < index; ++done) {
                gpu::destroy_pso(stored->pipes[done]);
            }

            gpu::destroy_sampler_descriptor_heap(stored->samplers);
            gpu::destroy_texture_descriptor_heap(stored->textures);
            gpu::destroy_gpu_heap(stored->upload);
            delete stored;

            return NogaRenderResult::device_error;
        }
    }

    pipeline.pipeline_state = stored;
    pipeline.ready = true;

    return NogaRenderResult::complete;
}

void destroy_noga_frame_pipeline(NogaContext& context, NogaFramePipeline& pipeline) noexcept {
    auto* stored = noga_detail::frames_of(pipeline);

    if (stored == nullptr) {
        return;
    }

    auto* state = noga_detail::state_of(context);

    if (state != nullptr && state->device != nullptr) {
        gpu::wait_idle(state->device);

        for (auto* pipe : stored->pipes) {
            if (pipe != nullptr) {
                gpu::destroy_pso(pipe);
            }
        }

        noga_detail::destroy_frame_entries(*state, *stored);

        if (stored->samplers != nullptr) {
            gpu::destroy_sampler_descriptor_heap(stored->samplers);
        }

        if (stored->textures != nullptr) {
            gpu::destroy_texture_descriptor_heap(stored->textures);
        }

        if (stored->upload.owner != nullptr) {
            gpu::destroy_gpu_heap(stored->upload);
        }
    }

    delete stored;

    pipeline.pipeline_state = nullptr;
    pipeline.ready = false;
}

}
