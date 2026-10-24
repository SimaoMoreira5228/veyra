#include "frame_common.slang"

struct RectOut {
    float4 position : SV_Position;
    float4 color : COLOR0;
};

GPU_ROOT(NogaFrameRectRoot, root);

[shader("vertex")]
RectOut vertexMain(uint vertex_id : SV_VertexID, uint instance_id : SV_InstanceID) {
    NogaFrameRect item = root.items[instance_id];
    RectOut output;
    output.position = noga_clip(noga_quad_corner(item.bounds, vertex_id), root.extent);
    output.color = item.color;

    return output;
}

[shader("fragment")]
float4 fragmentMain(RectOut input) : SV_Target0 {
    return input.color;
}
