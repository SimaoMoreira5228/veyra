#include "veyra/behaviors/range.hpp"

#include <cmath>

namespace veyra {

RangeState RangeController::state() const noexcept {
    return {
        .minimum = minimum_,
        .maximum = maximum_,
        .step = step_,
        .value = value_,
        .page_step = page_step_,
        .kind = kind_,
        .disabled = disabled_,
    };
}

double RangeController::value() const noexcept {
    return value_;
}

double RangeController::minimum() const noexcept {
    return minimum_;
}

double RangeController::maximum() const noexcept {
    return maximum_;
}

double RangeController::step() const noexcept {
    return step_;
}

double RangeController::page_step() const noexcept {
    return page_step_;
}

RangeKind RangeController::kind() const noexcept {
    return kind_;
}

bool RangeController::disabled() const noexcept {
    return disabled_;
}

bool RangeController::configure(const RangeConfig& config) noexcept {
    if (!valid_domain(config)) {
        return false;
    }

    const double snapped = clamp_to(
        snap_at(
            config.minimum,
            config.step,
            clamp_to(config.value, config.minimum, config.maximum)
        ),
        config.minimum,
        config.maximum
    );

    const bool changed = minimum_ != config.minimum || maximum_ != config.maximum ||
        step_ != config.step || value_ != snapped || page_step_ != config.page_step ||
        kind_ != config.kind;

    minimum_ = config.minimum;
    maximum_ = config.maximum;
    step_ = config.step;
    value_ = snapped;
    page_step_ = config.page_step;
    kind_ = config.kind;

    return changed;
}

bool RangeController::set_disabled(bool disabled) noexcept {
    if (disabled_ == disabled) {
        return false;
    }

    disabled_ = disabled;

    return true;
}

double RangeController::fraction() const noexcept {
    if (maximum_ == minimum_) {
        return 0.0;
    }

    return (value_ - minimum_) / (maximum_ - minimum_);
}

bool RangeController::write_semantics(
    std::string_view accessible_name,
    std::string_view value_text,
    SemanticNodeData& output
) const noexcept {
    if (accessible_name.empty()) {
        return false;
    }

    SemanticRole role{SemanticRole::slider};
    if (kind_ == RangeKind::scrollbar) {
        role = SemanticRole::scrollbar;
    } else if (kind_ == RangeKind::spinbox) {
        role = SemanticRole::spinbutton;
    } else if (kind_ == RangeKind::progress) {
        role = SemanticRole::progress_indicator;
    }

    std::uint16_t states{};
    if (disabled_) {
        states = states | SemanticState::disabled;
    }

    std::uint16_t actions{};
    if (!disabled_) {
        if (kind_ == RangeKind::scrollbar) {
            actions = action_flag(SemanticAction::scroll_backward) |
                action_flag(SemanticAction::scroll_forward);
        } else if (kind_ != RangeKind::progress) {
            actions = action_flag(SemanticAction::increment) |
                action_flag(SemanticAction::decrement) | action_flag(SemanticAction::set_value);
        }
    }

    output = {
        .role = role,
        .label = accessible_name,
        .value = value_text,
        .states = states,
        .actions = actions,
    };

    return true;
}

RangeTransition RangeController::set_value(double value) noexcept {
    return commit(value);
}

RangeTransition RangeController::set_fraction(double fraction) noexcept {
    if (!std::isfinite(fraction)) {
        return {.result = RangeResult::ignored, .value = value_};
    }

    double clamped = fraction;
    if (clamped < 0.0) {
        clamped = 0.0;
    }
    if (clamped > 1.0) {
        clamped = 1.0;
    }

    return commit(minimum_ + clamped * (maximum_ - minimum_));
}

RangeTransition RangeController::increment() noexcept {
    return commit(value_ + step_);
}

RangeTransition RangeController::decrement() noexcept {
    return commit(value_ - step_);
}

RangeTransition RangeController::page_increment() noexcept {
    return commit(value_ + page_step_);
}

RangeTransition RangeController::page_decrement() noexcept {
    return commit(value_ - page_step_);
}

RangeTransition RangeController::set_to_min() noexcept {
    return commit(minimum_);
}

RangeTransition RangeController::set_to_max() noexcept {
    return commit(maximum_);
}

bool RangeController::valid_domain(const RangeConfig& config) noexcept {
    if (!std::isfinite(config.minimum)) {
        return false;
    }
    if (!std::isfinite(config.maximum)) {
        return false;
    }
    if (!std::isfinite(config.step)) {
        return false;
    }
    if (!std::isfinite(config.value)) {
        return false;
    }
    if (!std::isfinite(config.page_step)) {
        return false;
    }
    if (config.minimum > config.maximum) {
        return false;
    }
    if (config.step <= 0.0) {
        return false;
    }
    if (config.page_step <= 0.0) {
        return false;
    }

    return true;
}

double RangeController::clamp_to(double value, double minimum, double maximum) noexcept {
    if (value < minimum) {
        return minimum;
    }
    if (value > maximum) {
        return maximum;
    }

    return value;
}

double RangeController::snap_at(double minimum, double step, double clamped) noexcept {
    // NOTE: std::round halves away from zero; the grid is anchored at minimum.
    const double steps = std::round((clamped - minimum) / step);

    return minimum + steps * step;
}

double RangeController::snap(double clamped) const noexcept {
    return snap_at(minimum_, step_, clamped);
}

RangeTransition RangeController::commit(double raw) noexcept {
    if (disabled_) {
        return {.result = RangeResult::ignored, .value = value_};
    }
    if (!std::isfinite(raw)) {
        return {.result = RangeResult::ignored, .value = value_};
    }

    const double snapped = clamp_to(snap(clamp_to(raw, minimum_, maximum_)), minimum_, maximum_);
    if (snapped == value_) {
        return {.result = RangeResult::handled, .value = value_};
    }

    value_ = snapped;

    return {.result = RangeResult::changed, .value = value_};
}

}
