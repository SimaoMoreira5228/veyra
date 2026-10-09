#include "veyra/layout/breakpoints.hpp"

#include <cmath>
#include <limits>

namespace veyra {

bool BreakpointController::configure(std::span<const float> thresholds) noexcept {
    if (thresholds.size() > kMaxThresholds) {
        return false;
    }

    float previous = -1.0F;

    for (float threshold : thresholds) {
        if (!std::isfinite(threshold)) {
            return false;
        }

        if (threshold < 0.0F) {
            return false;
        }

        if (threshold <= previous) {
            return false;
        }

        previous = threshold;
    }

    for (std::size_t i = 0; i < thresholds.size(); ++i) {
        thresholds_[i] = thresholds[i];
    }

    count_ = thresholds.size();
    active_ = 0;
    previous_ = 0;
    alternations_ = 0;
    steady_ = 0;
    holding_ = false;

    return true;
}

BreakpointUpdate BreakpointController::update(float width) noexcept {
    if (!std::isfinite(width)) {
        return {false, active_, false, holding_};
    }

    if (width < 0.0F) {
        return {false, active_, false, holding_};
    }

    const std::size_t candidate = resolve(width);

    if (holding_) {
        if (candidate == active_) {
            ++steady_;

            if (steady_ >= kReleaseSteady) {
                holding_ = false;
                alternations_ = 0;
                steady_ = 0;

                return {true, active_, false, false};
            }

            return {true, active_, false, true};
        }

        if (candidate != previous_) {
            previous_ = active_;
            active_ = candidate;
            alternations_ = 0;
            steady_ = 0;
            holding_ = false;

            return {true, active_, true, false};
        }

        return {true, active_, false, true};
    }

    if (candidate == active_) {
        alternations_ = 0;

        return {true, active_, false, false};
    }

    if (candidate == previous_) {
        ++alternations_;

        if (alternations_ >= kFlipBound) {
            holding_ = true;
            steady_ = 0;

            return {true, active_, false, true};
        }
    } else {
        alternations_ = 0;
    }

    previous_ = active_;
    active_ = candidate;

    return {true, active_, true, false};
}

std::size_t BreakpointController::active_variant() const noexcept {
    return active_;
}

std::size_t BreakpointController::band_count() const noexcept {
    return count_ + 1;
}

std::size_t BreakpointController::threshold_count() const noexcept {
    return count_;
}

float BreakpointController::threshold_at(std::size_t index) const noexcept {
    if (index >= count_) {
        return std::numeric_limits<float>::quiet_NaN();
    }

    return thresholds_[index];
}

bool BreakpointController::holding() const noexcept {
    return holding_;
}

std::size_t BreakpointController::resolve(float width) const noexcept {
    std::size_t variant = 0;

    for (std::size_t i = 0; i < count_; ++i) {
        if (width < thresholds_[i]) {
            break;
        }

        ++variant;
    }

    return variant;
}

}
