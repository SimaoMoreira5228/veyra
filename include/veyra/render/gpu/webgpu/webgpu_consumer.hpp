#ifndef VEYRA_RENDER_WEBGPU_CONSUMER_HPP
#define VEYRA_RENDER_WEBGPU_CONSUMER_HPP

#include "veyra/render/display/display_batches.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace veyra {

enum class WebGpuPipeline : std::uint8_t {
    fill_rect,
    fill_rounded_rect,
    stroke_rect,
    stroke_rounded_rect,
    stroke_line,
    stroke_polyline,
    draw_mask,
    draw_glyph,
    fill_path,
    draw_image,
};

WebGpuPipeline webgpu_pipeline(BatchPrimitive primitive) noexcept;
std::string_view webgpu_pipeline_name(WebGpuPipeline pipeline) noexcept;

struct WebGpuBatch {
    WebGpuPipeline pipeline{WebGpuPipeline::fill_rect};
    const void* resource{};
    SoftwareImageSampling sampling{SoftwareImageSampling::nearest};
    std::uint64_t clip_id{};
    std::size_t clip_depth{};
    LayoutRect clip_bounds{};
    bool clipped{};
    std::size_t start{};
    std::size_t count{};
    std::size_t sequence{};
};

struct WebGpuFramePlan {
    static constexpr std::size_t kMaxBatches = 256;

    std::array<WebGpuBatch, kMaxBatches> batches{};
    std::size_t batch_count{};
    std::size_t command_count{};
    bool overflowed{};
};

enum class WebGpuPlanResult : std::uint8_t {
    complete,
    insufficient_capacity,
};

class WebGpuSink {
  public:
    virtual ~WebGpuSink() = default;

    virtual void begin_frame(std::uint32_t width, std::uint32_t height) noexcept = 0;
    virtual void
    draw(const WebGpuBatch& batch, std::span<const DisplayCommand> commands) noexcept = 0;
    virtual void end_frame() noexcept = 0;
};

class WebGpuConsumer {
  public:
    explicit WebGpuConsumer(WebGpuSink& sink) noexcept;

    WebGpuPlanResult submit(
        std::span<const DisplayCommand> commands,
        std::uint32_t width,
        std::uint32_t height,
        WebGpuFramePlan& plan
    ) noexcept;

  private:
    WebGpuSink* sink_;
};

}

namespace veyra::render {
using ::veyra::webgpu_pipeline;
using ::veyra::webgpu_pipeline_name;
using ::veyra::WebGpuBatch;
using ::veyra::WebGpuConsumer;
using ::veyra::WebGpuFramePlan;
using ::veyra::WebGpuPipeline;
using ::veyra::WebGpuPlanResult;
using ::veyra::WebGpuSink;
}

namespace veyra::render::gpu::webgpu {
using ::veyra::webgpu_pipeline;
using ::veyra::webgpu_pipeline_name;
using ::veyra::WebGpuBatch;
using ::veyra::WebGpuConsumer;
using ::veyra::WebGpuFramePlan;
using ::veyra::WebGpuPipeline;
using ::veyra::WebGpuPlanResult;
using ::veyra::WebGpuSink;
}

#endif
