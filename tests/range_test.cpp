#include "veyra/behaviors/range.hpp"

#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

const double kQuietNan = std::numeric_limits<double>::quiet_NaN();
const double kInfinity = std::numeric_limits<double>::infinity();

void require(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

void check_clamp_and_snap() {
    auto control = veyra::RangeController{};
    const auto moved = control.set_value(42.6);
    require(
        moved.result == veyra::RangeResult::changed && moved.value == 43.0 &&
            control.value() == 43.0,
        "values snap to the step grid on the way in"
    );

    const auto same = control.set_value(43.2);
    require(
        same.result == veyra::RangeResult::handled && control.value() == 43.0,
        "a value snapping to the stored one reports no change"
    );

    const auto low = control.set_value(-20.0);
    require(
        low.result == veyra::RangeResult::changed && low.value == 0.0,
        "values below minimum clamp to the range floor"
    );

    const auto high = control.set_value(120.0);
    require(
        high.result == veyra::RangeResult::changed && high.value == 100.0,
        "values above maximum clamp to the range ceiling"
    );

    require(
        control.set_value(100.0).result == veyra::RangeResult::handled,
        "repeating the clamped ceiling reports no change"
    );
}

void check_fractional_steps_anchor_at_minimum() {
    auto control = veyra::RangeController{};
    require(
        control.configure({.minimum = 1.0, .maximum = 2.0, .step = 0.5, .value = 1.0}),
        "a fractional domain configures cleanly"
    );

    const auto snapped = control.set_value(1.3);
    require(
        snapped.result == veyra::RangeResult::changed && snapped.value == 1.5,
        "the snap grid anchors at minimum, not at zero"
    );

    require(
        control.configure({.minimum = 0.0, .maximum = 2.0, .step = 0.25, .value = 0.0}),
        "a quarter-step domain configures cleanly"
    );
    require(
        control.set_value(0.3).value == 0.25 && control.set_value(0.4).value == 0.5,
        "fractional steps snap down and up along the grid"
    );
}

void check_off_grid_maximum() {
    auto control = veyra::RangeController{};
    require(
        control.configure({.minimum = 0.0, .maximum = 10.0, .step = 3.0, .value = 0.0}),
        "a maximum off the step grid configures cleanly"
    );

    const auto end = control.set_to_max();
    require(
        end.result == veyra::RangeResult::changed && end.value == 9.0,
        "set_to_max snaps the ceiling down onto the grid"
    );
    require(
        control.increment().result == veyra::RangeResult::handled && control.value() == 9.0,
        "stepping past an off-grid ceiling holds instead of faking a change"
    );
}

void check_non_finite_rejection() {
    auto control = veyra::RangeController{};
    control.set_value(30.0);

    require(
        control.set_value(kQuietNan).result == veyra::RangeResult::ignored &&
            control.set_value(kInfinity).result == veyra::RangeResult::ignored &&
            control.set_value(-kInfinity).result == veyra::RangeResult::ignored,
        "NaN and infinite values are rejected"
    );
    require(
        control.set_fraction(kQuietNan).result == veyra::RangeResult::ignored &&
            control.value() == 30.0,
        "a NaN drag fraction leaves the stored value untouched"
    );

    const veyra::RangeState before = control.state();
    require(
        !control.configure({.minimum = kQuietNan, .maximum = 100.0, .step = 1.0, .value = 0.0}),
        "a NaN minimum is rejected"
    );
    require(
        !control.configure({.minimum = 0.0, .maximum = kInfinity, .step = 1.0, .value = 0.0}),
        "an infinite maximum is rejected"
    );
    require(
        !control.configure({.minimum = 0.0, .maximum = 100.0, .step = 1.0, .value = kQuietNan}),
        "a NaN configure value is rejected"
    );
    require(
        control.state().minimum == before.minimum && control.state().maximum == before.maximum &&
            control.state().value == before.value,
        "rejected configures leave the whole domain untouched"
    );
}

void check_domain_invariants() {
    auto control = veyra::RangeController{};
    control.set_value(25.0);

    require(
        !control.configure({.minimum = 80.0, .maximum = 20.0, .step = 1.0, .value = 50.0}),
        "an inverted domain is rejected, never silently swapped"
    );
    require(
        control.minimum() == 0.0 && control.maximum() == 100.0 && control.value() == 25.0,
        "an inverted domain leaves state untouched"
    );

    require(
        !control.configure({.minimum = 0.0, .maximum = 100.0, .step = 0.0, .value = 10.0}),
        "a zero step is rejected"
    );
    require(
        !control.configure({.minimum = 0.0, .maximum = 100.0, .step = -2.0, .value = 10.0}),
        "a negative step is rejected"
    );
    require(
        !control.configure(
            {.minimum = 0.0, .maximum = 100.0, .step = 1.0, .value = 10.0, .page_step = 0.0}
        ),
        "a zero page step is rejected"
    );
    require(control.step() == 1.0 && control.page_step() == 10.0, "rejected steps keep old state");

    require(
        control.configure({.minimum = 5.0, .maximum = 5.0, .step = 1.0, .value = 99.0}),
        "a collapsed domain is allowed"
    );
    require(
        control.value() == 5.0 && control.fraction() == 0.0,
        "a collapsed domain pins the value and reports a zero fraction"
    );
    require(
        control.set_value(99.0).result == veyra::RangeResult::handled,
        "every value in a collapsed domain snaps to the single point"
    );
}

void check_steps_pages_and_ends() {
    auto control = veyra::RangeController{};

    require(control.increment().value == 1.0, "increment moves one step");
    require(control.decrement().value == 0.0, "decrement returns to the floor");
    require(
        control.decrement().result == veyra::RangeResult::handled,
        "stepping past the floor reports no change"
    );

    require(control.page_increment().value == 10.0, "a page defaults to ten steps");
    require(control.page_decrement().value == 0.0, "a page back returns to the floor");

    require(control.set_to_max().value == 100.0, "set_to_max jumps to the ceiling");
    require(
        control.page_increment().result == veyra::RangeResult::handled,
        "paging past the ceiling reports no change"
    );
    require(control.set_to_min().value == 0.0, "set_to_min jumps to the floor");

    require(
        control.configure(
            {.minimum = 0.0,
             .maximum = 100.0,
             .step = 1.0,
             .value = 0.0,
             .page_step = 25.0,
             .kind = veyra::RangeKind::scrollbar}
        ),
        "an explicit page step configures cleanly"
    );
    require(control.page_increment().value == 25.0, "paging honors the caller page step");
    require(control.kind() == veyra::RangeKind::scrollbar, "the configured kind sticks");
}

void check_fraction_mapping() {
    auto control = veyra::RangeController{};

    require(control.set_fraction(0.0).value == 0.0, "fraction zero maps to minimum");
    require(control.set_fraction(1.0).value == 100.0, "fraction one maps to maximum");
    require(control.set_fraction(0.5).value == 50.0, "a mid fraction maps onto the domain");

    require(
        control.configure({.minimum = 10.0, .maximum = 20.0, .step = 4.0, .value = 10.0}),
        "an off-grid fraction domain configures cleanly"
    );
    const auto dragged = control.set_fraction(0.5);
    require(
        dragged.result == veyra::RangeResult::changed && dragged.value == 14.0,
        "drag fractions snap to the grid before committing"
    );

    control.set_to_min();
    require(control.set_fraction(-0.5).value == 10.0, "fractions below zero clamp to minimum");
    require(control.set_fraction(1.5).value == 20.0, "fractions above one clamp to maximum");
    require(
        control.fraction() == 1.0,
        "fraction reads back one at the snapped ceiling of an off-grid domain"
    );
}

void check_direct_commit_without_modes() {
    auto control = veyra::RangeController{};

    const auto first = control.set_value(60.0);
    require(
        first.result == veyra::RangeResult::changed && control.value() == 60.0,
        "set_value commits immediately with no intent round-trip"
    );
    require(
        !control.configure({.minimum = 0.0, .maximum = 100.0, .step = 1.0, .value = 60.0}),
        "configuring the identical domain reports no change"
    );
}

void check_disabled_rejects_mutations() {
    auto control = veyra::RangeController{};
    control.set_value(40.0);
    require(control.set_disabled(true), "disabling reports a state change");
    require(!control.set_disabled(true), "repeating the disable reports no change");

    require(
        control.set_value(50.0).result == veyra::RangeResult::ignored &&
            control.set_fraction(0.9).result == veyra::RangeResult::ignored &&
            control.increment().result == veyra::RangeResult::ignored &&
            control.decrement().result == veyra::RangeResult::ignored &&
            control.page_increment().result == veyra::RangeResult::ignored &&
            control.page_decrement().result == veyra::RangeResult::ignored &&
            control.set_to_min().result == veyra::RangeResult::ignored &&
            control.set_to_max().result == veyra::RangeResult::ignored,
        "every mutation is rejected while disabled"
    );
    require(control.value() == 40.0, "disabled reads still report the stored value");
    require(control.fraction() == 0.4, "disabled reads still report the fraction");

    auto semantics = veyra::SemanticNodeData{};
    require(
        control.write_semantics("Volume", "40", semantics) &&
            veyra::has_state(semantics.states, veyra::SemanticState::disabled) &&
            semantics.actions == 0,
        "disabled semantics keep the record but offer no actions"
    );

    require(control.set_disabled(false), "enabling reports a state change");
    require(
        control.increment().result == veyra::RangeResult::changed,
        "mutations work again once enabled"
    );
}

void check_semantics_per_kind() {
    auto control = veyra::RangeController{};

    auto semantics = veyra::SemanticNodeData{
        .role = veyra::SemanticRole::text,
        .label = "unchanged",
    };
    require(!control.write_semantics({}, "50", semantics), "an empty accessible name is rejected");
    require(
        semantics.role == veyra::SemanticRole::text && semantics.label == "unchanged",
        "rejected semantics leave the output untouched"
    );

    require(control.write_semantics("Volume", "50", semantics), "a named slider writes semantics");
    require(
        semantics.role == veyra::SemanticRole::slider && semantics.label == "Volume" &&
            semantics.value == "50" &&
            veyra::has_action(semantics.actions, veyra::SemanticAction::increment) &&
            veyra::has_action(semantics.actions, veyra::SemanticAction::decrement) &&
            veyra::has_action(semantics.actions, veyra::SemanticAction::set_value),
        "a slider exposes increment, decrement, and set_value with caller text"
    );

    control.configure(
        {.minimum = 0.0,
         .maximum = 100.0,
         .step = 1.0,
         .value = 0.0,
         .page_step = 10.0,
         .kind = veyra::RangeKind::scrollbar}
    );
    require(
        control.write_semantics("Log", "top", semantics) &&
            semantics.role == veyra::SemanticRole::scrollbar &&
            veyra::has_action(semantics.actions, veyra::SemanticAction::scroll_forward) &&
            veyra::has_action(semantics.actions, veyra::SemanticAction::scroll_backward),
        "a scrollbar exposes scroll actions instead of value actions"
    );

    control.configure(
        {.minimum = 0.0,
         .maximum = 10.0,
         .step = 1.0,
         .value = 3.0,
         .page_step = 5.0,
         .kind = veyra::RangeKind::spinbox}
    );
    require(
        control.write_semantics("Count", "3", semantics) &&
            semantics.role == veyra::SemanticRole::spinbutton &&
            veyra::has_action(semantics.actions, veyra::SemanticAction::set_value),
        "a spinbox exposes value actions under its own role"
    );

    control.configure(
        {.minimum = 0.0,
         .maximum = 100.0,
         .step = 1.0,
         .value = 70.0,
         .page_step = 10.0,
         .kind = veyra::RangeKind::progress}
    );
    require(
        control.write_semantics("Install", "70%", semantics) &&
            semantics.role == veyra::SemanticRole::progress_indicator && semantics.actions == 0,
        "progress is read-only: a role and text with no actions"
    );
}

}

int main() {
    try {
        check_clamp_and_snap();
        check_fractional_steps_anchor_at_minimum();
        check_off_grid_maximum();
        check_non_finite_rejection();
        check_domain_invariants();
        check_steps_pages_and_ends();
        check_fraction_mapping();
        check_direct_commit_without_modes();
        check_disabled_rejects_mutations();
        check_semantics_per_kind();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
