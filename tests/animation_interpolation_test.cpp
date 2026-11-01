#include "veyra/media/animation/animation_interpolation.hpp"

#include <cmath>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

void require(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

bool near(float first, float second, float tolerance = 0.0001F) {
    return std::fabs(first - second) <= tolerance;
}

void check_duration_zero_snaps_and_clamps() {
    require(
        veyra::interpolate(0.0F, 1.0F, 0, 0, veyra::AnimationEasing::linear) == 1.0F,
        "zero duration snaps to the target"
    );
    require(
        veyra::interpolate(0.0F, 1.0F, 999, 100, veyra::AnimationEasing::linear) == 1.0F,
        "elapsed past the duration holds the target"
    );
    require(
        veyra::interpolate(0.0F, 1.0F, 100, 100, veyra::AnimationEasing::linear) == 1.0F,
        "elapsed at the duration reaches the target"
    );
    require(
        veyra::interpolate(2.0F, 6.0F, 50, 100, veyra::AnimationEasing::linear) == 4.0F,
        "linear interpolation crosses the midpoint"
    );
    require(
        veyra::interpolate(5.0F, 5.0F, 30, 100, veyra::AnimationEasing::linear) == 5.0F,
        "equal endpoints stay put"
    );
}

void check_easing_endpoints_and_bounds() {
    const veyra::AnimationEasing easings[] = {
        veyra::AnimationEasing::linear,
        veyra::AnimationEasing::ease_out_cubic,
        veyra::AnimationEasing::ease_in_out_cubic,
    };

    for (const auto easing : easings) {
        require(veyra::easing_position(easing, 0.0F) == 0.0F, "easing starts at zero");
        require(veyra::easing_position(easing, 1.0F) == 1.0F, "easing ends at one");
        require(veyra::easing_position(easing, -2.0F) == 0.0F, "easing clamps below zero");
        require(veyra::easing_position(easing, 2.0F) == 1.0F, "easing clamps above one");

        auto previous = 0.0F;
        for (std::uint32_t step = 0; step <= 100; ++step) {
            const auto progress = static_cast<float>(step) / 100.0F;
            const auto value = veyra::easing_position(easing, progress);
            require(value >= 0.0F && value <= 1.0F, "easing never overshoots its endpoints");
            require(value >= previous, "easing never moves backward");
            previous = value;
        }
    }

    require(
        near(
            veyra::interpolate(0.0F, 1.0F, 50, 100, veyra::AnimationEasing::ease_out_cubic),
            0.875F
        ),
        "ease-out cubic covers most ground early"
    );
    require(
        near(
            veyra::interpolate(0.0F, 1.0F, 50, 100, veyra::AnimationEasing::ease_in_out_cubic),
            0.5F
        ),
        "ease-in-out cubic crosses the midpoint"
    );
}

void check_exponential_approach_converges() {
    require(
        veyra::approach(0.0F, 1.0F, 0, 100) == 0.0F,
        "zero elapsed time holds the current value"
    );
    require(veyra::approach(0.0F, 1.0F, 50, 0) == 1.0F, "zero time constant snaps to the target");

    auto value = 0.0F;
    auto previous = value;
    for (std::uint32_t step = 0; step < 60; ++step) {
        value = veyra::approach(value, 1.0F, 16, 100);
        require(value >= previous, "approach never moves away from the target");
        require(value <= 1.0F, "approach never overshoots the target");
        previous = value;
    }
    require(near(value, 1.0F, 0.01F), "approach converges near the target");

    auto falling = 1.0F;
    for (std::uint32_t step = 0; step < 60; ++step) {
        falling = veyra::approach(falling, 0.0F, 16, 100);
        require(falling >= 0.0F, "a falling approach never undershoots");
    }
    require(near(falling, 0.0F, 0.01F), "a falling approach converges near zero");
}

void check_animator_runs_to_completion() {
    auto animator = veyra::ScalarAnimator{};
    animator.start(0.0F, 1.0F, 0, 100, veyra::AnimationEasing::linear);

    require(animator.running(), "a fresh animation runs");
    require(!animator.finished(), "a fresh animation is not finished");
    require(animator.advance(0) == 0.0F, "zero elapsed time holds the start value");
    require(animator.advance(50) == 0.5F, "half the duration crosses the midpoint");
    require(animator.running(), "a partial animation still runs");
    require(animator.advance(100) == 1.0F, "full duration reaches the target");
    require(animator.finished(), "reaching the target finishes the animation");
    require(animator.advance(5000) == 1.0F, "a finished animation holds its target");

    auto snapped = veyra::ScalarAnimator{};
    snapped.start(0.0F, 1.0F, 0, 0, veyra::AnimationEasing::linear);
    require(snapped.finished(), "zero duration finishes immediately");
    require(snapped.value() == 1.0F, "zero duration snaps to the target");
}

void check_retarget_restarts_from_current() {
    auto animator = veyra::ScalarAnimator{};
    animator.start(0.0F, 1.0F, 0, 100, veyra::AnimationEasing::linear);
    require(animator.advance(40) == 0.4F, "the first leg advances partway");

    animator.retarget(0.0F, 40);
    require(animator.value() == 0.4F, "retargeting never jumps from the current value");
    require(animator.running(), "retargeting to a new value keeps running");
    require(
        near(animator.advance(90), 0.2F),
        "the new leg restarts its clock at the retarget point"
    );
    require(animator.advance(140) == 0.0F, "the new leg completes at its target");
    require(animator.finished(), "completing the new leg finishes the animation");
}

void check_reduced_motion_snaps_to_target() {
    auto snapped = veyra::ScalarAnimator{};
    snapped.set_reduced_motion(true);
    snapped.start(0.0F, 1.0F, 0, 100, veyra::AnimationEasing::linear);
    require(snapped.value() == 1.0F, "reduced motion snaps a new animation");
    require(snapped.finished(), "a snapped animation is finished");

    snapped.set_reduced_motion(false);
    snapped.start(0.0F, 1.0F, 1100, 100, veyra::AnimationEasing::linear);
    require(snapped.advance(1150) == 0.5F, "clearing the flag restores animation");

    auto mid_flight = veyra::ScalarAnimator{};
    mid_flight.start(0.0F, 1.0F, 0, 100, veyra::AnimationEasing::linear);
    require(mid_flight.advance(40) == 0.4F, "the animation advances before the flag");
    mid_flight.set_reduced_motion(true);
    require(mid_flight.value() == 1.0F, "engaging mid-flight snaps to the target");
    require(mid_flight.finished(), "engaging mid-flight finishes the animation");
}

void check_pause_and_throttle_share_one_gate() {
    auto animator = veyra::ScalarAnimator{};
    animator.start(0.0F, 1.0F, 0, 100, veyra::AnimationEasing::linear);
    require(animator.advance(40) == 0.4F, "the animation advances before pausing");

    animator.set_paused(true);
    require(animator.advance(5000) == 0.4F, "paused ticks contribute zero progress");
    require(animator.running(), "pausing does not finish the animation");

    animator.set_paused(false);
    require(animator.advance(5060) == 1.0F, "resuming continues from the held value");

    auto offscreen = veyra::ScalarAnimator{};
    offscreen.start(0.0F, 1.0F, 0, 100, veyra::AnimationEasing::linear);
    require(offscreen.advance(40) == 0.4F, "the throttled animation advances while visible");
    offscreen.set_throttled(true);
    require(offscreen.advance(5000) == 0.4F, "throttled ticks contribute zero progress");
    offscreen.set_throttled(false);
    require(offscreen.advance(5060) == 1.0F, "unthrottling continues from the held value");
}

void check_backward_clock_never_jumps() {
    auto animator = veyra::ScalarAnimator{};
    animator.start(0.0F, 1.0F, 100, 100, veyra::AnimationEasing::linear);
    require(animator.advance(140) == 0.4F, "the animation advances normally");
    require(animator.advance(50) == 0.4F, "a backward clock contributes zero progress");
    require(animator.advance(200) == 1.0F, "the clock recovers after a backward tick");
}

void check_color_mixing() {
    const auto from = veyra::Srgba8{.red = 0, .green = 0, .blue = 0, .alpha = 255};
    const auto to = veyra::Srgba8{.red = 100, .green = 150, .blue = 200, .alpha = 255};

    require(veyra::mix_srgba8(from, to, 0.0F) == from, "zero progress keeps the start color");
    require(veyra::mix_srgba8(from, to, 1.0F) == to, "full progress reaches the end color");
    require(veyra::mix_srgba8(from, to, -1.0F) == from, "mixing clamps below zero");
    require(veyra::mix_srgba8(from, to, 2.0F) == to, "mixing clamps above one");

    const auto middle = veyra::mix_srgba8(from, to, 0.5F);
    require(
        middle == veyra::Srgba8{.red = 50, .green = 75, .blue = 100, .alpha = 255},
        "mixing blends each channel"
    );
}

void check_classification_and_dirty_routing() {
    require(
        veyra::invalidation_for(veyra::AnimatedProperty::opacity) ==
            veyra::AnimationInvalidation::none,
        "opacity takes the compositor path"
    );
    require(
        veyra::invalidation_for(veyra::AnimatedProperty::transform) ==
            veyra::AnimationInvalidation::none,
        "transform takes the compositor path"
    );
    require(
        veyra::invalidation_for(veyra::AnimatedProperty::color) ==
            veyra::AnimationInvalidation::paint,
        "color repaints without layout"
    );
    require(
        veyra::invalidation_for(veyra::AnimatedProperty::geometry) ==
            veyra::AnimationInvalidation::layout,
        "geometry pays layout"
    );

    auto tree = veyra::NodeTree{};
    const auto root = tree.create();
    const auto child = tree.create();
    require(tree.append_child(root, child), "test nodes link");

    tree.clear_dirty(child, veyra::DirtyCategory::structure);
    tree.clear_dirty(child, veyra::DirtyCategory::layout);
    tree.clear_dirty(child, veyra::DirtyCategory::paint);

    require(
        !veyra::mark_animation_dirty(tree, child, veyra::AnimatedProperty::opacity),
        "a compositor opacity tick invalidates nothing"
    );
    require(
        !tree.is_dirty(child, veyra::DirtyCategory::paint) &&
            !tree.is_dirty(child, veyra::DirtyCategory::layout),
        "the compositor path leaves paint and layout clean"
    );
    require(
        !veyra::mark_animation_dirty(tree, child, veyra::AnimatedProperty::transform),
        "a compositor transform tick invalidates nothing"
    );
    require(
        !tree.is_dirty(child, veyra::DirtyCategory::paint) &&
            !tree.is_dirty(child, veyra::DirtyCategory::layout),
        "transforms leave paint and layout clean"
    );

    require(
        veyra::mark_animation_dirty(tree, child, veyra::AnimatedProperty::color),
        "a color tick marks paint"
    );
    require(tree.is_dirty(child, veyra::DirtyCategory::paint), "color marks the paint flag");
    require(!tree.is_dirty(child, veyra::DirtyCategory::layout), "color pays no layout");

    require(
        veyra::mark_animation_dirty(tree, child, veyra::AnimatedProperty::geometry),
        "a geometry tick marks layout"
    );
    require(tree.is_dirty(child, veyra::DirtyCategory::layout), "geometry marks the layout flag");
}

}

int main() {
    try {
        check_duration_zero_snaps_and_clamps();
        check_easing_endpoints_and_bounds();
        check_exponential_approach_converges();
        check_animator_runs_to_completion();
        check_retarget_restarts_from_current();
        check_reduced_motion_snaps_to_target();
        check_pause_and_throttle_share_one_gate();
        check_backward_clock_never_jumps();
        check_color_mixing();
        check_classification_and_dirty_routing();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
