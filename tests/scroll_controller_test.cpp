#include "veyra/behaviors/scroll.hpp"

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

bool near(double left, double right) noexcept {
    return std::abs(left - right) < 0.0001;
}

void check_offset_clamps_and_reports() {
    auto scroller = veyra::ScrollController{};

    require(scroller.configure(500.0, 100.0), "valid extents are accepted");
    require(near(scroller.maximum_offset(), 400.0), "the range is content minus viewport");
    require(scroller.at_start(), "a fresh controller sits at its leading edge");
    require(!scroller.at_end(), "a fresh controller is not at its end");

    require(
        scroller.set_offset(450.0) == veyra::ScrollResult::changed,
        "setting past the end reports a change"
    );
    require(near(scroller.offset(), 400.0), "the offset clamps to the end");
    require(scroller.at_end(), "the end flag reads the clamped offset");
    require(
        scroller.set_offset(400.0) == veyra::ScrollResult::handled,
        "repeating the offset reports no change"
    );
    require(
        scroller.set_offset(-20.0) == veyra::ScrollResult::changed && near(scroller.offset(), 0.0),
        "negative offsets clamp to the start"
    );
    require(
        scroller.set_offset(std::numeric_limits<double>::quiet_NaN()) ==
                veyra::ScrollResult::ignored &&
            near(scroller.offset(), 0.0),
        "a non-finite offset is rejected without moving"
    );

    require(!scroller.configure(-1.0, 10.0), "negative content is rejected");
    require(
        !scroller.configure(10.0, std::numeric_limits<double>::infinity()),
        "infinite viewports are rejected"
    );
    require(near(scroller.content_extent(), 500.0), "rejected geometry leaves content alone");
}

void check_scroll_reports_excess_without_moving_past_edges() {
    auto scroller = veyra::ScrollController{};

    require(scroller.configure(200.0, 100.0), "scroll extents are accepted");
    require(scroller.set_offset(90.0) == veyra::ScrollResult::changed, "an offset is set");

    const auto forward = scroller.scroll_by(25.0);
    require(forward.result == veyra::ScrollResult::changed, "partial motion reports a change");
    require(near(scroller.offset(), 100.0), "motion stops at the maximum offset");
    require(near(forward.overshoot, 15.0), "the unapplied remainder is reported");

    const auto pinned = scroller.scroll_by(10.0);
    require(
        pinned.result == veyra::ScrollResult::handled,
        "pushing past the edge reports no change"
    );
    require(near(scroller.offset(), 100.0), "excess never moves the offset");
    require(near(pinned.overshoot, 10.0), "the whole push past the edge is excess");

    const auto back = scroller.scroll_by(-130.0);
    require(back.result == veyra::ScrollResult::changed, "backward motion reports a change");
    require(near(scroller.offset(), 0.0), "motion stops at the leading edge");
    require(near(back.overshoot, -30.0), "leading-edge excess keeps its sign");

    const auto settled = scroller.scroll_by(0.0);
    require(
        settled.result == veyra::ScrollResult::handled && near(settled.overshoot, 0.0),
        "zero motion reports no change and no excess"
    );

    const auto invalid = scroller.scroll_by(std::numeric_limits<double>::infinity());
    require(
        invalid.result == veyra::ScrollResult::ignored && near(scroller.offset(), 0.0),
        "non-finite deltas leave the offset alone"
    );
}

void check_absolute_moves_report_excess() {
    auto scroller = veyra::ScrollController{};

    require(scroller.configure(200.0, 100.0), "scroll extents are accepted");

    const auto past_end = scroller.scroll_to(160.0);
    require(past_end.result == veyra::ScrollResult::changed, "an absolute move reports a change");
    require(near(scroller.offset(), 100.0), "the move clamps to the end");
    require(near(past_end.overshoot, 60.0), "the clamped-away part is reported");

    const auto before_start = scroller.scroll_to(-40.0);
    require(
        before_start.result == veyra::ScrollResult::changed,
        "a backward move reports a change"
    );
    require(near(scroller.offset(), 0.0), "the move clamps to the start");
    require(near(before_start.overshoot, -40.0), "start-side excess keeps its sign");

    const auto quiet = scroller.set_offset(500.0);
    require(
        quiet == veyra::ScrollResult::changed && near(scroller.offset(), 100.0),
        "set_offset clamps quietly"
    );
}

void check_lines_and_pages_use_caller_sizes() {
    auto scroller = veyra::ScrollController{};

    require(scroller.configure(1'000.0, 100.0), "tall content is accepted");

    const auto lines = scroller.scroll_lines(3.0, 16.0);
    require(lines.result == veyra::ScrollResult::changed, "line motion reports a change");
    require(near(scroller.offset(), 48.0), "three lines of sixteen move forty-eight");
    require(near(lines.overshoot, 0.0), "consumed motion reports no excess");

    const auto up = scroller.scroll_lines(-1.0, 16.0);
    require(
        up.result == veyra::ScrollResult::changed && near(scroller.offset(), 32.0),
        "negative lines move back"
    );

    const auto page = scroller.scroll_pages(1.0, 100.0);
    require(
        page.result == veyra::ScrollResult::changed && near(scroller.offset(), 132.0),
        "one page moves the viewport size"
    );

    const auto bad_step = scroller.scroll_lines(2.0, 0.0);
    require(
        bad_step.result == veyra::ScrollResult::ignored && near(scroller.offset(), 132.0),
        "a zero line height is rejected"
    );

    const auto bad_page = scroller.scroll_pages(1.0, -50.0);
    require(
        bad_page.result == veyra::ScrollResult::ignored && near(scroller.offset(), 132.0),
        "a negative page size is rejected"
    );

    const auto bad_count = scroller.scroll_lines(std::numeric_limits<double>::quiet_NaN(), 16.0);
    require(
        bad_count.result == veyra::ScrollResult::ignored && near(scroller.offset(), 132.0),
        "a non-finite line count is rejected"
    );

    require(
        scroller.state().offset == scroller.offset() &&
            scroller.state().content_extent == 1'000.0 &&
            scroller.state().viewport_extent == 100.0 && !scroller.state().disabled,
        "the state snapshot mirrors the controller"
    );
}

void check_generation_tracks_committed_moves_only() {
    auto scroller = veyra::ScrollController{};
    require(scroller.generation() == 0, "a fresh controller starts at generation zero");

    require(scroller.configure(500.0, 100.0), "initial geometry is accepted");
    require(scroller.generation() == 1, "the first configure bumps the generation");
    require(
        scroller.state().generation == scroller.generation(),
        "the state snapshot mirrors the generation"
    );

    require(scroller.configure(500.0, 100.0), "identical geometry is accepted");
    require(scroller.generation() == 1, "a no-op configure does not bump");

    require(
        scroller.set_offset(0.0) == veyra::ScrollResult::handled,
        "repeating the offset reports no change"
    );
    require(scroller.generation() == 1, "a no-op set does not bump");

    require(
        scroller.set_offset(50.0) == veyra::ScrollResult::changed,
        "a moved offset reports a change"
    );
    require(scroller.generation() == 2, "a committed offset bumps the generation");

    const auto settled = scroller.generation();
    const auto quiet = scroller.scroll_by(0.0);
    require(
        quiet.result == veyra::ScrollResult::handled && near(quiet.overshoot, 0.0),
        "a zero delta reports no change"
    );
    require(scroller.generation() == settled, "a zero delta does not bump");

    require(scroller.set_disabled(true), "disabling reports a state change");
    require(scroller.generation() == settled, "toggling disabled does not bump");

    require(scroller.set_disabled(false), "enabling reports a state change");
    require(
        scroller.scroll_by(10.0).result == veyra::ScrollResult::changed,
        "motion works after enabling"
    );
    require(scroller.generation() == settled + 1, "motion after enabling bumps once");
}

void check_disabled_rejects_intent_but_keeps_reads() {
    auto scroller = veyra::ScrollController{};

    require(scroller.configure(200.0, 100.0), "scroll extents are accepted");
    require(scroller.set_disabled(true), "disabling reports a state change");

    require(
        scroller.set_offset(50.0) == veyra::ScrollResult::ignored && near(scroller.offset(), 0.0),
        "offset sets are rejected while disabled"
    );

    const auto held = scroller.scroll_by(30.0);
    require(
        held.result == veyra::ScrollResult::ignored && near(scroller.offset(), 0.0) &&
            near(held.overshoot, 0.0),
        "deltas are rejected while disabled with no excess"
    );

    require(!scroller.set_disabled(true), "repeating the flag reports no change");
    require(scroller.state().disabled, "disabled state stays observable");
    require(scroller.configure(300.0, 100.0), "geometry still updates while disabled");
    require(near(scroller.maximum_offset(), 200.0), "disabled geometry reads work");

    require(scroller.set_disabled(false), "enabling reports a state change");
    require(
        scroller.scroll_by(30.0).result == veyra::ScrollResult::changed &&
            near(scroller.offset(), 30.0),
        "motion works again after enabling"
    );
}

}

int main() {
    try {
        check_offset_clamps_and_reports();
        check_scroll_reports_excess_without_moving_past_edges();
        check_absolute_moves_report_excess();
        check_lines_and_pages_use_caller_sizes();
        check_generation_tracks_committed_moves_only();
        check_disabled_rejects_intent_but_keeps_reads();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
