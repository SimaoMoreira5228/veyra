#ifndef VEYRA_RENDER_NOGA_RECT_HPP
#define VEYRA_RENDER_NOGA_RECT_HPP

#include "veyra/render/software_renderer.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

namespace veyra {

constexpr std::size_t kNogaMaxRects = 256;

enum class NogaContextResult : std::uint8_t {
    ready,
    invalid_input,
    unsupported,
    creation_failed,
    device_lost,
};

enum class NogaRenderResult : std::uint8_t {
    complete,
    invalid_input,
    insufficient_capacity,
    unsupported_commands,
    device_error,
    unchanged,
    device_lost,
};

struct NogaDeviceInfo {
    char device_name[256]{};
    float timestamp_period_ns{};
    std::uint32_t queue_count{};
    bool indirect_mesh_draw{};
};

struct NogaContext {
    void* context_state{};
    std::size_t width{};
    std::size_t height{};
    NogaDeviceInfo info{};
    bool ready{};
};

struct NogaRectStats {
    std::size_t submitted{};
    std::size_t draws{};
};

struct NogaRectPipeline {
    void* pipeline_state{};
    bool ready{};
};

NogaContextResult
create_noga_context(NogaContext& context, std::size_t width, std::size_t height) noexcept;

void destroy_noga_context(NogaContext& context) noexcept;

NogaContextResult
resize_noga_target(NogaContext& context, std::size_t width, std::size_t height) noexcept;

bool noga_context_ready(const NogaContext& context) noexcept;

bool noga_context_device_lost(const NogaContext& context) noexcept;

std::uint64_t noga_context_generation(const NogaContext& context) noexcept;

NogaRenderResult
create_noga_rect_pipeline(NogaContext& context, NogaRectPipeline& pipeline) noexcept;

void destroy_noga_rect_pipeline(NogaContext& context, NogaRectPipeline& pipeline) noexcept;

NogaRenderResult render_noga_rects(
    NogaContext& context,
    NogaRectPipeline& pipeline,
    std::span<const DisplayCommand> commands,
    std::span<SoftwarePixel> output,
    NogaRectStats& stats
) noexcept;

}

#endif
