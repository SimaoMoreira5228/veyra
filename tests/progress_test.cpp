#include "veyra/behaviors/file_operation_queue.hpp"
#include "veyra/behaviors/progress.hpp"

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

void check_fraction_rejects_instead_of_clamping() {
    auto bar = veyra::ProgressController{};

    auto moved = bar.set_fraction(0.5);
    require(
        moved.result == veyra::ProgressResult::changed && moved.fraction == 0.5 &&
            bar.fraction() == 0.5,
        "a mid-range fraction commits and echoes back"
    );

    moved = bar.set_fraction(0.5);
    require(
        moved.result == veyra::ProgressResult::handled && bar.fraction() == 0.5,
        "repeating the stored fraction stays put"
    );

    for (const double bad :
         {std::numeric_limits<double>::quiet_NaN(),
          -0.25,
          1.5,
          std::numeric_limits<double>::infinity()}) {
        moved = bar.set_fraction(bad);
        require(
            moved.result == veyra::ProgressResult::invalid_input && moved.fraction == 0.5 &&
                bar.fraction() == 0.5,
            "a bad fraction is rejected with the stored value untouched"
        );
    }

    moved = bar.set_fraction(0.0);
    require(
        moved.result == veyra::ProgressResult::changed && bar.fraction() == 0.0,
        "zero commits exactly"
    );

    moved = bar.set_fraction(1.0);
    require(
        moved.result == veyra::ProgressResult::changed && bar.fraction() == 1.0,
        "one commits exactly"
    );
}

void check_flags_report_changes() {
    auto bar = veyra::ProgressController{};
    require(
        !bar.indeterminate() && !bar.cancellable() && !bar.canceled(),
        "a fresh bar is determinate, fixed, and live"
    );

    require(
        bar.set_indeterminate(true) && bar.indeterminate(),
        "entering busy mode reports a change"
    );
    require(!bar.set_indeterminate(true), "repeating busy mode stays put");

    require(
        bar.set_cancellable(true) && bar.cancellable(),
        "making the bar cancellable reports a change"
    );
    require(!bar.set_cancellable(true), "repeating cancellable stays put");

    require(
        bar.set_indeterminate(false) && !bar.indeterminate(),
        "leaving busy mode reports a change"
    );
}

void check_cancel_needs_cancellable() {
    auto bar = veyra::ProgressController{};

    require(
        bar.cancel() == veyra::ProgressResult::ignored && !bar.canceled(),
        "cancel without cancellable is a no-op"
    );

    bar.set_cancellable(true);
    require(
        bar.cancel() == veyra::ProgressResult::changed && bar.canceled(),
        "cancel once it is allowed latches the flag"
    );
    require(
        bar.cancel() == veyra::ProgressResult::handled && bar.canceled(),
        "canceling twice stays put"
    );

    bar.set_cancellable(false);
    require(
        bar.cancel() == veyra::ProgressResult::ignored && bar.canceled(),
        "losing cancellable after the fact keeps the latch without disturbing it"
    );
}

void check_reset_restores_defaults() {
    auto bar = veyra::ProgressController{};
    require(!bar.reset(), "resetting a fresh bar reports nothing to do");

    bar.set_fraction(0.75);
    bar.set_indeterminate(true);
    bar.set_cancellable(true);
    bar.cancel();

    require(bar.reset(), "resetting a worked bar reports a change");

    const auto state = bar.state();
    require(
        state.fraction == 0.0 && !state.indeterminate && !state.cancellable && !state.canceled,
        "reset restores the fresh defaults"
    );
    require(!bar.reset(), "a second reset stays put");
}

void check_queue_fraction_flows_through_the_caller() {
    auto queue = veyra::FileOperationQueue{};
    auto bar = veyra::ProgressController{};
    bar.set_cancellable(true);

    const std::uint64_t id = 7;
    require(
        queue.submit(veyra::FileOperationKind::copy, id) == veyra::FileOperationResult::changed,
        "the batch queues"
    );
    require(queue.begin(id) == veyra::FileOperationResult::started, "the batch starts");

    for (const float reported : {0.25F, 0.5F}) {
        require(
            queue.advance(id, reported) == veyra::FileOperationResult::changed,
            "the worker advance lands"
        );

        veyra::FileOperation operation{};
        require(queue.operation(id, operation), "the row reads back");

        const auto mirrored = bar.set_fraction(static_cast<double>(operation.progress));
        require(
            mirrored.result == veyra::ProgressResult::changed &&
                bar.fraction() == static_cast<double>(reported),
            "the caller mirrors the queued fraction into the bar"
        );
    }

    const auto canceled = bar.cancel();
    require(canceled == veyra::ProgressResult::changed, "the user cancel latches the bar");

    if (canceled == veyra::ProgressResult::changed) {
        require(
            queue.cancel(id) == veyra::FileOperationResult::changed,
            "the caller forwards the bar cancel to the queued batch"
        );
    }

    veyra::FileOperation operation{};
    require(
        queue.operation(id, operation) && operation.state == veyra::FileOperationState::canceled &&
            bar.canceled(),
        "both halves agree the work is canceled"
    );
}

}

int main() {
    try {
        check_fraction_rejects_instead_of_clamping();
        check_flags_report_changes();
        check_cancel_needs_cancellable();
        check_reset_restores_defaults();
        check_queue_fraction_flows_through_the_caller();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
