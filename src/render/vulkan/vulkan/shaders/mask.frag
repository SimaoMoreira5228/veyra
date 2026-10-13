#version 450

layout(binding = 0) uniform sampler2D tex;

layout(location = 0) in vec2 inPixel;
layout(location = 1) in vec4 inDest;
layout(location = 2) in vec2 inStart;
layout(location = 3) in vec4 inColor;

layout(location = 0) out vec4 outColor;

void main() {
    ivec2 size = textureSize(tex, 0);
    ivec2 texel = ivec2(inStart) + ivec2(inPixel) - ivec2(inDest.xy);
    texel = clamp(texel, ivec2(0), size - ivec2(1));

    float coverage = texelFetch(tex, texel, 0).r;

    outColor = vec4(inColor.rgb, inColor.a * coverage);
}
