#ifndef VEYRA_RENDER_SOFTWARE_RENDERER_HPP
#define VEYRA_RENDER_SOFTWARE_RENDERER_HPP

#include "veyra/layout/geometry.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

namespace veyra {

struct DisplayColor {
    std::uint8_t red{};
    std::uint8_t green{};
    std::uint8_t blue{};
    std::uint8_t alpha{255};
};

enum class DisplayCommandType : std::uint8_t {
    fill_rect,
    push_clip,
    pop_clip,
};

struct DisplayCommand {
    DisplayCommandType type{DisplayCommandType::fill_rect};
    LayoutRect bounds;
    DisplayColor color;
};

struct SoftwarePixel {
    std::uint8_t red{};
    std::uint8_t green{};
    std::uint8_t blue{};
    std::uint8_t alpha{255};
};

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
