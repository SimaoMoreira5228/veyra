#include "veyra/render/gpu/webgpu/webgpu_backend.hpp"
#include "veyra/render/resource/glyph_atlas.hpp"

#include <webgpu/webgpu.h>
#include <webgpu/wgpu.h>

#include <algorithm>
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
@group(0) @binding(1) var coverage_texture: texture_2d<f32>;
@group(0) @binding(2) var coverage_sampler: sampler;

struct VertexOutput {
    @builtin(position) position: vec4f,
    @location(0) color: vec4f,
    @location(1) local: vec2f,
    @location(2) size: vec2f,
    @location(3) uv: vec2f,
    @location(4) @interpolate(flat) radius: f32,
    @location(5) @interpolate(flat) mode: f32,
};

@vertex
fn vs_main(
    @builtin(vertex_index) vertex_index: u32,
    @location(0) rect: vec4f,
    @location(1) color: vec4f,
    @location(2) uv_rect: vec4f,
    @location(3) radius: f32,
    @location(4) mode: f32
) -> VertexOutput {
    var corners = array<vec2f, 6>(
        vec2f(0.0, 0.0),
        vec2f(1.0, 0.0),
        vec2f(0.0, 1.0),
        vec2f(0.0, 1.0),
        vec2f(1.0, 0.0),
        vec2f(1.0, 1.0)
    );
    let corner = corners[vertex_index];
    let local = corner * rect.zw;
    let point = rect.xy + local;
    let ndc = vec2f(
        point.x / uniforms.size.x * 2.0 - 1.0,
        1.0 - point.y / uniforms.size.y * 2.0
    );

    var output: VertexOutput;
    output.position = vec4f(ndc, 0.0, 1.0);
    output.color = color;
    output.local = local;
    output.size = rect.zw;
    output.uv = mix(uv_rect.xy, uv_rect.zw, corner);
    output.radius = radius;
    output.mode = mode;
    return output;
}

@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4f {
    let texel = textureSample(coverage_texture, coverage_sampler, input.uv);
    var color = input.color.rgb;
    var coverage = input.color.a;
    if (input.mode > 0.5) {
        color = texel.rgb;
        coverage *= texel.a;
    } else {
        coverage *= texel.r;
    }
    let radius = min(input.radius, min(input.size.x, input.size.y) * 0.5);
    if (radius > 0.0) {
        let half_size = input.size * 0.5;
        let point = input.local - half_size;
        let straight = max(half_size - vec2f(radius), vec2f(0.0));
        let outside = abs(point) - straight;
        var distance = 0.0;
        if (outside.y <= 0.0) {
            distance = max(outside.x, outside.y) - radius;
        } else if (outside.x <= 0.0) {
            distance = outside.y - radius;
        } else {
            distance = length(outside) - radius;
        }
        coverage *= clamp(0.5 - distance, 0.0, 1.0);
    }
    if (coverage <= 0.0) {
        discard;
    }

    return vec4f(color, coverage);
}
)";

constexpr char kPathShader[] = R"(
struct VertexOutput {
    @builtin(position) position: vec4f,
    @location(0) color: vec4f,
};

@vertex
fn vs_path(
    @location(0) position: vec2f,
    @location(1) color: vec4f
) -> VertexOutput {
    var output: VertexOutput;
    output.position = vec4f(position, 0.0, 1.0);
    output.color = color;
    return output;
}

@fragment
fn fs_path(input: VertexOutput) -> @location(0) vec4f {
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
    WGPUShaderModule path_shader{};
    WGPURenderPipeline path_stencil_even{};
    WGPURenderPipeline path_stencil_nonzero{};
    WGPURenderPipeline path_cover{};
    WGPUSampler nearest_sampler{};
    WGPUSampler linear_sampler{};
    WGPUTexture white_texture{};
    WGPUTextureView white_view{};
    bool valid{};
    bool path_ready_{};

    ~Device() {
        if (white_view != nullptr) {
            wgpuTextureViewRelease(white_view);
        }
        if (white_texture != nullptr) {
            wgpuTextureRelease(white_texture);
        }
        if (nearest_sampler != nullptr) {
            wgpuSamplerRelease(nearest_sampler);
        }
        if (linear_sampler != nullptr) {
            wgpuSamplerRelease(linear_sampler);
        }
        if (path_cover != nullptr) {
            wgpuRenderPipelineRelease(path_cover);
        }
        if (path_stencil_nonzero != nullptr) {
            wgpuRenderPipelineRelease(path_stencil_nonzero);
        }
        if (path_stencil_even != nullptr) {
            wgpuRenderPipelineRelease(path_stencil_even);
        }
        if (path_shader != nullptr) {
            wgpuShaderModuleRelease(path_shader);
        }
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

    bool create_white_texture() noexcept {
        const WGPUTextureDescriptor texture_descriptor{
            .usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst,
            .dimension = WGPUTextureDimension_2D,
            .size = {1, 1, 1},
            .format = WGPUTextureFormat_R8Unorm,
            .mipLevelCount = 1,
            .sampleCount = 1,
        };
        white_texture = wgpuDeviceCreateTexture(device, &texture_descriptor);
        if (white_texture == nullptr) {
            return false;
        }
        white_view = wgpuTextureCreateView(white_texture, nullptr);
        if (white_view == nullptr) {
            return false;
        }

        constexpr std::uint8_t white = 255;
        const WGPUTexelCopyTextureInfo destination{
            .texture = white_texture,
            .mipLevel = 0,
            .origin = {0, 0, 0},
            .aspect = WGPUTextureAspect_All,
        };
        const WGPUTexelCopyBufferLayout layout{
            .offset = 0,
            .bytesPerRow = 1,
            .rowsPerImage = 1,
        };
        const WGPUExtent3D extent{1, 1, 1};
        wgpuQueueWriteTexture(queue, &destination, &white, 1, &layout, &extent);

        return true;
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

        const std::array<WGPUBindGroupLayoutEntry, 3> entries{
            WGPUBindGroupLayoutEntry{
                .binding = 0,
                .visibility = WGPUShaderStage_Vertex,
                .buffer =
                    {
                        .type = WGPUBufferBindingType_Uniform,
                        .minBindingSize = 16,
                    },
            },
            WGPUBindGroupLayoutEntry{
                .binding = 1,
                .visibility = WGPUShaderStage_Fragment,
                .texture =
                    {
                        .sampleType = WGPUTextureSampleType_Float,
                        .viewDimension = WGPUTextureViewDimension_2D,
                    },
            },
            WGPUBindGroupLayoutEntry{
                .binding = 2,
                .visibility = WGPUShaderStage_Fragment,
                .sampler = {.type = WGPUSamplerBindingType_Filtering},
            },
        };
        const WGPUBindGroupLayoutDescriptor bind_layout_descriptor{
            .entryCount = entries.size(),
            .entries = entries.data(),
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

        const std::array<WGPUVertexAttribute, 5> attributes{
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
            WGPUVertexAttribute{
                .format = WGPUVertexFormat_Float32x4,
                .offset = 32,
                .shaderLocation = 2,
            },
            WGPUVertexAttribute{
                .format = WGPUVertexFormat_Float32,
                .offset = 48,
                .shaderLocation = 3,
            },
            WGPUVertexAttribute{
                .format = WGPUVertexFormat_Float32,
                .offset = 52,
                .shaderLocation = 4,
            },
        };
        const WGPUVertexBufferLayout vertex_buffer{
            .stepMode = WGPUVertexStepMode_Instance,
            .arrayStride = 64,
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
        if (pipeline == nullptr) {
            return false;
        }

        auto nearest = WGPUSamplerDescriptor{
            .addressModeU = WGPUAddressMode_ClampToEdge,
            .addressModeV = WGPUAddressMode_ClampToEdge,
            .addressModeW = WGPUAddressMode_ClampToEdge,
            .magFilter = WGPUFilterMode_Nearest,
            .minFilter = WGPUFilterMode_Nearest,
            .mipmapFilter = WGPUMipmapFilterMode_Nearest,
            .lodMinClamp = 0.0F,
            .lodMaxClamp = 1.0F,
            .maxAnisotropy = 1,
        };
        nearest_sampler = wgpuDeviceCreateSampler(device, &nearest);
        auto linear = nearest;
        linear.magFilter = WGPUFilterMode_Linear;
        linear.minFilter = WGPUFilterMode_Linear;
        linear_sampler = wgpuDeviceCreateSampler(device, &linear);
        return nearest_sampler != nullptr && linear_sampler != nullptr && create_white_texture();
    }

    bool ensure_path_pipelines() noexcept {
        if (path_ready_) {
            return true;
        }
        auto path_source = WGPUShaderSourceWGSL{
            .chain = {.sType = WGPUSType_ShaderSourceWGSL},
            .code = {kPathShader, WGPU_STRLEN},
        };
        const WGPUShaderModuleDescriptor path_shader_descriptor{
            .nextInChain = &path_source.chain,
        };
        path_shader = wgpuDeviceCreateShaderModule(device, &path_shader_descriptor);
        if (path_shader == nullptr) {
            return false;
        }
        const WGPUPipelineLayoutDescriptor path_layout_descriptor{};
        WGPUPipelineLayout path_layout =
            wgpuDeviceCreatePipelineLayout(device, &path_layout_descriptor);
        if (path_layout == nullptr) {
            return false;
        }
        const std::array<WGPUVertexAttribute, 2> path_attributes{
            WGPUVertexAttribute{
                .format = WGPUVertexFormat_Float32x2,
                .offset = 0,
                .shaderLocation = 0,
            },
            WGPUVertexAttribute{
                .format = WGPUVertexFormat_Float32x4,
                .offset = 8,
                .shaderLocation = 1,
            },
        };
        const WGPUVertexBufferLayout path_vertex_buffer{
            .stepMode = WGPUVertexStepMode_Vertex,
            .arrayStride = 24,
            .attributeCount = path_attributes.size(),
            .attributes = path_attributes.data(),
        };
        const WGPUVertexState path_vertex{
            .module = path_shader,
            .entryPoint = {"vs_path", WGPU_STRLEN},
            .bufferCount = 1,
            .buffers = &path_vertex_buffer,
        };
        const WGPUBlendState path_blend{
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
        const WGPUColorTargetState path_target{
            .format = WGPUTextureFormat_RGBA8Unorm,
            .blend = &path_blend,
            .writeMask = WGPUColorWriteMask_All,
        };
        const WGPUFragmentState path_fragment{
            .module = path_shader,
            .entryPoint = {"fs_path", WGPU_STRLEN},
            .targetCount = 1,
            .targets = &path_target,
        };
        const WGPUStencilFaceState cover_face{
            .compare = WGPUCompareFunction_NotEqual,
            .failOp = WGPUStencilOperation_Keep,
            .depthFailOp = WGPUStencilOperation_Keep,
            .passOp = WGPUStencilOperation_Zero,
        };
        const WGPUDepthStencilState cover_depth{
            .format = WGPUTextureFormat_Depth24PlusStencil8,
            .depthWriteEnabled = WGPUOptionalBool_False,
            .depthCompare = WGPUCompareFunction_Always,
            .stencilFront = cover_face,
            .stencilBack = cover_face,
            .stencilReadMask = 0xFF,
            .stencilWriteMask = 0xFF,
        };
        const WGPURenderPipelineDescriptor cover_descriptor{
            .layout = path_layout,
            .vertex = path_vertex,
            .primitive =
                {
                    .topology = WGPUPrimitiveTopology_TriangleList,
                },
            .depthStencil = &cover_depth,
            .multisample =
                {
                    .count = 1,
                    .mask = 0xFFFFFFFF,
                },
            .fragment = &path_fragment,
        };
        path_cover = wgpuDeviceCreateRenderPipeline(device, &cover_descriptor);

        const WGPUStencilFaceState even_face{
            .compare = WGPUCompareFunction_Always,
            .failOp = WGPUStencilOperation_Keep,
            .depthFailOp = WGPUStencilOperation_Keep,
            .passOp = WGPUStencilOperation_Invert,
        };
        const WGPUDepthStencilState even_depth{
            .format = WGPUTextureFormat_Depth24PlusStencil8,
            .depthWriteEnabled = WGPUOptionalBool_False,
            .depthCompare = WGPUCompareFunction_Always,
            .stencilFront = even_face,
            .stencilBack = even_face,
            .stencilReadMask = 0xFF,
            .stencilWriteMask = 0xFF,
        };
        const WGPUColorTargetState no_color_target{
            .format = WGPUTextureFormat_RGBA8Unorm,
            .writeMask = WGPUColorWriteMask_None,
        };
        const WGPUFragmentState no_color_fragment{
            .module = path_shader,
            .entryPoint = {"fs_path", WGPU_STRLEN},
            .targetCount = 1,
            .targets = &no_color_target,
        };
        const WGPURenderPipelineDescriptor even_descriptor{
            .layout = path_layout,
            .vertex = path_vertex,
            .primitive =
                {
                    .topology = WGPUPrimitiveTopology_TriangleList,
                },
            .depthStencil = &even_depth,
            .multisample =
                {
                    .count = 1,
                    .mask = 0xFFFFFFFF,
                },
            .fragment = &no_color_fragment,
        };
        path_stencil_even = wgpuDeviceCreateRenderPipeline(device, &even_descriptor);

        const WGPUStencilFaceState nonzero_front{
            .compare = WGPUCompareFunction_Always,
            .failOp = WGPUStencilOperation_Keep,
            .depthFailOp = WGPUStencilOperation_Keep,
            .passOp = WGPUStencilOperation_IncrementWrap,
        };
        const WGPUStencilFaceState nonzero_back{
            .compare = WGPUCompareFunction_Always,
            .failOp = WGPUStencilOperation_Keep,
            .depthFailOp = WGPUStencilOperation_Keep,
            .passOp = WGPUStencilOperation_DecrementWrap,
        };
        const WGPUDepthStencilState nonzero_depth{
            .format = WGPUTextureFormat_Depth24PlusStencil8,
            .depthWriteEnabled = WGPUOptionalBool_False,
            .depthCompare = WGPUCompareFunction_Always,
            .stencilFront = nonzero_front,
            .stencilBack = nonzero_back,
            .stencilReadMask = 0xFF,
            .stencilWriteMask = 0xFF,
        };
        const WGPURenderPipelineDescriptor nonzero_descriptor{
            .layout = path_layout,
            .vertex = path_vertex,
            .primitive =
                {
                    .topology = WGPUPrimitiveTopology_TriangleList,
                },
            .depthStencil = &nonzero_depth,
            .multisample =
                {
                    .count = 1,
                    .mask = 0xFFFFFFFF,
                },
            .fragment = &no_color_fragment,
        };
        path_stencil_nonzero = wgpuDeviceCreateRenderPipeline(device, &nonzero_descriptor);
        wgpuPipelineLayoutRelease(path_layout);
        if (path_cover == nullptr || path_stencil_even == nullptr ||
            path_stencil_nonzero == nullptr) {
            return false;
        }
        path_ready_ = true;
        return true;
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

    WGPUTexture create_coverage_texture(const SoftwareMask& mask) noexcept {
        if (mask.width == 0 || mask.height == 0 || mask.coverage.empty()) {
            return nullptr;
        }

        const WGPUTextureDescriptor texture_descriptor{
            .usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst,
            .dimension = WGPUTextureDimension_2D,
            .size =
                {static_cast<std::uint32_t>(mask.width),
                 static_cast<std::uint32_t>(mask.height),
                 1},
            .format = WGPUTextureFormat_R8Unorm,
            .mipLevelCount = 1,
            .sampleCount = 1,
        };
        WGPUTexture texture = wgpuDeviceCreateTexture(device, &texture_descriptor);
        if (texture == nullptr) {
            return nullptr;
        }

        const WGPUTexelCopyTextureInfo destination{
            .texture = texture,
            .mipLevel = 0,
            .origin = {0, 0, 0},
            .aspect = WGPUTextureAspect_All,
        };
        const WGPUTexelCopyBufferLayout layout{
            .offset = 0,
            .bytesPerRow = static_cast<std::uint32_t>(mask.stride),
            .rowsPerImage = static_cast<std::uint32_t>(mask.height),
        };
        const WGPUExtent3D extent{
            static_cast<std::uint32_t>(mask.width),
            static_cast<std::uint32_t>(mask.height),
            1,
        };
        wgpuQueueWriteTexture(
            queue,
            &destination,
            mask.coverage.data(),
            mask.coverage.size(),
            &layout,
            &extent
        );

        return texture;
    }

    WGPUTexture create_image_texture(const SoftwareImage& image) noexcept {
        if (image.width == 0 || image.height == 0 || image.pixels.empty()) {
            return nullptr;
        }

        const WGPUTextureDescriptor texture_descriptor{
            .usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst,
            .dimension = WGPUTextureDimension_2D,
            .size =
                {static_cast<std::uint32_t>(image.width),
                 static_cast<std::uint32_t>(image.height),
                 1},
            .format = WGPUTextureFormat_RGBA8Unorm,
            .mipLevelCount = 1,
            .sampleCount = 1,
        };
        WGPUTexture texture = wgpuDeviceCreateTexture(device, &texture_descriptor);
        if (texture == nullptr) {
            return nullptr;
        }

        const WGPUTexelCopyTextureInfo destination{
            .texture = texture,
            .mipLevel = 0,
            .origin = {0, 0, 0},
            .aspect = WGPUTextureAspect_All,
        };
        const WGPUTexelCopyBufferLayout layout{
            .offset = 0,
            .bytesPerRow = static_cast<std::uint32_t>(image.stride * 4U),
            .rowsPerImage = static_cast<std::uint32_t>(image.height),
        };
        const WGPUExtent3D extent{
            static_cast<std::uint32_t>(image.width),
            static_cast<std::uint32_t>(image.height),
            1,
        };
        wgpuQueueWriteTexture(
            queue,
            &destination,
            image.pixels.data(),
            image.pixels.size() * sizeof(SoftwarePixel),
            &layout,
            &extent
        );

        return texture;
    }

    bool render_instances(
        std::uint32_t width,
        std::uint32_t height,
        std::span<const GpuInstance> instances,
        std::span<const PathVertex> path_vertices,
        std::span<const std::uint32_t> path_indices,
        std::span<const DrawRange> ranges,
        std::span<SoftwarePixel> output
    ) noexcept {
        if (!valid || width == 0 || height == 0 || (instances.empty() && path_indices.empty()) ||
            ranges.empty() || output.size() < static_cast<std::size_t>(width) * height) {
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

        const auto instance_bytes = static_cast<std::uint64_t>(instances.size()) * 64U;
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
        WGPUBuffer path_vertex_buffer = nullptr;
        WGPUBuffer path_index_buffer = nullptr;
        if (!path_vertices.empty()) {
            const WGPUBufferDescriptor vertex_descriptor{
                .usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst,
                .size = static_cast<std::uint64_t>(path_vertices.size()) * 24U,
            };
            path_vertex_buffer = wgpuDeviceCreateBuffer(device, &vertex_descriptor);
        }
        if (!path_indices.empty()) {
            const WGPUBufferDescriptor index_descriptor{
                .usage = WGPUBufferUsage_Index | WGPUBufferUsage_CopyDst,
                .size = static_cast<std::uint64_t>(path_indices.size()) * 4U,
            };
            path_index_buffer = wgpuDeviceCreateBuffer(device, &index_descriptor);
        }
        WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(device, nullptr);
        if (instance_buffer == nullptr || uniform_buffer == nullptr || encoder == nullptr ||
            (!path_indices.empty() &&
             (path_vertex_buffer == nullptr || path_index_buffer == nullptr))) {
            if (instance_buffer != nullptr) {
                wgpuBufferRelease(instance_buffer);
            }
            if (uniform_buffer != nullptr) {
                wgpuBufferRelease(uniform_buffer);
            }
            if (path_vertex_buffer != nullptr) {
                wgpuBufferRelease(path_vertex_buffer);
            }
            if (path_index_buffer != nullptr) {
                wgpuBufferRelease(path_index_buffer);
            }
            if (encoder != nullptr) {
                wgpuCommandEncoderRelease(encoder);
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
        if (path_vertex_buffer != nullptr) {
            wgpuQueueWriteBuffer(
                queue,
                path_vertex_buffer,
                0,
                path_vertices.data(),
                path_vertices.size() * sizeof(PathVertex)
            );
        }
        if (path_index_buffer != nullptr) {
            wgpuQueueWriteBuffer(
                queue,
                path_index_buffer,
                0,
                path_indices.data(),
                path_indices.size() * sizeof(std::uint32_t)
            );
        }

        auto textures = std::array<WGPUTexture, WebGpuFramePlan::kMaxBatches>{};
        auto views = std::array<WGPUTextureView, WebGpuFramePlan::kMaxBatches>{};
        auto bind_groups = std::array<WGPUBindGroup, WebGpuFramePlan::kMaxBatches>{};
        auto texture_count = std::size_t{};
        auto bind_count = std::size_t{};
        const auto release_uploads = [&]() noexcept {
            for (auto& group : bind_groups) {
                if (group != nullptr) {
                    wgpuBindGroupRelease(group);
                    group = nullptr;
                }
            }
            for (auto& texture_view : views) {
                if (texture_view != nullptr) {
                    wgpuTextureViewRelease(texture_view);
                    texture_view = nullptr;
                }
            }
            for (auto& uploaded : textures) {
                if (uploaded != nullptr) {
                    wgpuTextureRelease(uploaded);
                    uploaded = nullptr;
                }
            }
        };

        for (std::size_t index = 0; index < ranges.size(); ++index) {
            const auto& range = ranges[index];
            WGPUTextureView texture_view = white_view;
            if (range.texture == DrawTexture::mask) {
                const auto* mask = static_cast<const SoftwareMask*>(range.resource);
                if (mask == nullptr) {
                    release_uploads();
                    wgpuCommandEncoderRelease(encoder);
                    wgpuBufferRelease(uniform_buffer);
                    wgpuBufferRelease(instance_buffer);
                    wgpuTextureViewRelease(view);
                    wgpuTextureRelease(texture);
                    return false;
                }
                auto uploaded = create_coverage_texture(*mask);
                if (uploaded == nullptr || texture_count == textures.size()) {
                    if (uploaded != nullptr) {
                        wgpuTextureRelease(uploaded);
                    }
                    release_uploads();
                    wgpuCommandEncoderRelease(encoder);
                    wgpuBufferRelease(uniform_buffer);
                    wgpuBufferRelease(instance_buffer);
                    wgpuTextureViewRelease(view);
                    wgpuTextureRelease(texture);
                    return false;
                }
                auto uploaded_view = wgpuTextureCreateView(uploaded, nullptr);
                if (uploaded_view == nullptr) {
                    wgpuTextureRelease(uploaded);
                    release_uploads();
                    wgpuCommandEncoderRelease(encoder);
                    wgpuBufferRelease(uniform_buffer);
                    wgpuBufferRelease(instance_buffer);
                    wgpuTextureViewRelease(view);
                    wgpuTextureRelease(texture);
                    return false;
                }
                textures[texture_count++] = uploaded;
                views[texture_count - 1] = uploaded_view;
                texture_view = uploaded_view;
            } else if (range.texture == DrawTexture::glyph) {
                const auto* atlas = static_cast<const GlyphAtlas*>(range.resource);
                if (atlas == nullptr) {
                    release_uploads();
                    wgpuCommandEncoderRelease(encoder);
                    wgpuBufferRelease(uniform_buffer);
                    wgpuBufferRelease(instance_buffer);
                    wgpuTextureViewRelease(view);
                    wgpuTextureRelease(texture);
                    return false;
                }
                auto uploaded = create_coverage_texture(atlas->view());
                if (uploaded == nullptr || texture_count == textures.size()) {
                    if (uploaded != nullptr) {
                        wgpuTextureRelease(uploaded);
                    }
                    release_uploads();
                    wgpuCommandEncoderRelease(encoder);
                    wgpuBufferRelease(uniform_buffer);
                    wgpuBufferRelease(instance_buffer);
                    wgpuTextureViewRelease(view);
                    wgpuTextureRelease(texture);
                    return false;
                }
                auto uploaded_view = wgpuTextureCreateView(uploaded, nullptr);
                if (uploaded_view == nullptr) {
                    wgpuTextureRelease(uploaded);
                    release_uploads();
                    wgpuCommandEncoderRelease(encoder);
                    wgpuBufferRelease(uniform_buffer);
                    wgpuBufferRelease(instance_buffer);
                    wgpuTextureViewRelease(view);
                    wgpuTextureRelease(texture);
                    return false;
                }
                textures[texture_count++] = uploaded;
                views[texture_count - 1] = uploaded_view;
                texture_view = uploaded_view;
            } else if (range.texture == DrawTexture::image) {
                const auto* image = static_cast<const SoftwareImage*>(range.resource);
                if (image == nullptr) {
                    release_uploads();
                    wgpuCommandEncoderRelease(encoder);
                    wgpuBufferRelease(uniform_buffer);
                    wgpuBufferRelease(instance_buffer);
                    wgpuTextureViewRelease(view);
                    wgpuTextureRelease(texture);
                    return false;
                }
                auto uploaded = create_image_texture(*image);
                if (uploaded == nullptr || texture_count == textures.size()) {
                    if (uploaded != nullptr) {
                        wgpuTextureRelease(uploaded);
                    }
                    release_uploads();
                    wgpuCommandEncoderRelease(encoder);
                    wgpuBufferRelease(uniform_buffer);
                    wgpuBufferRelease(instance_buffer);
                    wgpuTextureViewRelease(view);
                    wgpuTextureRelease(texture);
                    return false;
                }
                auto uploaded_view = wgpuTextureCreateView(uploaded, nullptr);
                if (uploaded_view == nullptr) {
                    wgpuTextureRelease(uploaded);
                    release_uploads();
                    wgpuCommandEncoderRelease(encoder);
                    wgpuBufferRelease(uniform_buffer);
                    wgpuBufferRelease(instance_buffer);
                    wgpuTextureViewRelease(view);
                    wgpuTextureRelease(texture);
                    return false;
                }
                textures[texture_count++] = uploaded;
                views[texture_count - 1] = uploaded_view;
                texture_view = uploaded_view;
            }

            if (bind_count == bind_groups.size()) {
                release_uploads();
                wgpuCommandEncoderRelease(encoder);
                wgpuBufferRelease(uniform_buffer);
                wgpuBufferRelease(instance_buffer);
                wgpuTextureViewRelease(view);
                wgpuTextureRelease(texture);
                return false;
            }

            const std::array<WGPUBindGroupEntry, 3> bind_entries{
                WGPUBindGroupEntry{
                    .binding = 0,
                    .buffer = uniform_buffer,
                    .offset = 0,
                    .size = 16,
                },
                WGPUBindGroupEntry{
                    .binding = 1,
                    .textureView = texture_view,
                },
                WGPUBindGroupEntry{
                    .binding = 2,
                    .sampler = range.sampling == SoftwareImageSampling::linear ? linear_sampler
                                                                               : nearest_sampler,
                },
            };
            const WGPUBindGroupDescriptor bind_group_descriptor{
                .layout = bind_group_layout,
                .entryCount = bind_entries.size(),
                .entries = bind_entries.data(),
            };
            bind_groups[bind_count] = wgpuDeviceCreateBindGroup(device, &bind_group_descriptor);
            if (bind_groups[bind_count] == nullptr) {
                release_uploads();
                wgpuCommandEncoderRelease(encoder);
                wgpuBufferRelease(uniform_buffer);
                wgpuBufferRelease(instance_buffer);
                wgpuTextureViewRelease(view);
                wgpuTextureRelease(texture);
                return false;
            }
            ++bind_count;
        }

        auto has_path = false;
        for (const auto& range : ranges) {
            has_path = has_path || range.path;
        }

        WGPUTexture depth_texture = nullptr;
        WGPUTextureView depth_view = nullptr;
        auto depth_attachment = WGPURenderPassDepthStencilAttachment{};
        if (has_path) {
            const WGPUTextureDescriptor depth_descriptor{
                .usage = WGPUTextureUsage_RenderAttachment,
                .dimension = WGPUTextureDimension_2D,
                .size = {width, height, 1},
                .format = WGPUTextureFormat_Depth24PlusStencil8,
                .mipLevelCount = 1,
                .sampleCount = 1,
            };
            depth_texture = wgpuDeviceCreateTexture(device, &depth_descriptor);
            depth_view =
                depth_texture == nullptr ? nullptr : wgpuTextureCreateView(depth_texture, nullptr);
            if (depth_texture == nullptr || depth_view == nullptr) {
                if (depth_view != nullptr) {
                    wgpuTextureViewRelease(depth_view);
                }
                if (depth_texture != nullptr) {
                    wgpuTextureRelease(depth_texture);
                }
                release_uploads();
                wgpuCommandEncoderRelease(encoder);
                wgpuBufferRelease(uniform_buffer);
                wgpuBufferRelease(instance_buffer);
                wgpuTextureViewRelease(view);
                wgpuTextureRelease(texture);
                return false;
            }

            depth_attachment = WGPURenderPassDepthStencilAttachment{
                .view = depth_view,
                .depthLoadOp = WGPULoadOp_Clear,
                .depthStoreOp = WGPUStoreOp_Discard,
                .depthClearValue = 1.0F,
                .stencilLoadOp = WGPULoadOp_Clear,
                .stencilStoreOp = WGPUStoreOp_Discard,
                .stencilClearValue = 0,
            };
        }

        auto first_pass = true;
        for (std::size_t index = 0; index < ranges.size() && index < bind_count; ++index) {
            const auto& range = ranges[index];
            auto left = std::uint32_t{};
            auto top = std::uint32_t{};
            auto right = width;
            auto bottom = height;
            if (range.clipped) {
                const auto x0 =
                    std::clamp(static_cast<double>(range.clip.x), 0.0, static_cast<double>(width));
                const auto y0 =
                    std::clamp(static_cast<double>(range.clip.y), 0.0, static_cast<double>(height));
                const auto x1 = std::clamp(
                    static_cast<double>(range.clip.x) + range.clip.width,
                    0.0,
                    static_cast<double>(width)
                );
                const auto y1 = std::clamp(
                    static_cast<double>(range.clip.y) + range.clip.height,
                    0.0,
                    static_cast<double>(height)
                );
                if (x1 <= x0 || y1 <= y0) {
                    continue;
                }
                left = static_cast<std::uint32_t>(std::floor(x0));
                top = static_cast<std::uint32_t>(std::floor(y0));
                right = static_cast<std::uint32_t>(std::ceil(x1));
                bottom = static_cast<std::uint32_t>(std::ceil(y1));
            }

            const WGPURenderPassColorAttachment color_attachment{
                .view = view,
                .depthSlice = WGPU_DEPTH_SLICE_UNDEFINED,
                .loadOp = first_pass ? WGPULoadOp_Clear : WGPULoadOp_Load,
                .storeOp = WGPUStoreOp_Store,
                .clearValue = {.r = 0.0F, .g = 0.0F, .b = 0.0F, .a = 0.0F},
            };
            const WGPURenderPassDescriptor pass_descriptor{
                .colorAttachmentCount = 1,
                .colorAttachments = &color_attachment,
                .depthStencilAttachment =
                    range.path && depth_view != nullptr ? &depth_attachment : nullptr,
            };
            WGPURenderPassEncoder pass =
                wgpuCommandEncoderBeginRenderPass(encoder, &pass_descriptor);
            if (pass == nullptr) {
                if (depth_view != nullptr) {
                    wgpuTextureViewRelease(depth_view);
                }
                if (depth_texture != nullptr) {
                    wgpuTextureRelease(depth_texture);
                }
                release_uploads();
                wgpuCommandEncoderRelease(encoder);
                wgpuBufferRelease(uniform_buffer);
                wgpuBufferRelease(instance_buffer);
                wgpuTextureViewRelease(view);
                wgpuTextureRelease(texture);
                return false;
            }
            first_pass = false;

            wgpuRenderPassEncoderSetScissorRect(pass, left, top, right - left, bottom - top);
            if (range.path) {
                const auto path_bytes =
                    static_cast<std::uint64_t>(path_vertices.size()) * sizeof(PathVertex);
                wgpuRenderPassEncoderSetPipeline(
                    pass,
                    range.path_fill_rule == SoftwarePathFillRule::even_odd ? path_stencil_even
                                                                           : path_stencil_nonzero
                );
                wgpuRenderPassEncoderSetVertexBuffer(pass, 0, path_vertex_buffer, 0, path_bytes);
                wgpuRenderPassEncoderSetIndexBuffer(
                    pass,
                    path_index_buffer,
                    WGPUIndexFormat_Uint32,
                    0,
                    WGPU_WHOLE_SIZE
                );
                wgpuRenderPassEncoderDrawIndexed(
                    pass,
                    range.path_index_count,
                    1,
                    range.path_index_first,
                    0,
                    0
                );

                wgpuRenderPassEncoderSetStencilReference(pass, 0);
                wgpuRenderPassEncoderSetPipeline(pass, path_cover);
                wgpuRenderPassEncoderSetVertexBuffer(pass, 0, path_vertex_buffer, 0, path_bytes);
                wgpuRenderPassEncoderDraw(pass, 3, 1, range.path_cover_first, 0);
            } else {
                wgpuRenderPassEncoderSetPipeline(pass, pipeline);
                wgpuRenderPassEncoderSetBindGroup(pass, 0, bind_groups[index], 0, nullptr);
                wgpuRenderPassEncoderSetVertexBuffer(pass, 0, instance_buffer, 0, instance_bytes);
                wgpuRenderPassEncoderDraw(
                    pass,
                    6,
                    static_cast<std::uint32_t>(range.count),
                    0,
                    static_cast<std::uint32_t>(range.first)
                );
            }
            wgpuRenderPassEncoderEnd(pass);
            wgpuRenderPassEncoderRelease(pass);
        }
        release_uploads();
        if (path_index_buffer != nullptr) {
            wgpuBufferRelease(path_index_buffer);
            path_index_buffer = nullptr;
        }
        if (path_vertex_buffer != nullptr) {
            wgpuBufferRelease(path_vertex_buffer);
            path_vertex_buffer = nullptr;
        }

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
            if (depth_view != nullptr) {
                wgpuTextureViewRelease(depth_view);
            }
            if (depth_texture != nullptr) {
                wgpuTextureRelease(depth_texture);
            }
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
            if (depth_view != nullptr) {
                wgpuTextureViewRelease(depth_view);
            }
            if (depth_texture != nullptr) {
                wgpuTextureRelease(depth_texture);
            }
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
            if (depth_view != nullptr) {
                wgpuTextureViewRelease(depth_view);
            }
            if (depth_texture != nullptr) {
                wgpuTextureRelease(depth_texture);
            }
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
        if (depth_view != nullptr) {
            wgpuTextureViewRelease(depth_view);
        }
        if (depth_texture != nullptr) {
            wgpuTextureRelease(depth_texture);
        }
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
    return !unsupported_ && (instance_count_ != 0 || path_index_count_ != 0) && range_count_ != 0;
}

void WebGpuBackend::begin_frame(std::uint32_t width, std::uint32_t height) noexcept {
    width_ = width;
    height_ = height;
    instance_count_ = 0;
    range_count_ = 0;
    path_vertex_count_ = 0;
    path_index_count_ = 0;
    unsupported_ = false;
}

void WebGpuBackend::draw(
    const WebGpuBatch& batch,
    std::span<const DisplayCommand> commands
) noexcept {
    if (batch.start > commands.size() || batch.count > commands.size() - batch.start) {
        unsupported_ = true;
        return;
    }
    if (range_count_ == ranges_.size()) {
        unsupported_ = true;
        return;
    }

    if (batch.pipeline == WebGpuPipeline::fill_path) {
        if (batch.clipped &&
            (batch.clip_bounds.width <= 0.0F || batch.clip_bounds.height <= 0.0F)) {
            return;
        }

        if (!device_->ensure_path_pipelines()) {
            unsupported_ = true;
            return;
        }

        for (std::size_t command_index = batch.start; command_index < batch.start + batch.count;
             ++command_index) {
            const auto* path = std::get_if<FillPath>(&commands[command_index]);
            if (path == nullptr) {
                unsupported_ = true;
                return;
            }
            if (path->color.alpha == 0) {
                continue;
            }
            if (range_count_ == ranges_.size()) {
                unsupported_ = true;
                return;
            }

            const auto red = static_cast<float>(path->color.red) / 255.0F;
            const auto green = static_cast<float>(path->color.green) / 255.0F;
            const auto blue = static_cast<float>(path->color.blue) / 255.0F;
            const auto alpha = static_cast<float>(path->color.alpha) / 255.0F;
            const auto index_first = static_cast<std::uint32_t>(path_index_count_);
            const auto append_vertex = [&](float x, float y) noexcept {
                if (path_vertex_count_ == path_vertices_.size()) {
                    return false;
                }
                path_vertices_[path_vertex_count_++] = {
                    .x = x,
                    .y = y,
                    .red = red,
                    .green = green,
                    .blue = blue,
                    .alpha = alpha,
                };
                return true;
            };

            for (const auto& contour : path->contours) {
                const auto contour_start = path_vertex_count_;
                auto point_count = std::size_t{};
                if (!append_vertex(
                        contour.start.x / static_cast<float>(width_) * 2.0F - 1.0F,
                        1.0F - contour.start.y / static_cast<float>(height_) * 2.0F
                    )) {
                    unsupported_ = true;
                    return;
                }
                ++point_count;
                auto current = contour.start;
                for (const auto& segment : contour.segments) {
                    if (const auto* line = std::get_if<SoftwareLineTo>(&segment)) {
                        if (!append_vertex(
                                line->end.x / static_cast<float>(width_) * 2.0F - 1.0F,
                                1.0F - line->end.y / static_cast<float>(height_) * 2.0F
                            )) {
                            unsupported_ = true;
                            return;
                        }
                        current = line->end;
                        ++point_count;
                        continue;
                    }
                    if (const auto* quadratic = std::get_if<SoftwareQuadraticTo>(&segment)) {
                        for (std::uint32_t step = 1; step <= 16; ++step) {
                            const auto t = static_cast<float>(step) / 16.0F;
                            const auto inverse = 1.0F - t;
                            const auto point = LayoutPoint{
                                .x = inverse * inverse * current.x +
                                    2.0F * inverse * t * quadratic->control.x +
                                    t * t * quadratic->end.x,
                                .y = inverse * inverse * current.y +
                                    2.0F * inverse * t * quadratic->control.y +
                                    t * t * quadratic->end.y,
                            };
                            if (!append_vertex(
                                    point.x / static_cast<float>(width_) * 2.0F - 1.0F,
                                    1.0F - point.y / static_cast<float>(height_) * 2.0F
                                )) {
                                unsupported_ = true;
                                return;
                            }
                            ++point_count;
                        }
                        current = quadratic->end;
                        continue;
                    }
                    const auto* cubic = std::get_if<SoftwareCubicTo>(&segment);
                    if (cubic == nullptr) {
                        unsupported_ = true;
                        return;
                    }
                    for (std::uint32_t step = 1; step <= 24; ++step) {
                        const auto t = static_cast<float>(step) / 24.0F;
                        const auto inverse = 1.0F - t;
                        const auto point = LayoutPoint{
                            .x = inverse * inverse * inverse * current.x +
                                3.0F * inverse * inverse * t * cubic->control_1.x +
                                3.0F * inverse * t * t * cubic->control_2.x +
                                t * t * t * cubic->end.x,
                            .y = inverse * inverse * inverse * current.y +
                                3.0F * inverse * inverse * t * cubic->control_1.y +
                                3.0F * inverse * t * t * cubic->control_2.y +
                                t * t * t * cubic->end.y,
                        };
                        if (!append_vertex(
                                point.x / static_cast<float>(width_) * 2.0F - 1.0F,
                                1.0F - point.y / static_cast<float>(height_) * 2.0F
                            )) {
                            unsupported_ = true;
                            return;
                        }
                        ++point_count;
                    }
                    current = cubic->end;
                }

                for (std::size_t point = 1; point + 1 < point_count; ++point) {
                    if (path_index_count_ + 3 > path_indices_.size()) {
                        unsupported_ = true;
                        return;
                    }
                    path_indices_[path_index_count_++] = static_cast<std::uint32_t>(contour_start);
                    path_indices_[path_index_count_++] =
                        static_cast<std::uint32_t>(contour_start + point);
                    path_indices_[path_index_count_++] =
                        static_cast<std::uint32_t>(contour_start + point + 1);
                }
            }

            if (path_index_count_ == index_first) {
                continue;
            }

            const auto cover_first = static_cast<std::uint32_t>(path_vertex_count_);
            if (!append_vertex(-1.0F, -1.0F) || !append_vertex(3.0F, -1.0F) ||
                !append_vertex(-1.0F, 3.0F)) {
                unsupported_ = true;
                return;
            }

            ranges_[range_count_++] = {
                .first = 0,
                .count = 0,
                .clipped = batch.clipped,
                .clip = batch.clip_bounds,
                .path = true,
                .path_index_first = index_first,
                .path_index_count = static_cast<std::uint32_t>(path_index_count_ - index_first),
                .path_cover_first = cover_first,
                .path_fill_rule = path->fill_rule,
            };
        }
        return;
    }

    auto texture = DrawTexture::white;
    const void* resource = nullptr;
    if (batch.pipeline == WebGpuPipeline::fill_rect ||
        batch.pipeline == WebGpuPipeline::fill_rounded_rect) {
        texture = DrawTexture::white;
    } else if (batch.pipeline == WebGpuPipeline::draw_mask) {
        texture = DrawTexture::mask;
        resource = batch.resource;
    } else if (batch.pipeline == WebGpuPipeline::draw_glyph) {
        texture = DrawTexture::glyph;
        resource = batch.resource;
    } else if (batch.pipeline == WebGpuPipeline::draw_image) {
        texture = DrawTexture::image;
        resource = batch.resource;
    } else {
        unsupported_ = true;
        return;
    }

    if (batch.clipped && (batch.clip_bounds.width <= 0.0F || batch.clip_bounds.height <= 0.0F)) {
        return;
    }

    const auto first = instance_count_;
    for (std::size_t index = batch.start; index < batch.start + batch.count; ++index) {
        auto x = 0.0F;
        auto y = 0.0F;
        auto width = 0.0F;
        auto height = 0.0F;
        auto radius = 0.0F;
        auto mode = 0.0F;
        auto u0 = 0.0F;
        auto v0 = 0.0F;
        auto u1 = 1.0F;
        auto v1 = 1.0F;
        auto color = DisplayColor{};

        if (batch.pipeline == WebGpuPipeline::fill_rect) {
            const auto* rect = std::get_if<FillRect>(&commands[index]);
            if (rect == nullptr) {
                unsupported_ = true;
                return;
            }
            x = rect->bounds.x;
            y = rect->bounds.y;
            width = rect->bounds.width;
            height = rect->bounds.height;
            color = rect->color;
        } else if (batch.pipeline == WebGpuPipeline::fill_rounded_rect) {
            const auto* rect = std::get_if<FillRoundedRect>(&commands[index]);
            if (rect == nullptr) {
                unsupported_ = true;
                return;
            }
            x = rect->bounds.x;
            y = rect->bounds.y;
            width = rect->bounds.width;
            height = rect->bounds.height;
            radius = rect->radius;
            color = rect->color;
        } else if (batch.pipeline == WebGpuPipeline::draw_mask) {
            const auto* mask = std::get_if<DrawMask>(&commands[index]);
            if (mask == nullptr || mask->mask == nullptr || mask->mask->width == 0 ||
                mask->mask->height == 0) {
                unsupported_ = true;
                return;
            }
            x = mask->x;
            y = mask->y;
            width = static_cast<float>(mask->mask->width);
            height = static_cast<float>(mask->mask->height);
            color = mask->color;
        } else if (batch.pipeline == WebGpuPipeline::draw_image) {
            const auto* image = std::get_if<DrawImage>(&commands[index]);
            if (image == nullptr || image->image == nullptr || image->image->width == 0 ||
                image->image->height == 0) {
                unsupported_ = true;
                return;
            }
            const auto image_width = static_cast<float>(image->image->width);
            const auto image_height = static_cast<float>(image->image->height);
            x = image->destination.x;
            y = image->destination.y;
            width = image->destination.width;
            height = image->destination.height;
            u0 = image->source.x / image_width;
            v0 = image->source.y / image_height;
            u1 = (image->source.x + image->source.width) / image_width;
            v1 = (image->source.y + image->source.height) / image_height;
            color = {
                .red = 255,
                .green = 255,
                .blue = 255,
                .alpha = image->opacity,
            };
            mode = 1.0F;
        } else {
            const auto* glyph = std::get_if<DrawGlyph>(&commands[index]);
            if (glyph == nullptr || glyph->atlas == nullptr || glyph->atlas->width() == 0 ||
                glyph->atlas->height() == 0) {
                unsupported_ = true;
                return;
            }
            const auto atlas_width = static_cast<float>(glyph->atlas->width());
            const auto atlas_height = static_cast<float>(glyph->atlas->height());
            x = glyph->x;
            y = glyph->y;
            width = static_cast<float>(glyph->width);
            height = static_cast<float>(glyph->height);
            u0 = static_cast<float>(glyph->u) / atlas_width;
            v0 = static_cast<float>(glyph->v) / atlas_height;
            u1 = static_cast<float>(glyph->u + glyph->width) / atlas_width;
            v1 = static_cast<float>(glyph->v + glyph->height) / atlas_height;
            color = glyph->color;
        }

        if (!valid_rect(x, y, width, height) || !std::isfinite(radius) || radius < 0.0F) {
            unsupported_ = true;
            return;
        }
        if (instance_count_ == instances_.size()) {
            unsupported_ = true;
            return;
        }

        instances_[instance_count_++] = {
            .x = x,
            .y = y,
            .width = width,
            .height = height,
            .red = static_cast<float>(color.red) / 255.0F,
            .green = static_cast<float>(color.green) / 255.0F,
            .blue = static_cast<float>(color.blue) / 255.0F,
            .alpha = static_cast<float>(color.alpha) / 255.0F,
            .u0 = u0,
            .v0 = v0,
            .u1 = u1,
            .v1 = v1,
            .radius = radius,
            .mode = mode,
        };
    }

    ranges_[range_count_++] = {
        .first = first,
        .count = instance_count_ - first,
        .clipped = batch.clipped,
        .clip = batch.clip_bounds,
        .texture = texture,
        .resource = resource,
        .sampling = batch.sampling,
    };
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

    if (!device_->render_instances(
            width,
            height,
            std::span<const GpuInstance>(instances_.data(), instance_count_),
            std::span<const PathVertex>(path_vertices_.data(), path_vertex_count_),
            std::span<const std::uint32_t>(path_indices_.data(), path_index_count_),
            std::span<const DrawRange>(ranges_.data(), range_count_),
            output
        )) {
        return WebGpuFrameResult::readback_failed;
    }

    return WebGpuFrameResult::complete;
}
}
