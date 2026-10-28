#ifndef VEYRA_RENDER_SOFTWARE_RENDERER_HPP
#define VEYRA_RENDER_SOFTWARE_RENDERER_HPP

#include "veyra/foundation/geometry.hpp"
#include "veyra/foundation/color.hpp"
#include "veyra/foundation/image.hpp"

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

namespace veyra::render {
using ::veyra::DisplayColor;
using ::veyra::DisplayCommand;
using ::veyra::DrawGlyph;
using ::veyra::DrawImage;
using ::veyra::DrawMask;
using ::veyra::FillPath;
using ::veyra::FillRect;
using ::veyra::FillRoundedRect;
using ::veyra::PopClip;
using ::veyra::PopTranslation;
using ::veyra::PushClip;
using ::veyra::PushTranslation;
using ::veyra::SoftwareCubicTo;
using ::veyra::SoftwareLineTo;
using ::veyra::SoftwarePathContour;
using ::veyra::SoftwarePathFillRule;
using ::veyra::SoftwarePathSegment;
using ::veyra::SoftwareQuadraticTo;
using ::veyra::SoftwareRenderResult;
using ::veyra::SoftwareRenderWorkspace;
using ::veyra::StrokeLine;
using ::veyra::StrokePolyline;
using ::veyra::StrokeRect;
using ::veyra::StrokeRoundedRect;
using ::veyra::render_software;
}

namespace veyra::render::cpu {
using ::veyra::DisplayColor;
using ::veyra::DisplayCommand;
using ::veyra::DrawGlyph;
using ::veyra::DrawImage;
using ::veyra::DrawMask;
using ::veyra::FillPath;
using ::veyra::FillRect;
using ::veyra::FillRoundedRect;
using ::veyra::PopClip;
using ::veyra::PopTranslation;
using ::veyra::PushClip;
using ::veyra::PushTranslation;
using ::veyra::SoftwareCubicTo;
using ::veyra::SoftwareLineTo;
using ::veyra::SoftwarePathContour;
using ::veyra::SoftwarePathFillRule;
using ::veyra::SoftwarePathSegment;
using ::veyra::SoftwareQuadraticTo;
using ::veyra::SoftwareRenderResult;
using ::veyra::SoftwareRenderWorkspace;
using ::veyra::StrokeLine;
using ::veyra::StrokePolyline;
using ::veyra::StrokeRect;
using ::veyra::StrokeRoundedRect;
using ::veyra::render_software;
}

#endif
