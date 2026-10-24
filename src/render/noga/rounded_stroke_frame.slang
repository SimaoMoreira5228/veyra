#include "frame_common.slang"

struct RoundedStrokeOut {
    float4 position : SV_Position;
    float4 color : COLOR0;
    float4 shape : TEXCOORD0;
    float2 pixel : TEXCOORD1;
    float radius : TEXCOORD2;
    float thickness : TEXCOORD3;
};

GPU_ROOT(NogaFrameRoundedStrokeRoot, root);

[shader("vertex")]
RoundedStrokeOut vertexMain(uint vertex_id : SV_VertexID, uint instance_id : SV_InstanceID) {
    NogaFrameRoundedStroke item = root.items[instance_id];
    RoundedStrokeOut output;
    float2 pixel = noga_quad_corner(item.bounds, vertex_id);
    output.position = noga_clip(pixel, root.extent);
    output.color = item.color;
    output.shape = item.shape;
    output.pixel = pixel;
    output.radius = item.radius;
    output.thickness = item.thickness;

    return output;
}

[shader("fragment")]
float4 fragmentMain(RoundedStrokeOut input) : SV_Target0 {
    float2 center = input.shape.xy;
    float2 half_size = input.shape.zw;
    float radius = min(input.radius, min(half_size.x, half_size.y));
    float2 edge = abs(input.pixel - center) - (half_size - radius);
    float distance = length(max(edge, float2(0.0f, 0.0f))) + min(max(edge.x, edge.y), 0.0f) - radius;
    float coverage = clamp(input.thickness * 0.5f + 0.5f - abs(distance), 0.0f, 1.0f);

    return float4(input.color.rgb, input.color.a * coverage);
}
