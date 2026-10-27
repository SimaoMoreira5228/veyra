#version 450

layout(location = 0) in vec4 inDest;
layout(location = 1) in vec4 inUV;
layout(location = 2) in float inOpacity;

layout(set = 1, binding = 0) uniform Extent {
    vec2 extent;
};

layout(location = 0) out vec2 outUV;
layout(location = 1) out float outOpacity;

void main() {
    vec2 pixel = inDest.xy;

    if (gl_VertexIndex == 1 || gl_VertexIndex == 4) {
        pixel = vec2(inDest.z, inDest.y);
    } else if (gl_VertexIndex == 2 || gl_VertexIndex == 3) {
        pixel = vec2(inDest.x, inDest.w);
    } else if (gl_VertexIndex == 5) {
        pixel = inDest.zw;
    }

    vec2 uv = inUV.xy;

    if (gl_VertexIndex == 1 || gl_VertexIndex == 4) {
        uv = vec2(inUV.z, inUV.y);
    } else if (gl_VertexIndex == 2 || gl_VertexIndex == 3) {
        uv = vec2(inUV.x, inUV.w);
    } else if (gl_VertexIndex == 5) {
        uv = inUV.zw;
    }

    outUV = uv;
    outOpacity = inOpacity;

    vec2 ndc = vec2(
        pixel.x / extent.x * 2.0 - 1.0,
        1.0 - pixel.y / extent.y * 2.0
    );

    gl_Position = vec4(ndc, 0.0, 1.0);
}
