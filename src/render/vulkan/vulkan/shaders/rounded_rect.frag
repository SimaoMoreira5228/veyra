#version 450

layout(location = 0) in vec4 inColor;
layout(location = 1) in vec4 inBounds;
layout(location = 2) in vec2 inPixel;
layout(location = 3) in float inRadius;

layout(location = 0) out vec4 outColor;

void main() {
    vec2 center = 0.5 * (inBounds.xy + inBounds.zw);
    vec2 half_size = 0.5 * (inBounds.zw - inBounds.xy);
    float radius = min(inRadius, min(half_size.x, half_size.y));
    vec2 edge = abs(inPixel - center) - (half_size - radius);
    float distance = length(max(edge, vec2(0.0))) + min(max(edge.x, edge.y), 0.0) - radius;
    float coverage = clamp(0.5 - distance, 0.0, 1.0);

    outColor = vec4(inColor.rgb, inColor.a * coverage);
}
