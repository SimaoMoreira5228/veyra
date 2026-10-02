#ifndef VEYRA_RENDER_SOFTWARE_RENDERER_HPP
#define VEYRA_RENDER_SOFTWARE_RENDERER_HPP

#include "veyra/layout/geometry.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <variant>

namespace veyra {

struct DisplayColor {
    std::uint8_t red{};
    std::uint8_t green{};
    std::uint8_t blue{};
    std::uint8_t alpha{255};
};

struct FillRect {
    LayoutRect bounds;
    DisplayColor color;
};

struct StrokeRect {
    LayoutRect bounds;
    float thickness{};
    DisplayColor color;
};

struct StrokeLine {
    LayoutPoint start;
    LayoutPoint end;
    float thickness{};
    DisplayColor color;
};

struct SoftwareMask {
    std::size_t width{};
    std::size_t height{};
    std::size_t stride{};
    std::span<const std::uint8_t> coverage;
};

struct DrawMask {
    std::int32_t x{};
    std::int32_t y{};
    const SoftwareMask* mask{};
    DisplayColor color;
};

struct PushClip {
    LayoutRect bounds;
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
};

struct DrawImage {
    std::int32_t x{};
    std::int32_t y{};
    const SoftwareImage* image{};
};

using DisplayCommand =
    std::variant<FillRect, StrokeRect, StrokeLine, DrawMask, DrawImage, PushClip, PopClip>;

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
