#version 450

layout(location = 0) in vec4 inBounds;
layout(location = 1) in vec4 inColor;
layout(location = 2) in vec4 inGlyph;

layout(push_constant) uniform Push {
    vec2 extent;
} push;

layout(location = 0) out vec4 outColor;
layout(location = 1) out vec2 outPixel;
layout(location = 2) out vec4 outDest;
layout(location = 3) out vec4 outGlyph;

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
    outPixel = pixel;
    outDest = inBounds;
    outGlyph = inGlyph;

    vec2 ndc = vec2(
        pixel.x / push.extent.x * 2.0 - 1.0,
        pixel.y / push.extent.y * 2.0 - 1.0
    );

    gl_Position = vec4(ndc, 0.0, 1.0);
}
