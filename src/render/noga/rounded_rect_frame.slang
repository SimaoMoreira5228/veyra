#include "frame_common.slang"

struct RoundedOut {
    float4 position : SV_Position;
    float4 color : COLOR0;
    float4 shape : TEXCOORD0;
    float2 pixel : TEXCOORD1;
    float radius : TEXCOORD2;
};

GPU_ROOT(NogaFrameRoundedRoot, root);

[shader("vertex")]
RoundedOut vertexMain(uint vertex_id : SV_VertexID, uint instance_id : SV_InstanceID) {
    NogaFrameRounded item = root.items[instance_id];
    RoundedOut output;
    float2 pixel = noga_quad_corner(item.bounds, vertex_id);
    output.position = noga_clip(pixel, root.extent);
    output.color = item.color;
    output.shape = item.shape;
    output.pixel = pixel;
    output.radius = item.radius;

    return output;
}

[shader("fragment")]
float4 fragmentMain(RoundedOut input) : SV_Target0 {
    float coverage = noga_rounded_coverage(input.pixel, input.shape, input.radius);

    return float4(input.color.rgb, input.color.a * coverage);
}
