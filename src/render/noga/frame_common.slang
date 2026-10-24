#include <NoGraphicsAPI/shader.slang>
#include "frame_shared.h"

float2 noga_quad_corner(float4 bounds, uint vertex_id) {
    float2 pixel = bounds.xy;

    if (vertex_id == 1 || vertex_id == 4) {
        pixel = float2(bounds.z, bounds.y);
    } else if (vertex_id == 2 || vertex_id == 3) {
        pixel = float2(bounds.x, bounds.w);
    } else if (vertex_id == 5) {
        pixel = bounds.zw;
    }

    return pixel;
}

float2 noga_quad_uv(float4 uv, uint vertex_id) {
    float2 sampled = uv.xy;

    if (vertex_id == 1 || vertex_id == 4) {
        sampled = float2(uv.z, uv.y);
    } else if (vertex_id == 2 || vertex_id == 3) {
        sampled = float2(uv.x, uv.w);
    } else if (vertex_id == 5) {
        sampled = uv.zw;
    }

    return sampled;
}

float4 noga_clip(float2 pixel, float2 extent) {
    float2 ndc = pixel / extent * 2.0f - 1.0f;

    return float4(ndc, 0.5f, 1.0f);
}

float noga_rounded_coverage(float2 pixel, float4 shape, float radius) {
    float2 center = shape.xy;
    float2 half_size = shape.zw;
    float clamped = min(radius, min(half_size.x, half_size.y));
    float2 edge = abs(pixel - center) - (half_size - clamped);
    float distance = length(max(edge, float2(0.0f, 0.0f))) + min(max(edge.x, edge.y), 0.0f) - clamped;

    return clamp(0.5f - distance, 0.0f, 1.0f);
}
