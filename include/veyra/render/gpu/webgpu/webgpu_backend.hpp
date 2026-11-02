#ifndef VEYRA_RENDER_WEBGPU_BACKEND_HPP
#define VEYRA_RENDER_WEBGPU_BACKEND_HPP

#include "veyra/render/gpu/webgpu/webgpu_consumer.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <span>

namespace veyra {

enum class WebGpuFrameResult : std::uint8_t {
    complete,
    no_device,
    unsupported,
    readback_failed,
};

class WebGpuBackend : public WebGpuSink {
  public:
    static constexpr std::size_t kMaxInstances = 1024;
    static constexpr std::size_t kMaxPathVertices = 4096;
    static constexpr std::size_t kMaxPathIndices = 8192;

    WebGpuBackend() noexcept;
    ~WebGpuBackend() override;

    WebGpuBackend(const WebGpuBackend&) = delete;
    WebGpuBackend& operator=(const WebGpuBackend&) = delete;

    bool create() noexcept;
    bool valid() const noexcept;

    WebGpuFrameResult render(
        std::span<const DisplayCommand> commands,
        std::uint32_t width,
        std::uint32_t height,
        std::span<SoftwarePixel> output
    ) noexcept;

    void begin_frame(std::uint32_t width, std::uint32_t height) noexcept override;
    void draw(const WebGpuBatch& batch, std::span<const DisplayCommand> commands) noexcept override;
    void end_frame() noexcept override;

  private:
    struct Device;
    struct GpuInstance {
        float x{};
        float y{};
        float width{};
        float height{};
        float red{};
        float green{};
        float blue{};
        float alpha{};
        float u0{};
        float v0{};
        float u1{};
        float v1{};
        float radius{};
        float mode{};
        float padding[2]{};
    };

    enum class DrawTexture : std::uint8_t {
        white,
        mask,
        glyph,
        image,
    };

    struct PathVertex {
        float x{};
        float y{};
        float red{};
        float green{};
        float blue{};
        float alpha{};
    };

    struct DrawRange {
        std::size_t first{};
        std::size_t count{};
        bool clipped{};
        LayoutRect clip{};
        DrawTexture texture{DrawTexture::white};
        const void* resource{};
        SoftwareImageSampling sampling{SoftwareImageSampling::nearest};
        bool path{};
        std::uint32_t path_index_first{};
        std::uint32_t path_index_count{};
        std::uint32_t path_cover_first{};
        SoftwarePathFillRule path_fill_rule{SoftwarePathFillRule::even_odd};
    };

    bool can_submit() const noexcept;

    std::unique_ptr<Device> device_;
    WebGpuConsumer consumer_;
    std::array<GpuInstance, kMaxInstances> instances_{};
    std::size_t instance_count_{};
    std::array<DrawRange, WebGpuFramePlan::kMaxBatches> ranges_{};
    std::size_t range_count_{};
    std::array<PathVertex, kMaxPathVertices> path_vertices_{};
    std::size_t path_vertex_count_{};
    std::array<std::uint32_t, kMaxPathIndices> path_indices_{};
    std::size_t path_index_count_{};
    std::uint32_t width_{};
    std::uint32_t height_{};
    bool unsupported_{};
};

}

namespace veyra::render {
using ::veyra::WebGpuBackend;
using ::veyra::WebGpuFrameResult;
}

namespace veyra::render::gpu::webgpu {
using ::veyra::WebGpuBackend;
using ::veyra::WebGpuFrameResult;
}

#endif
