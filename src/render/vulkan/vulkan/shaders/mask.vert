#version 450

layout(location = 0) in vec4 inDest;
layout(location = 1) in vec2 inStart;
layout(location = 2) in vec4 inColor;

layout(push_constant) uniform Push {
    vec2 extent;
} push;

layout(location = 0) out vec2 outPixel;
layout(location = 1) out vec4 outDest;
layout(location = 2) out vec2 outStart;
layout(location = 3) out vec4 outColor;

void main() {
    vec2 pixel = inDest.xy;

    if (gl_VertexIndex == 1 || gl_VertexIndex == 4) {
        pixel = vec2(inDest.z, inDest.y);
    } else if (gl_VertexIndex == 2 || gl_VertexIndex == 3) {
        pixel = vec2(inDest.x, inDest.w);
    } else if (gl_VertexIndex == 5) {
        pixel = inDest.zw;
    }

    outPixel = pixel;
    outDest = inDest;
    outStart = inStart;
    outColor = inColor;

    vec2 ndc = vec2(
        pixel.x / push.extent.x * 2.0 - 1.0,
        pixel.y / push.extent.y * 2.0 - 1.0
    );

    gl_Position = vec4(ndc, 0.0, 1.0);
}
