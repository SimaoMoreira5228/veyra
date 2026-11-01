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
    struct RectInstance {
        float x{};
        float y{};
        float width{};
        float height{};
        float red{};
        float green{};
        float blue{};
        float alpha{};
    };

    bool can_submit() const noexcept;

    std::unique_ptr<Device> device_;
    WebGpuConsumer consumer_;
    std::array<RectInstance, kMaxInstances> instances_{};
    std::size_t instance_count_{};
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
