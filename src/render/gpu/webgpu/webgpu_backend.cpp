#include "veyra/render/gpu/webgpu/webgpu_backend.hpp"

#include <webgpu/webgpu.h>
#include <webgpu/wgpu.h>

#include <array>
#include <cmath>
#include <variant>

namespace veyra {
namespace {

constexpr char kRectShader[] = R"(
struct Uniforms {
    size: vec2f,
};

@group(0) @binding(0) var<uniform> uniforms: Uniforms;

struct VertexOutput {
    @builtin(position) position: vec4f,
    @location(0) color: vec4f,
};

@vertex
fn vs_main(
    @builtin(vertex_index) vertex_index: u32,
    @location(0) rect: vec4f,
    @location(1) color: vec4f
) -> VertexOutput {
    var corners = array<vec2f, 6>(
        vec2f(0.0, 0.0),
        vec2f(1.0, 0.0),
        vec2f(0.0, 1.0),
        vec2f(0.0, 1.0),
        vec2f(1.0, 0.0),
        vec2f(1.0, 1.0)
    );
    let point = rect.xy + corners[vertex_index] * rect.zw;
    let ndc = vec2f(
        point.x / uniforms.size.x * 2.0 - 1.0,
        1.0 - point.y / uniforms.size.y * 2.0
    );

    var output: VertexOutput;
    output.position = vec4f(ndc, 0.0, 1.0);
    output.color = color;
    return output;
}

@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4f {
    return input.color;
}
)";

void on_adapter(
    WGPURequestAdapterStatus status,
    WGPUAdapter adapter,
    WGPUStringView message,
    void* user1,
    void* user2
) noexcept {
    (void)message;
    (void)user2;
    if (status == WGPURequestAdapterStatus_Success) {
        *static_cast<WGPUAdapter*>(user1) = adapter;
    }
}

void on_device(
    WGPURequestDeviceStatus status,
    WGPUDevice device,
    WGPUStringView message,
    void* user1,
    void* user2
) noexcept {
    (void)message;
    (void)user2;
    if (status == WGPURequestDeviceStatus_Success) {
        *static_cast<WGPUDevice*>(user1) = device;
    }
}

struct MapState {
    bool mapped{};
};

void on_map(WGPUMapAsyncStatus status, WGPUStringView message, void* user1, void* user2) noexcept {
    (void)message;
    (void)user2;
    static_cast<MapState*>(user1)->mapped = status == WGPUMapAsyncStatus_Success;
}

bool valid_rect(float x, float y, float width, float height) noexcept {
    return std::isfinite(x) && std::isfinite(y) && std::isfinite(width) && std::isfinite(height) &&
        width >= 0.0F && height >= 0.0F;
}

}

struct WebGpuBackend::Device {
    WGPUInstance instance{};
    WGPUAdapter adapter{};
    WGPUDevice device{};
    WGPUQueue queue{};
    WGPUShaderModule shader{};
    WGPUBindGroupLayout bind_group_layout{};
    WGPUPipelineLayout pipeline_layout{};
    WGPURenderPipeline pipeline{};
    bool valid{};

    ~Device() {
        if (pipeline != nullptr) {
            wgpuRenderPipelineRelease(pipeline);
        }
        if (pipeline_layout != nullptr) {
            wgpuPipelineLayoutRelease(pipeline_layout);
        }
        if (bind_group_layout != nullptr) {
            wgpuBindGroupLayoutRelease(bind_group_layout);
        }
        if (shader != nullptr) {
            wgpuShaderModuleRelease(shader);
        }
        if (queue != nullptr) {
            wgpuQueueRelease(queue);
        }
        if (device != nullptr) {
            wgpuDeviceRelease(device);
        }
        if (adapter != nullptr) {
            wgpuAdapterRelease(adapter);
        }
        if (instance != nullptr) {
            wgpuInstanceRelease(instance);
        }
    }

    bool create_pipeline() noexcept {
        auto source = WGPUShaderSourceWGSL{
            .chain = {.sType = WGPUSType_ShaderSourceWGSL},
            .code = {kRectShader, WGPU_STRLEN},
        };
        const WGPUShaderModuleDescriptor shader_descriptor{
            .nextInChain = &source.chain,
        };
        shader = wgpuDeviceCreateShaderModule(device, &shader_descriptor);
        if (shader == nullptr) {
            return false;
        }

        const WGPUBindGroupLayoutEntry bind_entry{
            .binding = 0,
            .visibility = WGPUShaderStage_Vertex,
            .buffer = {
                .type = WGPUBufferBindingType_Uniform,
                .minBindingSize = 16,
            },
        };
        const WGPUBindGroupLayoutDescriptor bind_layout_descriptor{
            .entryCount = 1,
            .entries = &bind_entry,
        };
        bind_group_layout = wgpuDeviceCreateBindGroupLayout(device, &bind_layout_descriptor);
        if (bind_group_layout == nullptr) {
            return false;
        }

        const WGPUPipelineLayoutDescriptor pipeline_layout_descriptor{
            .bindGroupLayoutCount = 1,
            .bindGroupLayouts = &bind_group_layout,
        };
        pipeline_layout = wgpuDeviceCreatePipelineLayout(device, &pipeline_layout_descriptor);
        if (pipeline_layout == nullptr) {
            return false;
        }

        const std::array<WGPUVertexAttribute, 2> attributes{
            WGPUVertexAttribute{
                .format = WGPUVertexFormat_Float32x4,
                .offset = 0,
                .shaderLocation = 0,
            },
            WGPUVertexAttribute{
                .format = WGPUVertexFormat_Float32x4,
                .offset = 16,
                .shaderLocation = 1,
            },
        };
        const WGPUVertexBufferLayout vertex_buffer{
            .stepMode = WGPUVertexStepMode_Instance,
            .arrayStride = 32,
            .attributeCount = attributes.size(),
            .attributes = attributes.data(),
        };
        const WGPUVertexState vertex{
            .module = shader,
            .entryPoint = {"vs_main", WGPU_STRLEN},
            .bufferCount = 1,
            .buffers = &vertex_buffer,
        };

        const WGPUBlendState blend{
            .color =
                {
                    .operation = WGPUBlendOperation_Add,
                    .srcFactor = WGPUBlendFactor_SrcAlpha,
                    .dstFactor = WGPUBlendFactor_OneMinusSrcAlpha,
                },
            .alpha = {
                .operation = WGPUBlendOperation_Add,
                .srcFactor = WGPUBlendFactor_One,
                .dstFactor = WGPUBlendFactor_OneMinusSrcAlpha,
            },
        };
        const WGPUColorTargetState target{
            .format = WGPUTextureFormat_RGBA8Unorm,
            .blend = &blend,
            .writeMask = WGPUColorWriteMask_All,
        };
        const WGPUFragmentState fragment{
            .module = shader,
            .entryPoint = {"fs_main", WGPU_STRLEN},
            .targetCount = 1,
            .targets = &target,
        };
        const WGPURenderPipelineDescriptor pipeline_descriptor{
            .layout = pipeline_layout,
            .vertex = vertex,
            .primitive =
                {
                    .topology = WGPUPrimitiveTopology_TriangleList,
                },
            .multisample =
                {
                    .count = 1,
                    .mask = 0xFFFFFFFF,
                },
            .fragment = &fragment,
        };
        pipeline = wgpuDeviceCreateRenderPipeline(device, &pipeline_descriptor);
        return pipeline != nullptr;
    }

    bool create() noexcept {
        instance = wgpuCreateInstance(nullptr);
        if (instance == nullptr) {
            return false;
        }

        wgpuInstanceRequestAdapter(
            instance,
            nullptr,
            WGPURequestAdapterCallbackInfo{
                .mode = WGPUCallbackMode_WaitAnyOnly,
                .callback = on_adapter,
                .userdata1 = static_cast<void*>(&adapter),
            }
        );
        if (adapter == nullptr) {
            return false;
        }

        wgpuAdapterRequestDevice(
            adapter,
            nullptr,
            WGPURequestDeviceCallbackInfo{
                .mode = WGPUCallbackMode_WaitAnyOnly,
                .callback = on_device,
                .userdata1 = static_cast<void*>(&device),
            }
        );
        if (device == nullptr) {
            return false;
        }

        queue = wgpuDeviceGetQueue(device);
        valid = queue != nullptr && create_pipeline();
        return valid;
    }

    bool render_rects(
        std::uint32_t width,
        std::uint32_t height,
        std::span<const RectInstance> instances,
        std::span<SoftwarePixel> output
    ) noexcept {
        if (!valid || width == 0 || height == 0 || instances.empty() ||
            output.size() < static_cast<std::size_t>(width) * height) {
            return false;
        }

        const WGPUTextureDescriptor texture_descriptor{
            .usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_CopySrc,
            .dimension = WGPUTextureDimension_2D,
            .size = {width, height, 1},
            .format = WGPUTextureFormat_RGBA8Unorm,
            .mipLevelCount = 1,
            .sampleCount = 1,
        };
        WGPUTexture texture = wgpuDeviceCreateTexture(device, &texture_descriptor);
        if (texture == nullptr) {
            return false;
        }
        WGPUTextureView view = wgpuTextureCreateView(texture, nullptr);
        if (view == nullptr) {
            wgpuTextureRelease(texture);
            return false;
        }

        const auto instance_bytes = static_cast<std::uint64_t>(instances.size()) * 32U;
        const WGPUBufferDescriptor instance_descriptor{
            .usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst,
            .size = instance_bytes,
        };
        WGPUBuffer instance_buffer = wgpuDeviceCreateBuffer(device, &instance_descriptor);
        const WGPUBufferDescriptor uniform_descriptor{
            .usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst,
            .size = 16,
        };
        WGPUBuffer uniform_buffer = wgpuDeviceCreateBuffer(device, &uniform_descriptor);
        if (instance_buffer == nullptr || uniform_buffer == nullptr) {
            if (instance_buffer != nullptr) {
                wgpuBufferRelease(instance_buffer);
            }
            if (uniform_buffer != nullptr) {
                wgpuBufferRelease(uniform_buffer);
            }
            wgpuTextureViewRelease(view);
            wgpuTextureRelease(texture);
            return false;
        }

        wgpuQueueWriteBuffer(
            queue,
            instance_buffer,
            0,
            instances.data(),
            static_cast<std::size_t>(instance_bytes)
        );
        const std::array<float, 4> uniform_data{
            static_cast<float>(width),
            static_cast<float>(height),
            0.0F,
            0.0F,
        };
        wgpuQueueWriteBuffer(queue, uniform_buffer, 0, uniform_data.data(), sizeof(uniform_data));

        const WGPUBindGroupEntry bind_entry{
            .binding = 0,
            .buffer = uniform_buffer,
            .offset = 0,
            .size = 16,
        };
        const WGPUBindGroupDescriptor bind_group_descriptor{
            .layout = bind_group_layout,
            .entryCount = 1,
            .entries = &bind_entry,
        };
        WGPUBindGroup bind_group = wgpuDeviceCreateBindGroup(device, &bind_group_descriptor);
        WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(device, nullptr);
        if (bind_group == nullptr || encoder == nullptr) {
            if (bind_group != nullptr) {
                wgpuBindGroupRelease(bind_group);
            }
            if (encoder != nullptr) {
                wgpuCommandEncoderRelease(encoder);
            }
            wgpuBufferRelease(uniform_buffer);
            wgpuBufferRelease(instance_buffer);
            wgpuTextureViewRelease(view);
            wgpuTextureRelease(texture);
            return false;
        }

        const WGPURenderPassColorAttachment color_attachment{
            .view = view,
            .depthSlice = WGPU_DEPTH_SLICE_UNDEFINED,
            .loadOp = WGPULoadOp_Clear,
            .storeOp = WGPUStoreOp_Store,
            .clearValue = {.r = 0.0F, .g = 0.0F, .b = 0.0F, .a = 0.0F},
        };
        const WGPURenderPassDescriptor pass_descriptor{
            .colorAttachmentCount = 1,
            .colorAttachments = &color_attachment,
        };
        WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &pass_descriptor);
        if (pass == nullptr) {
            wgpuCommandEncoderRelease(encoder);
            wgpuBindGroupRelease(bind_group);
            wgpuBufferRelease(uniform_buffer);
            wgpuBufferRelease(instance_buffer);
            wgpuTextureViewRelease(view);
            wgpuTextureRelease(texture);
            return false;
        }

        wgpuRenderPassEncoderSetPipeline(pass, pipeline);
        wgpuRenderPassEncoderSetBindGroup(pass, 0, bind_group, 0, nullptr);
        wgpuRenderPassEncoderSetVertexBuffer(pass, 0, instance_buffer, 0, instance_bytes);
        wgpuRenderPassEncoderDraw(pass, 6, static_cast<std::uint32_t>(instances.size()), 0, 0);
        wgpuRenderPassEncoderEnd(pass);
        wgpuRenderPassEncoderRelease(pass);
        wgpuBindGroupRelease(bind_group);
        wgpuBufferRelease(uniform_buffer);
        wgpuBufferRelease(instance_buffer);

        const auto pixels_per_row = width * 4U;
        const auto bytes_per_row = (pixels_per_row + 255U) & ~255U;
        const auto buffer_size = static_cast<std::uint64_t>(bytes_per_row) * height;
        const WGPUBufferDescriptor readback_descriptor{
            .usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_MapRead,
            .size = buffer_size,
        };
        WGPUBuffer readback = wgpuDeviceCreateBuffer(device, &readback_descriptor);
        if (readback == nullptr) {
            wgpuCommandEncoderRelease(encoder);
            wgpuTextureViewRelease(view);
            wgpuTextureRelease(texture);
            return false;
        }

        const WGPUTexelCopyTextureInfo copy_source{
            .texture = texture,
            .mipLevel = 0,
            .origin = {0, 0, 0},
            .aspect = WGPUTextureAspect_All,
        };
        const WGPUTexelCopyBufferInfo copy_destination{
            .layout = {.offset = 0, .bytesPerRow = bytes_per_row, .rowsPerImage = height},
            .buffer = readback,
        };
        const WGPUExtent3D copy_extent{width, height, 1};
        wgpuCommandEncoderCopyTextureToBuffer(
            encoder,
            &copy_source,
            &copy_destination,
            &copy_extent
        );

        WGPUCommandBuffer command = wgpuCommandEncoderFinish(encoder, nullptr);
        if (command == nullptr) {
            wgpuBufferRelease(readback);
            wgpuCommandEncoderRelease(encoder);
            wgpuTextureViewRelease(view);
            wgpuTextureRelease(texture);
            return false;
        }
        wgpuQueueSubmit(queue, 1, &command);
        wgpuCommandBufferRelease(command);
        wgpuCommandEncoderRelease(encoder);

        auto map_state = MapState{};
        wgpuBufferMapAsync(
            readback,
            WGPUMapMode_Read,
            0,
            static_cast<std::size_t>(buffer_size),
            WGPUBufferMapCallbackInfo{
                .mode = WGPUCallbackMode_AllowProcessEvents,
                .callback = on_map,
                .userdata1 = &map_state,
            }
        );
        wgpuDevicePoll(device, 1, nullptr);
        if (!map_state.mapped) {
            wgpuBufferRelease(readback);
            wgpuTextureViewRelease(view);
            wgpuTextureRelease(texture);
            return false;
        }

        const auto* pixels =
            static_cast<const std::uint8_t*>(wgpuBufferGetMappedRange(readback, 0, buffer_size));
        auto copied = false;
        if (pixels != nullptr) {
            for (std::uint32_t y = 0; y < height; ++y) {
                const auto* row = pixels + static_cast<std::size_t>(y) * bytes_per_row;
                for (std::uint32_t x = 0; x < width; ++x) {
                    const auto offset = static_cast<std::size_t>(x) * 4;
                    const auto alpha = row[offset + 3];
                    const auto straight = [alpha](std::uint8_t channel) noexcept {
                        if (alpha == 0 || alpha == 255) {
                            return channel;
                        }

                        const auto value =
                            (static_cast<std::uint32_t>(channel) * 255U + alpha / 2U) / alpha;

                        return static_cast<std::uint8_t>(value > 255U ? 255U : value);
                    };
                    output[static_cast<std::size_t>(y) * width + x] = {
                        .red = straight(row[offset]),
                        .green = straight(row[offset + 1]),
                        .blue = straight(row[offset + 2]),
                        .alpha = alpha,
                    };
                }
            }
            copied = true;
        }
        wgpuBufferUnmap(readback);
        wgpuBufferRelease(readback);
        wgpuTextureViewRelease(view);
        wgpuTextureRelease(texture);

        return copied;
    }
};

WebGpuBackend::WebGpuBackend() noexcept : consumer_(*this) {
}

WebGpuBackend::~WebGpuBackend() = default;

bool WebGpuBackend::create() noexcept {
    if (!device_) {
        device_ = std::make_unique<Device>();
        if (!device_->create()) {
            device_.reset();
            return false;
        }
    }

    return valid();
}

bool WebGpuBackend::valid() const noexcept {
    return device_ != nullptr && device_->valid;
}

bool WebGpuBackend::can_submit() const noexcept {
    return !unsupported_ && instance_count_ != 0;
}

void WebGpuBackend::begin_frame(std::uint32_t width, std::uint32_t height) noexcept {
    width_ = width;
    height_ = height;
    instance_count_ = 0;
    unsupported_ = false;
}

void WebGpuBackend::draw(
    const WebGpuBatch& batch,
    std::span<const DisplayCommand> commands
) noexcept {
    if (batch.pipeline != WebGpuPipeline::fill_rect || batch.clip_depth != 0 ||
        batch.start > commands.size() || batch.count > commands.size() - batch.start) {
        unsupported_ = true;
        return;
    }

    for (std::size_t index = batch.start; index < batch.start + batch.count; ++index) {
        const auto* rect = std::get_if<FillRect>(&commands[index]);
        if (rect == nullptr ||
            !valid_rect(rect->bounds.x, rect->bounds.y, rect->bounds.width, rect->bounds.height)) {
            unsupported_ = true;
            return;
        }
        if (instance_count_ == instances_.size()) {
            unsupported_ = true;
            return;
        }

        instances_[instance_count_++] = {
            .x = rect->bounds.x,
            .y = rect->bounds.y,
            .width = rect->bounds.width,
            .height = rect->bounds.height,
            .red = static_cast<float>(rect->color.red) / 255.0F,
            .green = static_cast<float>(rect->color.green) / 255.0F,
            .blue = static_cast<float>(rect->color.blue) / 255.0F,
            .alpha = static_cast<float>(rect->color.alpha) / 255.0F,
        };
    }
}

void WebGpuBackend::end_frame() noexcept {
}

WebGpuFrameResult WebGpuBackend::render(
    std::span<const DisplayCommand> commands,
    std::uint32_t width,
    std::uint32_t height,
    std::span<SoftwarePixel> output
) noexcept {
    if (!valid()) {
        return WebGpuFrameResult::no_device;
    }

    auto plan = WebGpuFramePlan{};
    if (consumer_.submit(commands, width, height, plan) != WebGpuPlanResult::complete ||
        !can_submit()) {
        return WebGpuFrameResult::unsupported;
    }

    if (!device_->render_rects(
            width,
            height,
            std::span<const RectInstance>(instances_.data(), instance_count_),
            output
        )) {
        return WebGpuFrameResult::readback_failed;
    }

    return WebGpuFrameResult::complete;
}

}
