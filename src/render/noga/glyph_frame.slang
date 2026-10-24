#include "frame_common.slang"

struct GlyphOut {
    float4 position : SV_Position;
    float4 color : COLOR0;
    float2 pixel : TEXCOORD0;
    float4 bounds : TEXCOORD1;
    float4 glyph : TEXCOORD2;
};

GPU_ROOT(NogaFrameGlyphRoot, root);

[shader("vertex")]
GlyphOut vertexMain(uint vertex_id : SV_VertexID, uint instance_id : SV_InstanceID) {
    NogaFrameGlyph item = root.items[instance_id];
    GlyphOut output;
    float2 pixel = noga_quad_corner(item.bounds, vertex_id);
    output.position = noga_clip(pixel, root.extent);
    output.color = item.color;
    output.pixel = pixel;
    output.bounds = item.bounds;
    output.glyph = item.glyph;

    return output;
}

[shader("fragment")]
float4 fragmentMain(GlyphOut input) : SV_Target0 {
    Texture2D<float4> texture = gpu_texture<Texture2D<float4>>(root.texture_index);
    uint width = 0;
    uint height = 0;
    uint levels = 0;
    texture.GetDimensions(0, width, height, levels);
    int2 origin = int2(input.glyph.xy);
    int2 texel = origin + int2(input.pixel) - int2(input.bounds.xy);
    texel = clamp(texel, origin, origin + int2(input.glyph.zw) - int2(1, 1));
    float coverage = texture.Load(int3(texel, 0)).r;

    return float4(input.color.rgb, input.color.a * coverage);
}
