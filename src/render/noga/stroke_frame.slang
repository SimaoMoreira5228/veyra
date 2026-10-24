#include "frame_common.slang"

struct StrokeOut {
    float4 position : SV_Position;
    float4 color : COLOR0;
    float2 start : TEXCOORD0;
    float2 end : TEXCOORD1;
    float2 pixel : TEXCOORD2;
    float thickness : TEXCOORD3;
};

GPU_ROOT(NogaFrameStrokeRoot, root);

[shader("vertex")]
StrokeOut vertexMain(uint vertex_id : SV_VertexID, uint instance_id : SV_InstanceID) {
    NogaFrameStroke item = root.items[instance_id];
    StrokeOut output;
    float2 pixel = noga_quad_corner(item.bounds, vertex_id);
    output.position = noga_clip(pixel, root.extent);
    output.color = item.color;
    output.start = item.segment.xy;
    output.end = item.segment.zw;
    output.pixel = pixel;
    output.thickness = item.thickness;

    return output;
}

[shader("fragment")]
float4 fragmentMain(StrokeOut input) : SV_Target0 {
    float2 axis = input.end - input.start;
    float denom = max(dot(axis, axis), 1e-8f);
    float along = clamp(dot(input.pixel - input.start, axis) / denom, 0.0f, 1.0f);
    float2 nearest = input.start + along * axis;
    float distance = length(input.pixel - nearest) - input.thickness * 0.5f;
    float coverage = clamp(0.5f - distance, 0.0f, 1.0f);

    return float4(input.color.rgb, input.color.a * coverage);
}
