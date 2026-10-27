#version 450

layout(location = 0) in vec4 inColor;
layout(location = 1) in vec2 inStart;
layout(location = 2) in vec2 inEnd;
layout(location = 3) in vec2 inPixel;
layout(location = 4) in float inThickness;

layout(location = 0) out vec4 outColor;

void main() {
    vec2 axis = inEnd - inStart;
    float denom = max(dot(axis, axis), 1e-8);
    float along = clamp(dot(inPixel - inStart, axis) / denom, 0.0, 1.0);
    vec2 nearest = inStart + along * axis;
    float distance = length(inPixel - nearest) - inThickness * 0.5;
    float coverage = clamp(0.5 - distance, 0.0, 1.0);

    outColor = vec4(inColor.rgb, inColor.a * coverage);
}
