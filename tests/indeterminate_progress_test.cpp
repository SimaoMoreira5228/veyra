#include "veyra/behaviors/input/range.hpp"

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

void check_position_loops_over_caller_time() {
    require(
        veyra::indeterminate_progress_fraction(0, 1000) == 0.0,
        "a fresh spinner starts at zero"
    );
    require(
        veyra::indeterminate_progress_fraction(250, 1000) == 0.25,
        "a quarter period reads a quarter"
    );
    require(veyra::indeterminate_progress_fraction(500, 1000) == 0.5, "a half period reads a half");
    require(
        veyra::indeterminate_progress_fraction(1000, 1000) == 0.0,
        "a full period wraps to zero"
    );
    require(
        veyra::indeterminate_progress_fraction(1250, 1000) == 0.25,
        "position repeats every period"
    );
    require(
        veyra::indeterminate_progress_fraction(5000, 1000) == 0.0,
        "whole multiples of the period all read zero"
    );
}

void check_degenerate_period_and_frozen_time() {
    require(veyra::indeterminate_progress_fraction(500, 0) == 0.0, "a zero period never divides");

    const auto first = veyra::indeterminate_progress_fraction(750, 1200);
    const auto second = veyra::indeterminate_progress_fraction(750, 1200);

    require(first == 0.625, "an off-beat sample reads its exact share");
    require(first == second, "frozen caller time freezes the bar");
}

void check_busy_record_pairs_with_progress_role() {
    auto control = veyra::RangeController{};

    require(
        control.configure(
            {.minimum = 0.0,
             .maximum = 100.0,
             .step = 1.0,
             .value = 0.0,
             .page_step = 10.0,
             .kind = veyra::RangeKind::progress}
        ),
        "a progress domain configures cleanly"
    );

    auto record = veyra::SemanticNodeData{};

    require(
        control.write_semantics("Sync", {}, record) &&
            record.role == veyra::SemanticRole::progress_indicator && record.actions == 0,
        "progress stays role plus text with no actions"
    );

    record.states = record.states | veyra::SemanticState::busy;

    require(
        veyra::has_state(record.states, veyra::SemanticState::busy) &&
            record.role == veyra::SemanticRole::progress_indicator && record.actions == 0,
        "the app marks busy on its own record without touching the role"
    );
}

}

int main() {
    try {
        check_position_loops_over_caller_time();
        check_degenerate_period_and_frozen_time();
        check_busy_record_pairs_with_progress_role();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
