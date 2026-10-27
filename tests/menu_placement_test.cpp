#include "veyra/layout/panels/menu_placement.hpp"

#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

void require(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

veyra::LayoutRect viewport() {
    return {0.0F, 0.0F, 200.0F, 150.0F};
}

void check_preferred_below_right() {
    veyra::MenuPlacement placed{};
    const veyra::MenuPlacementRequest request{
        .anchor = {20.0F, 30.0F},
        .menu_width = 80.0F,
        .menu_height = 60.0F,
        .viewport = viewport(),
        .gap = 4.0F,
    };

    require(
        veyra::resolve_menu_placement(request, placed) == veyra::MenuPlacementResult::complete,
        "a menu with room opens below and right of the anchor"
    );
    require(
        placed.rect.x == 24.0F && placed.rect.y == 34.0F && placed.rect.width == 80.0F &&
            placed.rect.height == 60.0F,
        "the preferred rect offsets by the gap on both axes"
    );
    require(
        !placed.flipped_horizontal && !placed.flipped_vertical && !placed.clamped,
        "a clean fit flips and clamps nothing"
    );
}

void check_flip_near_edges() {
    veyra::MenuPlacement placed{};
    veyra::MenuPlacementRequest request{
        .anchor = {180.0F, 30.0F},
        .menu_width = 80.0F,
        .menu_height = 60.0F,
        .viewport = viewport(),
        .gap = 4.0F,
    };

    require(
        veyra::resolve_menu_placement(request, placed) == veyra::MenuPlacementResult::clamped,
        "a menu past the right edge reports its adjustment"
    );
    require(
        placed.flipped_horizontal && !placed.flipped_vertical && !placed.clamped,
        "a pure flip sets only its own flag"
    );
    require(
        placed.rect.x == 96.0F && placed.rect.y == 34.0F,
        "the flipped menu sits left of the anchor by the gap"
    );

    request.anchor = {20.0F, 130.0F};
    require(
        veyra::resolve_menu_placement(request, placed) == veyra::MenuPlacementResult::clamped,
        "a menu past the bottom edge reports its adjustment"
    );
    require(
        !placed.flipped_horizontal && placed.flipped_vertical,
        "the vertical overflow flips upward"
    );
    require(
        placed.rect.x == 24.0F && placed.rect.y == 66.0F,
        "the flipped menu sits above the anchor by the gap"
    );

    request.anchor = {180.0F, 130.0F};
    require(
        veyra::resolve_menu_placement(request, placed) == veyra::MenuPlacementResult::clamped,
        "a corner press reports its adjustment"
    );
    require(
        placed.flipped_horizontal && placed.flipped_vertical,
        "a corner overflow flips on both axes"
    );
    require(
        placed.rect.x == 96.0F && placed.rect.y == 66.0F,
        "the corner menu lands up and left of the anchor"
    );
}

void check_shift_when_flip_misses() {
    veyra::MenuPlacement placed{};
    const veyra::MenuPlacementRequest request{
        .anchor = {10.0F, 10.0F},
        .menu_width = 190.0F,
        .menu_height = 140.0F,
        .viewport = viewport(),
        .gap = 4.0F,
    };

    require(
        veyra::resolve_menu_placement(request, placed) == veyra::MenuPlacementResult::clamped,
        "a menu wider than its flip room reports its adjustment"
    );
    require(
        placed.flipped_horizontal && placed.flipped_vertical && placed.clamped,
        "flipping past the origin shifts back and marks the clamp"
    );
    require(
        placed.rect.x == 0.0F && placed.rect.y == 0.0F && placed.rect.width == 190.0F &&
            placed.rect.height == 140.0F,
        "the shift pins the menu to the viewport origin without resizing it"
    );
}

void check_oversized_reports_unfulfillable() {
    veyra::MenuPlacement placed{};
    const veyra::MenuPlacementRequest request{
        .anchor = {50.0F, 50.0F},
        .menu_width = 260.0F,
        .menu_height = 200.0F,
        .viewport = viewport(),
        .gap = 4.0F,
    };

    require(
        veyra::resolve_menu_placement(request, placed) == veyra::MenuPlacementResult::unfulfillable,
        "a menu larger than the viewport reports unfulfillable"
    );
    require(
        placed.clamped && placed.rect.x == 0.0F && placed.rect.y == 0.0F,
        "the hopeless menu pins to the viewport origin and says so"
    );
    require(
        placed.rect.width == 260.0F && placed.rect.height == 200.0F,
        "placement never resizes the menu, even when it cannot fit"
    );

    veyra::MenuPlacement tall{};
    const veyra::MenuPlacementRequest tall_request{
        .anchor = {50.0F, 50.0F},
        .menu_width = 40.0F,
        .menu_height = 400.0F,
        .viewport = viewport(),
        .gap = 4.0F,
    };
    require(
        veyra::resolve_menu_placement(tall_request, tall) ==
            veyra::MenuPlacementResult::unfulfillable,
        "one oversized axis is enough for unfulfillable"
    );
    require(
        tall.rect.x == 54.0F && tall.rect.y == 0.0F,
        "the fitting axis still places preferred while the other pins"
    );
}

void check_zero_gap_and_zero_menu() {
    veyra::MenuPlacement placed{};
    const veyra::MenuPlacementRequest request{
        .anchor = {20.0F, 30.0F},
        .menu_width = 80.0F,
        .menu_height = 60.0F,
        .viewport = viewport(),
        .gap = 0.0F,
    };

    require(
        veyra::resolve_menu_placement(request, placed) == veyra::MenuPlacementResult::complete &&
            placed.rect.x == 20.0F && placed.rect.y == 30.0F,
        "a zero gap plants the menu exactly on the anchor"
    );

    veyra::MenuPlacement dot{};
    const veyra::MenuPlacementRequest dot_request{
        .anchor = {20.0F, 30.0F},
        .menu_width = 0.0F,
        .menu_height = 0.0F,
        .viewport = viewport(),
        .gap = 4.0F,
    };
    require(
        veyra::resolve_menu_placement(dot_request, dot) == veyra::MenuPlacementResult::complete,
        "an empty menu still places complete"
    );
}

void check_exact_viewport_fit() {
    veyra::MenuPlacement placed{};
    const veyra::MenuPlacementRequest request{
        .anchor = {50.0F, 50.0F},
        .menu_width = 200.0F,
        .menu_height = 150.0F,
        .viewport = viewport(),
        .gap = 4.0F,
    };

    require(
        veyra::resolve_menu_placement(request, placed) == veyra::MenuPlacementResult::clamped,
        "a viewport-sized menu reports its adjustment"
    );
    require(
        placed.rect.x == 0.0F && placed.rect.y == 0.0F && placed.clamped,
        "the viewport-sized menu fills the viewport exactly"
    );
}

void check_invalid_input_leaves_output() {
    const float quiet = std::numeric_limits<float>::quiet_NaN();
    veyra::MenuPlacementRequest request{
        .anchor = {20.0F, 30.0F},
        .menu_width = 80.0F,
        .menu_height = 60.0F,
        .viewport = viewport(),
        .gap = 4.0F,
    };
    veyra::MenuPlacement placed{
        {.x = 1.0F, .y = 2.0F, .width = 3.0F, .height = 4.0F},
        true,
        true,
        true,
    };

    request.menu_width = quiet;
    require(
        veyra::resolve_menu_placement(request, placed) == veyra::MenuPlacementResult::invalid_input,
        "a NaN menu width is rejected"
    );

    request.menu_width = -1.0F;
    require(
        veyra::resolve_menu_placement(request, placed) == veyra::MenuPlacementResult::invalid_input,
        "a negative menu width is rejected"
    );

    request.menu_width = 80.0F;
    request.gap = -2.0F;
    require(
        veyra::resolve_menu_placement(request, placed) == veyra::MenuPlacementResult::invalid_input,
        "a negative gap is rejected"
    );

    request.gap = 4.0F;
    request.viewport.width = -10.0F;
    require(
        veyra::resolve_menu_placement(request, placed) == veyra::MenuPlacementResult::invalid_input,
        "a negative viewport is rejected"
    );

    require(
        placed.rect.x == 1.0F && placed.rect.y == 2.0F && placed.rect.width == 3.0F &&
            placed.rect.height == 4.0F && placed.flipped_horizontal && placed.flipped_vertical &&
            placed.clamped,
        "rejected inputs leave the output untouched"
    );
}

}

int main() {
    try {
        check_preferred_below_right();
        check_flip_near_edges();
        check_shift_when_flip_misses();
        check_oversized_reports_unfulfillable();
        check_zero_gap_and_zero_menu();
        check_exact_viewport_fit();
        check_invalid_input_leaves_output();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
