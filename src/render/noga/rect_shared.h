#pragma once

#include <NoGraphicsAPIUtility/shader_types.h>

struct NogaRect {
    float4 bounds;
    float4 color;
};

struct NogaRectRoot {
    NogaRect* rects;
    float2 extent;
    uint32 count;
    uint32 pad[3];
};

#if !defined(__SLANG__)
static_assert(sizeof(NogaRect) == 32);
static_assert(sizeof(NogaRectRoot) == 32);
static_assert(alignof(NogaRectRoot) == 8);
#endif
