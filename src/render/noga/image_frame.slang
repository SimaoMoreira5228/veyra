#include "frame_common.slang"

struct ImageOut {
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
    float opacity : TEXCOORD1;
};

GPU_ROOT(NogaFrameImageRoot, root);

[shader("vertex")]
ImageOut vertexMain(uint vertex_id : SV_VertexID, uint instance_id : SV_InstanceID) {
    NogaFrameImage item = root.items[instance_id];
    ImageOut output;
    output.position = noga_clip(noga_quad_corner(item.bounds, vertex_id), root.extent);
    output.uv = noga_quad_uv(item.uv, vertex_id);
    output.opacity = item.opacity;

    return output;
}

[shader("fragment")]
float4 fragmentMain(ImageOut input) : SV_Target0 {
    Texture2D<float4> texture = gpu_texture<Texture2D<float4>>(root.texture_index);
    SamplerState sampler = gpu_sampler(root.sampler_index);
    float4 texel = texture.Sample(sampler, input.uv);

    return float4(texel.rgb, texel.a * input.opacity);
}
