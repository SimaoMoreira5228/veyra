#ifndef VEYRA_RENDER_SOFTWARE_RENDERER_HPP
#define VEYRA_RENDER_SOFTWARE_RENDERER_HPP

#include "veyra/layout/core/geometry.hpp"
#include "veyra/style/theme/color.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <variant>

namespace veyra {

using DisplayColor = Srgba8;

struct FillRect {
    LayoutRect bounds;
    DisplayColor color;
};

struct FillRoundedRect {
    LayoutRect bounds;
    float radius{};
    DisplayColor color;
};

struct StrokeRect {
    LayoutRect bounds;
    float thickness{};
    DisplayColor color;
};

struct StrokeRoundedRect {
    LayoutRect bounds;
    float radius{};
    float thickness{};
    DisplayColor color;
};

struct StrokeLine {
    LayoutPoint start;
    LayoutPoint end;
    float thickness{};
    DisplayColor color;
};

struct StrokePolyline {
    std::span<const LayoutPoint> points;
    float thickness{};
    DisplayColor color;
    bool closed{};
};

struct SoftwareMask {
    std::size_t width{};
    std::size_t height{};
    std::size_t stride{};
    std::span<const std::uint8_t> coverage;
    std::uint64_t generation{};
};

struct DrawMask {
    float x{};
    float y{};
    const SoftwareMask* mask{};
    DisplayColor color;
};

class GlyphAtlas;

struct DrawGlyph {
    const GlyphAtlas* atlas{};
    std::uint32_t u{};
    std::uint32_t v{};
    std::uint32_t width{};
    std::uint32_t height{};
    float x{};
    float y{};
    DisplayColor color;
};

struct SoftwareLineTo {
    LayoutPoint end;
};

struct SoftwareQuadraticTo {
    LayoutPoint control;
    LayoutPoint end;
};

struct SoftwareCubicTo {
    LayoutPoint control_1;
    LayoutPoint control_2;
    LayoutPoint end;
};

using SoftwarePathSegment = std::variant<SoftwareLineTo, SoftwareQuadraticTo, SoftwareCubicTo>;

struct SoftwarePathContour {
    LayoutPoint start;
    std::span<const SoftwarePathSegment> segments;
};

enum class SoftwarePathFillRule : std::uint8_t {
    even_odd,
    non_zero,
};

struct FillPath {
    std::span<const SoftwarePathContour> contours;
    DisplayColor color;
    SoftwarePathFillRule fill_rule{SoftwarePathFillRule::even_odd};
};

struct PushTranslation {
    LayoutPoint offset;
};

struct PopTranslation {};

struct PushClip {
    LayoutRect bounds;
    std::uint64_t clip_id{};
};

struct PopClip {};

struct SoftwarePixel {
    std::uint8_t red{};
    std::uint8_t green{};
    std::uint8_t blue{};
    std::uint8_t alpha{255};
};

struct SoftwareImage {
    std::size_t width{};
    std::size_t height{};
    std::size_t stride{};
    std::span<const SoftwarePixel> pixels;
    std::uint64_t generation{};
};

enum class SoftwareImageSampling : std::uint8_t {
    nearest,
    linear,
};

struct DrawImage {
    LayoutRect source;
    LayoutRect destination;
    const SoftwareImage* image{};
    SoftwareImageSampling sampling{SoftwareImageSampling::nearest};
    std::uint8_t opacity{255};
};

using DisplayCommand = std::variant<
    FillRect,
    FillRoundedRect,
    StrokeRect,
    StrokeRoundedRect,
    StrokeLine,
    StrokePolyline,
    DrawMask,
    DrawGlyph,
    FillPath,
    DrawImage,
    PushTranslation,
    PopTranslation,
    PushClip,
    PopClip>;

struct SoftwareSurface {
    std::size_t width{};
    std::size_t height{};
    std::size_t stride{};
    std::span<SoftwarePixel> pixels;
};

struct SoftwareClipRect {
    std::size_t left{};
    std::size_t top{};
    std::size_t right{};
    std::size_t bottom{};
};

struct SoftwareRenderWorkspace {
    std::span<SoftwareClipRect> clips;
    std::span<LayoutPoint> translations;
    std::span<double> path_intersections;
    std::span<std::int8_t> path_windings;
};

enum class SoftwareRenderResult : std::uint8_t {
    complete,
    invalid_input,
    invalid_surface,
    insufficient_workspace,
};

SoftwareRenderResult render_software(
    std::span<const DisplayCommand> commands,
    SoftwareSurface surface,
    SoftwareRenderWorkspace workspace
) noexcept;

SoftwareRenderResult render_software(
    std::span<const DisplayCommand> commands,
    SoftwareSurface surface,
    SoftwareRenderWorkspace workspace,
    std::span<const LayoutRect> damage_regions
) noexcept;

}

#endif
