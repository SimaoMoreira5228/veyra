#include "veyra/layout/breakpoints.hpp"

#include <array>
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

veyra::BreakpointController make_controller() {
    veyra::BreakpointController controller;

    const std::array<float, 2> thresholds{400.0F, 800.0F};
    const bool configured = controller.configure(thresholds);
    require(configured, "two-threshold setup is accepted");

    return controller;
}

void check_band_resolution_and_edges() {
    auto controller = make_controller();

    auto update = controller.update(0.0F);
    require(update.accepted, "zero width is accepted");
    require(update.variant == 0, "zero width sits in band zero");
    require(!update.changed, "first update in band zero reports no change");

    update = controller.update(399.0F);
    require(update.variant == 0, "width below the first threshold stays in band zero");
    require(!update.changed, "staying in band zero reports no change");

    update = controller.update(400.0F);
    require(update.variant == 1, "width exactly on a threshold joins the upper band");
    require(update.changed, "crossing a threshold reports a change");

    update = controller.update(799.0F);
    require(update.variant == 1, "width between thresholds stays in the middle band");
    require(!update.changed, "staying in the middle band reports no change");

    update = controller.update(800.0F);
    require(update.variant == 2, "width exactly on the second threshold joins the top band");
    require(update.changed, "crossing the second threshold reports a change");

    update = controller.update(5000.0F);
    require(update.variant == 2, "huge widths clamp into the top band");
    require(!update.changed, "staying in the top band reports no change");

    require(controller.active_variant() == 2, "active variant tracks the last update");
    require(controller.band_count() == 3, "two thresholds make three bands");
}

void check_configure_rejection() {
    veyra::BreakpointController controller;

    const std::array<float, 2> good{300.0F, 600.0F};
    require(controller.configure(good), "ascending thresholds are accepted");
    require(controller.threshold_count() == 2, "accepted thresholds stick");

    const std::array<float, 2> unsorted{600.0F, 300.0F};
    require(!controller.configure(unsorted), "out-of-order thresholds are rejected");

    const std::array<float, 2> duplicate{300.0F, 300.0F};
    require(!controller.configure(duplicate), "duplicate thresholds are rejected");

    const float quiet_nan = std::numeric_limits<float>::quiet_NaN();
    const float infinity = std::numeric_limits<float>::infinity();

    const std::array<float, 2> with_nan{300.0F, quiet_nan};
    require(!controller.configure(with_nan), "NaN thresholds are rejected");

    const std::array<float, 2> with_infinity{300.0F, infinity};
    require(!controller.configure(with_infinity), "infinite thresholds are rejected");

    const std::array<float, 2> with_negative{-10.0F, 300.0F};
    require(!controller.configure(with_negative), "negative thresholds are rejected");

    const std::array<float, 9> too_many{
        100.0F,
        200.0F,
        300.0F,
        400.0F,
        500.0F,
        600.0F,
        700.0F,
        800.0F,
        900.0F,
    };
    require(!controller.configure(too_many), "more than eight thresholds are rejected");

    require(controller.threshold_count() == 2, "rejected configure leaves thresholds untouched");
    require(controller.threshold_at(0) == 300.0F, "rejected configure keeps the first threshold");
    require(controller.threshold_at(1) == 600.0F, "rejected configure keeps the second threshold");
    require(std::isnan(controller.threshold_at(2)), "threshold query past the end returns NaN");

    const std::array<float, 0> empty{};
    require(controller.configure(empty), "empty thresholds are accepted");
    require(controller.band_count() == 1, "no thresholds means a single band");

    auto update = controller.update(5000.0F);
    require(update.accepted, "width resolves without thresholds");
    require(update.variant == 0, "every width lands in band zero without thresholds");
}

void check_sweep_without_oscillation() {
    veyra::BreakpointController controller;

    const std::array<float, 4> thresholds{200.0F, 400.0F, 600.0F, 800.0F};
    require(controller.configure(thresholds), "four-threshold setup is accepted");

    std::size_t changes = 0;

    for (int step = 0; step <= 20; ++step) {
        const float width = static_cast<float>(step * 50);
        const auto update = controller.update(width);

        require(update.accepted, "sweep widths are accepted");
        require(!update.oscillating, "monotonic sweep never trips the guard");

        if (update.changed) {
            ++changes;
        }
    }

    require(changes == 4, "monotonic sweep flips once per threshold");
    require(controller.active_variant() == 4, "sweep ends in the top band");
    require(!controller.holding(), "sweep leaves the guard disengaged");
}

void check_flip_hold_release() {
    veyra::BreakpointController controller;

    const std::array<float, 1> thresholds{500.0F};
    require(controller.configure(thresholds), "single-threshold setup is accepted");

    auto update = controller.update(400.0F);
    require(update.variant == 0, "setup parks the controller in band zero");

    update = controller.update(600.0F);
    require(update.changed && !update.oscillating, "first crossing flips cleanly");

    update = controller.update(400.0F);
    require(update.changed && !update.oscillating, "first return flips cleanly");

    update = controller.update(600.0F);
    require(update.changed && !update.oscillating, "second return flips cleanly");

    update = controller.update(400.0F);
    require(update.accepted, "third return is still a valid width");
    require(update.variant == 1, "guard holds the current variant instead of flipping");
    require(!update.changed, "held update reports no change");
    require(update.oscillating, "third alternation raises the oscillation flag");
    require(controller.holding(), "controller reports the hold");

    update = controller.update(400.0F);
    require(update.variant == 1, "width on the far pole stays held");
    require(update.oscillating, "far-pole update keeps the oscillation flag");

    update = controller.update(600.0F);
    require(update.variant == 1, "first steady update stays held");
    require(update.oscillating, "one steady update is not enough to release");

    update = controller.update(600.0F);
    require(!update.oscillating, "two steady updates clear the oscillation flag");
    require(!update.changed, "release reports no change");
    require(!controller.holding(), "controller reports the release");

    update = controller.update(400.0F);
    require(update.changed && !update.oscillating, "post-release crossing flips cleanly");
}

void check_escape_releases_hold() {
    veyra::BreakpointController controller;

    const std::array<float, 2> thresholds{400.0F, 800.0F};
    require(controller.configure(thresholds), "two-threshold setup is accepted");

    controller.update(500.0F);
    controller.update(300.0F);
    controller.update(500.0F);
    controller.update(300.0F);

    auto update = controller.update(500.0F);
    require(update.oscillating, "setup reaches the held state");

    update = controller.update(900.0F);
    require(update.accepted, "escape width is accepted");
    require(update.variant == 2, "width outside the flip range escapes to its own band");
    require(update.changed, "escape reports a change");
    require(!update.oscillating, "escape clears the oscillation flag");
    require(!controller.holding(), "escape disengages the guard");
}

void check_invalid_widths() {
    auto controller = make_controller();
    controller.update(500.0F);

    const float quiet_nan = std::numeric_limits<float>::quiet_NaN();
    const float infinity = std::numeric_limits<float>::infinity();

    auto update = controller.update(quiet_nan);
    require(!update.accepted, "NaN width is rejected");

    update = controller.update(infinity);
    require(!update.accepted, "infinite width is rejected");

    update = controller.update(-1.0F);
    require(!update.accepted, "negative width is rejected");

    require(controller.active_variant() == 1, "rejected widths leave the variant untouched");
    require(!controller.holding(), "rejected widths leave the guard untouched");

    update = controller.update(500.0F);
    require(update.accepted, "valid width still resolves after rejections");
    require(!update.changed, "rejections never moved the variant");
    require(!update.oscillating, "rejections never touched the guard");
}

}

int main() {
    try {
        check_band_resolution_and_edges();
        check_configure_rejection();
        check_sweep_without_oscillation();
        check_flip_hold_release();
        check_escape_releases_hold();
        check_invalid_widths();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
