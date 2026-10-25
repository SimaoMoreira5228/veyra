#ifndef VEYRA_SRC_RENDER_NOGA_INTERNAL_HPP
#define VEYRA_SRC_RENDER_NOGA_INTERNAL_HPP

#include "noga/rect_shared.h"
#include "veyra/render/noga_frame.hpp"

#include <NoGraphicsAPI/NoGraphicsAPI.hpp>

#include <cstddef>
#include <cstdint>
#include <span>

namespace veyra::noga_detail {

constexpr float kChannelScale = 1.0F / 255.0F;
constexpr std::size_t kTranslationDepth = 8;

constexpr std::size_t kRootAlign = 16;

constexpr std::size_t root_offset() noexcept {
    const auto rect_bytes = kNogaMaxRects * sizeof(NogaRect);

    return (rect_bytes + kRootAlign - 1) & ~(kRootAlign - 1);
}

constexpr std::size_t kUploadBytes = root_offset() + sizeof(NogaRectRoot);

constexpr std::size_t kFramePipeCount = 7;
constexpr std::size_t kFramePipeSlots = 256;
constexpr std::size_t kFramePipeCapacity = 4096;
constexpr std::size_t kFrameStagingBytes = 1 << 20;
constexpr std::size_t kFrameUploadBytes = (3 << 20);

struct NogaImageEntry {
    const SoftwareImage* key{};
    std::uint64_t generation{};
    std::uint64_t epoch{};
    std::uint64_t frame_tag{};
    std::size_t width{};
    std::size_t height{};
    gpu::TextureHeap heap{};
    gpu::Texture* texture{};
    std::uint32_t slot{};
    bool occupied{};
};

struct NogaMaskEntry {
    const SoftwareMask* key{};
    std::uint64_t generation{};
    std::uint64_t epoch{};
    std::uint64_t frame_tag{};
    std::size_t width{};
    std::size_t height{};
    gpu::TextureHeap heap{};
    gpu::Texture* texture{};
    std::uint32_t slot{};
    bool occupied{};
};

struct FramePipelines {
    gpu::PSO* pipes[kFramePipeCount]{};
    gpu::TextureDescriptorHeap* textures{};
    gpu::SamplerDescriptorHeap* samplers{};
    gpu::GpuHeap upload{};
    std::size_t pipe_offsets[kFramePipeCount]{};
    std::size_t pipe_strides[kFramePipeCount]{};
    std::size_t root_base{};
    std::size_t staging_base{};
    std::size_t staging_used{};
    std::uint64_t bump{};
    std::uint64_t frame_seq{};
    NogaImageEntry images[kNogaMaxFrameImages]{};
    NogaMaskEntry masks[kNogaMaxFrameImages]{};
    std::size_t image_cursor{};
    std::size_t mask_cursor{};
};

struct State {
    gpu::Device* device{};
    gpu::CommandPool* pool{};
    gpu::TimelineSemaphore* semaphore{};
    gpu::GpuHeap upload{};
    gpu::GpuHeap readback{};
    gpu::TextureHeap texture_heap{};
    gpu::Texture* target{};
    gpu::RenderView* view{};
    std::uint64_t completed{};
    std::size_t width{};
    std::size_t height{};
    float timestamp_period_ns{};
    std::uint64_t epoch{1};
    bool target_ready{};
    bool device_lost{};
};

inline State* state_of(NogaContext& context) noexcept {
    return static_cast<State*>(context.context_state);
}

inline const State* state_of(const NogaContext& context) noexcept {
    return static_cast<const State*>(context.context_state);
}

inline gpu::PSO* pso_of(NogaRectPipeline& pipeline) noexcept {
    return static_cast<gpu::PSO*>(pipeline.pipeline_state);
}

inline FramePipelines* frames_of(NogaFramePipeline& pipeline) noexcept {
    return static_cast<FramePipelines*>(pipeline.pipeline_state);
}

void copy_name(char (&output)[256], const char* source) noexcept;

void destroy_target(State& state) noexcept;

bool create_target(State& state, std::size_t width, std::size_t height) noexcept;

const NogaImageEntry* lookup_image(
    const FramePipelines& stored,
    std::uint64_t epoch,
    const SoftwareImage& image,
    SoftwareImageSampling sampling
) noexcept;

const NogaMaskEntry*
lookup_mask(const FramePipelines& stored, std::uint64_t epoch, const SoftwareMask& mask) noexcept;

NogaRenderResult upload_frame_textures(
    State& state,
    FramePipelines& stored,
    gpu::CommandBuffer* commands,
    std::span<const DisplayCommand> frame_commands,
    std::size_t& uploads,
    std::size_t& upload_bytes
) noexcept;

void evict_frame_entries(State& state, FramePipelines& stored) noexcept;

void destroy_frame_entries(State& state, FramePipelines& stored) noexcept;

}

#endif
