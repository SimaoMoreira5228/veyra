#version 450

layout(binding = 0) uniform sampler2D tex;

layout(location = 0) in vec2 inUV;
layout(location = 1) in float inOpacity;

layout(location = 0) out vec4 outColor;

void main() {
    vec4 texel = texture(tex, inUV);

    outColor = vec4(texel.rgb, texel.a * inOpacity);
}
