#ifndef VEYRA_LAYOUT_MENU_PLACEMENT_HPP
#define VEYRA_LAYOUT_MENU_PLACEMENT_HPP

#include "veyra/layout/geometry.hpp"

#include <cmath>
#include <cstdint>

namespace veyra {

enum class MenuPlacementResult : std::uint8_t {
    complete,
    clamped,
    invalid_input,
    unfulfillable,
};

struct MenuPlacementRequest {
    LayoutPoint anchor{};
    float menu_width{};
    float menu_height{};
    LayoutRect viewport{};
    float gap{};
};

struct MenuPlacement {
    LayoutRect rect{};
    bool flipped_horizontal{};
    bool flipped_vertical{};
    bool clamped{};
};

inline MenuPlacementResult
resolve_menu_placement(const MenuPlacementRequest& request, MenuPlacement& output) noexcept {
    if (!std::isfinite(request.anchor.x) || !std::isfinite(request.anchor.y)) {
        return MenuPlacementResult::invalid_input;
    }

    if (!std::isfinite(request.menu_width) || !std::isfinite(request.menu_height)) {
        return MenuPlacementResult::invalid_input;
    }

    if (!std::isfinite(request.viewport.x) || !std::isfinite(request.viewport.y)) {
        return MenuPlacementResult::invalid_input;
    }

    if (!std::isfinite(request.viewport.width) || !std::isfinite(request.viewport.height)) {
        return MenuPlacementResult::invalid_input;
    }

    if (!std::isfinite(request.gap)) {
        return MenuPlacementResult::invalid_input;
    }

    if (request.menu_width < 0.0F || request.menu_height < 0.0F) {
        return MenuPlacementResult::invalid_input;
    }

    if (request.viewport.width < 0.0F || request.viewport.height < 0.0F) {
        return MenuPlacementResult::invalid_input;
    }

    if (request.gap < 0.0F) {
        return MenuPlacementResult::invalid_input;
    }

    const float left = request.viewport.x;
    const float top = request.viewport.y;
    const float right = request.viewport.x + request.viewport.width;
    const float bottom = request.viewport.y + request.viewport.height;

    float x = request.anchor.x + request.gap;
    float y = request.anchor.y + request.gap;
    bool flipped_horizontal = false;
    bool flipped_vertical = false;
    bool clamped = false;
    bool fits = true;

    if (request.menu_width > request.viewport.width) {
        x = left;
        fits = false;
    } else {
        if (x + request.menu_width > right) {
            x = request.anchor.x - request.gap - request.menu_width;
            flipped_horizontal = true;
        }

        if (x < left) {
            x = left;
            clamped = true;
        } else if (x + request.menu_width > right) {
            x = right - request.menu_width;
            clamped = true;
        }
    }

    if (request.menu_height > request.viewport.height) {
        y = top;
        fits = false;
    } else {
        if (y + request.menu_height > bottom) {
            y = request.anchor.y - request.gap - request.menu_height;
            flipped_vertical = true;
        }

        if (y < top) {
            y = top;
            clamped = true;
        } else if (y + request.menu_height > bottom) {
            y = bottom - request.menu_height;
            clamped = true;
        }
    }

    output.rect = {x, y, request.menu_width, request.menu_height};
    output.flipped_horizontal = flipped_horizontal;
    output.flipped_vertical = flipped_vertical;
    output.clamped = clamped || !fits;

    if (!fits) {
        return MenuPlacementResult::unfulfillable;
    }

    if (flipped_horizontal || flipped_vertical || clamped) {
        return MenuPlacementResult::clamped;
    }

    return MenuPlacementResult::complete;
}

}

#endif
