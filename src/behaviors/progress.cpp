#include "veyra/behaviors/progress.hpp"

#include <cmath>

namespace veyra {

ProgressState ProgressController::state() const noexcept {
    return {
        .fraction = fraction_,
        .indeterminate = indeterminate_,
        .cancellable = cancellable_,
        .canceled = canceled_,
    };
}

double ProgressController::fraction() const noexcept {
    return fraction_;
}

bool ProgressController::indeterminate() const noexcept {
    return indeterminate_;
}

bool ProgressController::cancellable() const noexcept {
    return cancellable_;
}

bool ProgressController::canceled() const noexcept {
    return canceled_;
}

ProgressTransition ProgressController::set_fraction(double fraction) noexcept {
    if (!std::isfinite(fraction) || fraction < 0.0 || fraction > 1.0) {
        return {.result = ProgressResult::invalid_input, .fraction = fraction_};
    }

    if (fraction == fraction_) {
        return {.result = ProgressResult::handled, .fraction = fraction_};
    }

    fraction_ = fraction;

    return {.result = ProgressResult::changed, .fraction = fraction_};
}

bool ProgressController::set_indeterminate(bool indeterminate) noexcept {
    if (indeterminate_ == indeterminate) {
        return false;
    }

    indeterminate_ = indeterminate;

    return true;
}

bool ProgressController::set_cancellable(bool cancellable) noexcept {
    if (cancellable_ == cancellable) {
        return false;
    }

    cancellable_ = cancellable;

    return true;
}

ProgressResult ProgressController::cancel() noexcept {
    if (!cancellable_) {
        return ProgressResult::ignored;
    }

    if (canceled_) {
        return ProgressResult::handled;
    }

    canceled_ = true;

    return ProgressResult::changed;
}

bool ProgressController::reset() noexcept {
    if (fraction_ == 0.0 && !indeterminate_ && !cancellable_ && !canceled_) {
        return false;
    }

    fraction_ = 0.0;
    indeterminate_ = false;
    cancellable_ = false;
    canceled_ = false;

    return true;
}

}
