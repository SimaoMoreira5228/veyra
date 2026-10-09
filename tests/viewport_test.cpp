#include "veyra/layout/split_pane.hpp"
#include "veyra/layout/viewport.hpp"

#include <cmath>
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

bool near(float actual, float expected) {
    return std::abs(actual - expected) < 0.001F;
}

void check_default_viewport() {
    const veyra::Viewport viewport;

    require(viewport.width() == 0.0F, "fresh viewport starts with zero width");
    require(viewport.height() == 0.0F, "fresh viewport starts with zero height");
    require(viewport.scale() == 1.0F, "fresh viewport starts at unit scale");
    require(!viewport.has_area(), "zero-size viewport reports no area");
    require(viewport.layout_generation() != 0, "layout generation never starts at zero");
    require(viewport.scale_generation() != 0, "scale generation never starts at zero");
}

void check_size_noop_and_change() {
    veyra::Viewport viewport;

    auto update = viewport.set_size(800.0F, 600.0F);
    require(update.accepted, "valid size change is accepted");
    require(
        update.invalidation.has(veyra::ViewportInvalidation::kLayout),
        "size change reruns layout"
    );
    require(
        update.invalidation.has(veyra::ViewportInvalidation::kDamage),
        "size change damages the full surface"
    );
    require(
        !update.invalidation.has(veyra::ViewportInvalidation::kText),
        "size change keeps text shaping"
    );
    require(
        !update.invalidation.has(veyra::ViewportInvalidation::kImages),
        "size change keeps image keys"
    );
    require(viewport.has_area(), "nonzero size reports area");

    const auto layout_generation = viewport.layout_generation();
    const auto scale_generation = viewport.scale_generation();
    require(layout_generation != 1, "size change bumps the layout generation");

    update = viewport.set_size(800.0F, 600.0F);
    require(update.accepted, "repeated size is still accepted");
    require(update.invalidation.empty(), "same size is a no-op");
    require(
        viewport.layout_generation() == layout_generation,
        "same size leaves the layout generation alone"
    );
    require(
        viewport.scale_generation() == scale_generation,
        "size traffic never touches the scale generation"
    );

    const auto constraints = viewport.root_constraints();
    require(
        constraints.width.minimum == 800.0F && constraints.width.maximum == 800.0F,
        "root width constraints pin the viewport width"
    );
    require(
        constraints.height.minimum == 600.0F && constraints.height.maximum == 600.0F,
        "root height constraints pin the viewport height"
    );
}

void check_zero_size_policy() {
    veyra::Viewport viewport;

    auto update = viewport.set_size(0.0F, 0.0F);
    require(update.accepted, "zero size is accepted on a fresh viewport");
    require(update.invalidation.empty(), "zero over zero is a no-op");

    viewport.set_size(320.0F, 200.0F);
    update = viewport.set_size(0.0F, 0.0F);
    require(update.accepted, "shrinking to zero is accepted");
    require(
        update.invalidation.has(veyra::ViewportInvalidation::kLayout),
        "shrinking to zero still reports layout"
    );
    require(
        update.invalidation.has(veyra::ViewportInvalidation::kDamage),
        "shrinking to zero still reports damage"
    );
    require(!viewport.has_area(), "zero width drops the area flag");

    update = viewport.set_size(0.0F, 200.0F);
    require(update.accepted, "a zero edge with nonzero height is accepted");
    require(!viewport.has_area(), "a zero edge means no area");
}

void check_scale_noop_and_change() {
    veyra::Viewport viewport;
    viewport.set_size(800.0F, 600.0F);

    auto update = viewport.set_scale(1.0F);
    require(update.accepted, "same scale is accepted");
    require(update.invalidation.empty(), "same scale is a no-op");

    const auto layout_generation = viewport.layout_generation();
    const auto scale_generation = viewport.scale_generation();

    update = viewport.set_scale(2.0F);
    require(update.accepted, "valid scale change is accepted");
    require(viewport.scale() == 2.0F, "scale change sticks");
    require(
        update.invalidation.has(veyra::ViewportInvalidation::kLayout),
        "scale change reruns layout"
    );
    require(
        update.invalidation.has(veyra::ViewportInvalidation::kText),
        "scale change reshapes text"
    );
    require(
        update.invalidation.has(veyra::ViewportInvalidation::kImages),
        "scale change re-keys images"
    );
    require(
        update.invalidation.has(veyra::ViewportInvalidation::kDamage),
        "scale change damages the full surface"
    );
    require(
        viewport.layout_generation() != layout_generation,
        "scale change bumps the layout generation"
    );
    require(
        viewport.scale_generation() != scale_generation,
        "scale change bumps the scale generation"
    );
}

void check_generation_monotonicity() {
    veyra::Viewport viewport;

    auto previous = viewport.layout_generation();

    for (int i = 1; i <= 8; ++i) {
        viewport.set_size(static_cast<float>(100 + i), 100.0F);
        require(
            viewport.layout_generation() != previous,
            "every size change moves the layout generation"
        );
        previous = viewport.layout_generation();
    }

    auto previous_scale = viewport.scale_generation();
    viewport.set_scale(1.25F);
    viewport.set_scale(1.5F);

    require(
        viewport.scale_generation() != previous_scale,
        "scale changes move the scale generation"
    );
    require(
        viewport.layout_generation() != previous,
        "scale changes also move the layout generation"
    );
}

void check_invalid_viewport_inputs() {
    veyra::Viewport viewport;
    viewport.set_size(800.0F, 600.0F);
    viewport.set_scale(2.0F);

    const float quiet_nan = std::numeric_limits<float>::quiet_NaN();
    const float infinity = std::numeric_limits<float>::infinity();

    const auto width = viewport.width();
    const auto height = viewport.height();
    const auto scale = viewport.scale();
    const auto layout_generation = viewport.layout_generation();
    const auto scale_generation = viewport.scale_generation();

    auto update = viewport.set_size(quiet_nan, 600.0F);
    require(!update.accepted, "NaN width is rejected");
    require(update.invalidation.empty(), "rejected size reports nothing");

    update = viewport.set_size(800.0F, -1.0F);
    require(!update.accepted, "negative height is rejected");

    update = viewport.set_size(infinity, 600.0F);
    require(!update.accepted, "infinite width is rejected");

    update = viewport.set_scale(0.0F);
    require(!update.accepted, "zero scale is rejected");

    update = viewport.set_scale(-2.0F);
    require(!update.accepted, "negative scale is rejected");

    update = viewport.set_scale(quiet_nan);
    require(!update.accepted, "NaN scale is rejected");

    update = viewport.set_scale(infinity);
    require(!update.accepted, "infinite scale is rejected");

    require(viewport.width() == width, "rejected size leaves width untouched");
    require(viewport.height() == height, "rejected size leaves height untouched");
    require(viewport.scale() == scale, "rejected scale leaves scale untouched");
    require(
        viewport.layout_generation() == layout_generation,
        "rejected input leaves the layout generation untouched"
    );
    require(
        viewport.scale_generation() == scale_generation,
        "rejected input leaves the scale generation untouched"
    );
}

void check_row_split_basics() {
    const veyra::SplitPaneRequest request{
        .total = {.x = 0.0F, .y = 0.0F, .width = 100.0F, .height = 50.0F},
        .fraction = 0.5F,
        .gap = 10.0F,
    };
    veyra::SplitPaneLayout panes{};

    const auto result = veyra::resolve_split_pane(request, panes);
    require(result == veyra::SplitPaneResult::complete, "even split completes");
    require(
        near(panes.first.x, 0.0F) && near(panes.first.width, 45.0F),
        "first pane takes half the gap-adjusted extent"
    );
    require(
        near(panes.second.x, 55.0F) && near(panes.second.width, 45.0F),
        "second pane starts past the divider gap"
    );
    require(
        near(panes.first.height, 50.0F) && near(panes.second.height, 50.0F),
        "row split keeps the full height"
    );
    require(
        near(panes.first.width + request.gap + panes.second.width, 100.0F),
        "panes plus gap fill the total extent"
    );
    require(near(panes.fraction, 0.5F), "effective fraction echoes the request");
}

void check_column_split_and_origin() {
    const veyra::SplitPaneRequest request{
        .total = {.x = 10.0F, .y = 20.0F, .width = 60.0F, .height = 100.0F},
        .fraction = 0.25F,
        .gap = 8.0F,
        .direction = veyra::FlexDirection::column,
    };
    veyra::SplitPaneLayout panes{};

    const auto result = veyra::resolve_split_pane(request, panes);
    require(result == veyra::SplitPaneResult::complete, "column split completes");
    require(
        near(panes.first.y, 20.0F) && near(panes.first.height, 23.0F),
        "first pane takes a quarter of the gap-adjusted height"
    );
    require(
        near(panes.second.y, 51.0F) && near(panes.second.height, 69.0F),
        "second pane starts past the divider gap"
    );
    require(
        near(panes.first.x, 10.0F) && near(panes.first.width, 60.0F),
        "column split keeps the full width and the total origin"
    );
    require(
        near(panes.first.height + request.gap + panes.second.height, 100.0F),
        "stacked panes plus gap fill the total height"
    );
}

void check_minimums_win_over_position() {
    veyra::SplitPaneLayout panes{};

    auto result = veyra::resolve_split_pane(
        veyra::SplitPaneRequest{
            .total = {.width = 100.0F, .height = 40.0F},
            .fraction = 0.1F,
            .min_first = 40.0F,
        },
        panes
    );
    require(result == veyra::SplitPaneResult::clamped, "minimums override the position");
    require(near(panes.first.width, 40.0F), "first pane honors its minimum");
    require(near(panes.second.width, 60.0F), "second pane takes the remainder");
    require(near(panes.fraction, 0.4F), "effective fraction follows the clamp");

    result = veyra::resolve_split_pane(
        veyra::SplitPaneRequest{
            .total = {.width = 100.0F, .height = 40.0F},
            .fraction = 0.9F,
            .min_second = 40.0F,
        },
        panes
    );
    require(result == veyra::SplitPaneResult::clamped, "trailing minimums clamp too");
    require(near(panes.first.width, 60.0F), "first pane yields to the trailing minimum");
    require(near(panes.second.width, 40.0F), "second pane honors its minimum");
}

void check_fraction_edges() {
    veyra::SplitPaneLayout panes{};

    auto result = veyra::resolve_split_pane(
        veyra::SplitPaneRequest{
            .total = {.width = 100.0F, .height = 40.0F},
            .fraction = 0.0F,
        },
        panes
    );
    require(result == veyra::SplitPaneResult::complete, "zero fraction completes");
    require(
        near(panes.first.width, 0.0F) && near(panes.second.width, 100.0F),
        "zero fraction collapses the first pane"
    );

    result = veyra::resolve_split_pane(
        veyra::SplitPaneRequest{
            .total = {.width = 100.0F, .height = 40.0F},
            .fraction = 1.0F,
        },
        panes
    );
    require(result == veyra::SplitPaneResult::complete, "unit fraction completes");
    require(
        near(panes.first.width, 100.0F) && near(panes.second.width, 0.0F),
        "unit fraction collapses the second pane"
    );

    result = veyra::resolve_split_pane(
        veyra::SplitPaneRequest{
            .total = {.width = 100.0F, .height = 40.0F},
            .fraction = 1.5F,
        },
        panes
    );
    require(
        result == veyra::SplitPaneResult::clamped,
        "finite fractions past one clamp instead of failing"
    );
    require(near(panes.first.width, 100.0F), "clamped fraction pins the divider at the end");

    result = veyra::resolve_split_pane(
        veyra::SplitPaneRequest{
            .total = {.width = 100.0F, .height = 40.0F},
            .fraction = -0.5F,
        },
        panes
    );
    require(
        result == veyra::SplitPaneResult::clamped,
        "finite fractions below zero clamp instead of failing"
    );
    require(near(panes.first.width, 0.0F), "clamped fraction pins the divider at the start");
}

void check_unfulfillable_minimums() {
    veyra::SplitPaneLayout panes{};

    auto result = veyra::resolve_split_pane(
        veyra::SplitPaneRequest{
            .total = {.width = 100.0F, .height = 40.0F},
            .fraction = 0.5F,
            .min_first = 80.0F,
            .min_second = 80.0F,
        },
        panes
    );
    require(result == veyra::SplitPaneResult::unfulfillable, "overlapping minimums are reported");
    require(
        near(panes.first.width, 50.0F) && near(panes.second.width, 50.0F),
        "unfulfillable minimums degrade proportionally"
    );
    require(
        near(panes.first.width + panes.second.width, 100.0F),
        "degraded panes never overlap and never leave holes"
    );

    result = veyra::resolve_split_pane(
        veyra::SplitPaneRequest{
            .total = {.width = 100.0F, .height = 40.0F},
            .fraction = 0.5F,
            .gap = 120.0F,
        },
        panes
    );
    require(result == veyra::SplitPaneResult::unfulfillable, "a gap past the extent is reported");
    require(
        near(panes.first.width, 0.0F) && near(panes.second.width, 0.0F),
        "a gap past the extent collapses both panes"
    );

    result = veyra::resolve_split_pane(
        veyra::SplitPaneRequest{
            .total = {.width = 0.0F, .height = 0.0F},
            .fraction = 0.5F,
        },
        panes
    );
    require(result == veyra::SplitPaneResult::complete, "zero extent with zero minimums completes");
    require(
        near(panes.first.width, 0.0F) && near(panes.second.width, 0.0F),
        "zero extent yields zero panes"
    );
}

void check_invalid_split_inputs() {
    const veyra::SplitPaneLayout sentinel{
        .first = {.x = 3.0F, .y = 4.0F, .width = 5.0F, .height = 6.0F},
        .second = {.x = 7.0F, .y = 8.0F, .width = 9.0F, .height = 10.0F},
        .fraction = 0.75F,
    };
    const float quiet_nan = std::numeric_limits<float>::quiet_NaN();

    const veyra::SplitPaneRequest bad_requests[]{
        {.total = {.width = quiet_nan, .height = 40.0F}, .fraction = 0.5F},
        {.total = {.width = -100.0F, .height = 40.0F}, .fraction = 0.5F},
        {.total = {.width = 100.0F, .height = 40.0F}, .fraction = quiet_nan},
        {.total = {.width = 100.0F, .height = 40.0F}, .fraction = 0.5F, .gap = -4.0F},
        {.total = {.width = 100.0F, .height = 40.0F}, .fraction = 0.5F, .min_first = -1.0F},
        {.total = {.width = 100.0F, .height = 40.0F}, .fraction = 0.5F, .min_second = quiet_nan},
    };

    for (const auto& request : bad_requests) {
        auto panes = sentinel;
        const auto result = veyra::resolve_split_pane(request, panes);

        require(result == veyra::SplitPaneResult::invalid_input, "bad split input is rejected");
        require(
            near(panes.first.x, 3.0F) && near(panes.second.width, 9.0F),
            "rejected split leaves the output untouched"
        );
        require(near(panes.fraction, 0.75F), "rejected split leaves the fraction untouched");
    }
}

}

int main() {
    try {
        check_default_viewport();
        check_size_noop_and_change();
        check_zero_size_policy();
        check_scale_noop_and_change();
        check_generation_monotonicity();
        check_invalid_viewport_inputs();
        check_row_split_basics();
        check_column_split_and_origin();
        check_minimums_win_over_position();
        check_fraction_edges();
        check_unfulfillable_minimums();
        check_invalid_split_inputs();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
