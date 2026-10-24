#include "frame_common.slang"

struct MaskOut {
    float4 position : SV_Position;
    float2 pixel : TEXCOORD0;
    float4 bounds : TEXCOORD1;
    float2 start : TEXCOORD2;
    float4 color : COLOR0;
};

GPU_ROOT(NogaFrameMaskRoot, root);

[shader("vertex")]
MaskOut vertexMain(uint vertex_id : SV_VertexID, uint instance_id : SV_InstanceID) {
    NogaFrameMask item = root.items[instance_id];
    MaskOut output;
    float2 pixel = noga_quad_corner(item.bounds, vertex_id);
    output.position = noga_clip(pixel, root.extent);
    output.pixel = pixel;
    output.bounds = item.bounds;
    output.start = item.start;
    output.color = item.color;

    return output;
}

[shader("fragment")]
float4 fragmentMain(MaskOut input) : SV_Target0 {
    Texture2D<float4> texture = gpu_texture<Texture2D<float4>>(root.texture_index);
    uint width = 0;
    uint height = 0;
    uint levels = 0;
    texture.GetDimensions(0, width, height, levels);
    int2 texel = int2(input.start) + int2(input.pixel) - int2(input.bounds.xy);
    texel = clamp(texel, int2(0, 0), int2(width, height) - int2(1, 1));
    float coverage = texture.Load(int3(texel, 0)).r;

    return float4(input.color.rgb, input.color.a * coverage);
}
