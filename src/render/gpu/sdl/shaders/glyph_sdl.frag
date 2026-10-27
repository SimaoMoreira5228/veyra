#version 450

layout(set = 2, binding = 0) uniform sampler2D tex;

layout(location = 0) in vec4 inColor;
layout(location = 1) in vec2 inPixel;
layout(location = 2) in vec4 inDest;
layout(location = 3) in vec4 inGlyph;

layout(location = 0) out vec4 outColor;

void main() {
    ivec2 origin = ivec2(inGlyph.xy);
    ivec2 texel = origin + ivec2(inPixel) - ivec2(inDest.xy);
    texel = clamp(texel, origin, origin + ivec2(inGlyph.zw) - ivec2(1));

    float coverage = texelFetch(tex, texel, 0).r;

    outColor = vec4(inColor.rgb, inColor.a * coverage);
}
