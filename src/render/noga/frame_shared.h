#pragma once

#include <NoGraphicsAPIUtility/shader_types.h>

struct NogaFrameRect {
    float4 bounds;
    float4 color;
};

struct NogaFrameRounded {
    float4 bounds;
    float4 color;
    float4 shape;
    float radius;
    float pad_a;
    float pad_b;
    float pad_c;
};

struct NogaFrameImage {
    float4 bounds;
    float4 uv;
    float opacity;
    float pad_a;
    float pad_b;
    float pad_c;
};

struct NogaFrameMask {
    float4 bounds;
    float2 start;
    float2 pad_a;
    float4 color;
};

struct NogaFrameStroke {
    float4 bounds;
    float4 color;
    float4 segment;
    float thickness;
    float pad_a;
    float pad_b;
    float pad_c;
};

struct NogaFrameRoundedStroke {
    float4 bounds;
    float4 color;
    float4 shape;
    float radius;
    float thickness;
    float pad_b;
    float pad_c;
};

struct NogaFrameGlyph {
    float4 bounds;
    float4 color;
    float4 glyph;
};

struct NogaFrameRectRoot {
    NogaFrameRect* items;
    float2 extent;
    uint32 pad[4];
};

struct NogaFrameRoundedRoot {
    NogaFrameRounded* items;
    float2 extent;
    uint32 pad[4];
};

struct NogaFrameImageRoot {
    NogaFrameImage* items;
    float2 extent;
    uint32 texture_index;
    uint32 sampler_index;
    uint32 pad[2];
};

struct NogaFrameMaskRoot {
    NogaFrameMask* items;
    float2 extent;
    uint32 texture_index;
    uint32 pad[3];
};

struct NogaFrameStrokeRoot {
    NogaFrameStroke* items;
    float2 extent;
    uint32 pad[4];
};

struct NogaFrameRoundedStrokeRoot {
    NogaFrameRoundedStroke* items;
    float2 extent;
    uint32 pad[4];
};

struct NogaFrameGlyphRoot {
    NogaFrameGlyph* items;
    float2 extent;
    uint32 texture_index;
    uint32 pad[3];
};

#if !defined(__SLANG__)
static_assert(sizeof(NogaFrameRect) == 32);
static_assert(sizeof(NogaFrameRounded) == 64);
static_assert(sizeof(NogaFrameImage) == 48);
static_assert(sizeof(NogaFrameMask) == 48);
static_assert(sizeof(NogaFrameStroke) == 64);
static_assert(sizeof(NogaFrameRoundedStroke) == 64);
static_assert(sizeof(NogaFrameGlyph) == 48);
static_assert(sizeof(NogaFrameRectRoot) == 32);
static_assert(sizeof(NogaFrameImageRoot) == 32);
static_assert(alignof(NogaFrameRectRoot) == 8);
#endif
