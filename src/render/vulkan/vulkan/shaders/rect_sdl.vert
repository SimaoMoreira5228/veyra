#version 450

layout(location = 0) in vec4 inBounds;
layout(location = 1) in vec4 inColor;

layout(set = 1, binding = 0) uniform Extent {
    vec2 extent;
};

layout(location = 0) out vec4 outColor;

void main() {
    vec2 pixel = inBounds.xy;

    if (gl_VertexIndex == 1 || gl_VertexIndex == 4) {
        pixel = vec2(inBounds.z, inBounds.y);
    } else if (gl_VertexIndex == 2 || gl_VertexIndex == 3) {
        pixel = vec2(inBounds.x, inBounds.w);
    } else if (gl_VertexIndex == 5) {
        pixel = inBounds.zw;
    }

    outColor = inColor;

    vec2 ndc = vec2(
        pixel.x / extent.x * 2.0 - 1.0,
        1.0 - pixel.y / extent.y * 2.0
    );

    gl_Position = vec4(ndc, 0.0, 1.0);
}
