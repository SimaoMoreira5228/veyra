#ifndef VEYRA_RENDER_SCROLL_SHIFT_HPP
#define VEYRA_RENDER_SCROLL_SHIFT_HPP

#include "veyra/layout/scroll_axis.hpp"
#include "veyra/render/cpu/software_renderer.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

namespace veyra {

struct ScrollShiftRect {
    std::size_t left{};
    std::size_t top{};
    std::size_t width{};
    std::size_t height{};
};

struct ScrollShiftRequest {
    std::size_t width{};
    std::size_t height{};
    int delta_x{};
    int delta_y{};
    bool has_clip{};
    ScrollShiftRect clip{};
    std::uint64_t current_generation{};
    std::uint64_t retained_generation{};
};

struct ScrollShiftPlan {
    bool has_blit{};
    ScrollShiftRect source{};
    ScrollShiftRect destination{};
    std::size_t strip_count{};
    ScrollShiftRect strips[2]{};
    bool full_damage_x{};
    bool full_damage_y{};
};

enum class ScrollShiftResult : std::uint8_t {
    complete,
    no_movement,
    invalid_input,
    stale_generation,
};

ScrollShiftResult
resolve_scroll_shift(const ScrollShiftRequest& request, ScrollShiftPlan& output) noexcept;

bool shift_surface_pixels(
    std::span<SoftwarePixel> pixels,
    std::size_t stride,
    std::size_t surface_width,
    std::size_t surface_height,
    const ScrollShiftRect& source,
    const ScrollShiftRect& destination
) noexcept;

}

#endif
