#include <NoGraphicsAPI/shader.slang>
#include "rect_shared.h"

GPU_ROOT(NogaRectRoot, root);

struct NogaRectVertex {
    float4 position : SV_Position;
    float4 color : COLOR0;
};

[shader("mesh")]
[outputtopology("triangle")]
[numthreads(1, 1, 1)]
void meshMain(
    uint3 group_id : SV_GroupID,
    out vertices NogaRectVertex verts[4],
    out indices uint3 tris[2]
) {
    NogaRect rect = root.rects[group_id.x];
    SetMeshOutputCounts(4, 2);
    float2 min_n = rect.bounds.xy / root.extent * 2.0f - 1.0f;
    float2 max_n = rect.bounds.zw / root.extent * 2.0f - 1.0f;
    verts[0].position = float4(min_n.x, min_n.y, 0.5f, 1.0f);
    verts[1].position = float4(max_n.x, min_n.y, 0.5f, 1.0f);
    verts[2].position = float4(min_n.x, max_n.y, 0.5f, 1.0f);
    verts[3].position = float4(max_n.x, max_n.y, 0.5f, 1.0f);
    verts[0].color = rect.color;
    verts[1].color = rect.color;
    verts[2].color = rect.color;
    verts[3].color = rect.color;
    tris[0] = uint3(0, 1, 2);
    tris[1] = uint3(2, 1, 3);
}

[shader("fragment")]
float4 fragmentMain(NogaRectVertex input) : SV_Target0 {
    return input.color;
}
