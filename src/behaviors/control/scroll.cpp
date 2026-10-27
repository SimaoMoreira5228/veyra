#include "veyra/behaviors/control/scroll.hpp"

#include <algorithm>
#include <cmath>

namespace veyra {

ScrollController::ScrollController() noexcept = default;

ScrollState ScrollController::state() const noexcept {
    return {
        .content_extent = axis_.content_extent(),
        .viewport_extent = axis_.viewport_extent(),
        .offset = axis_.offset(),
        .disabled = disabled_,
        .generation = axis_.generation(),
    };
}

double ScrollController::content_extent() const noexcept {
    return axis_.content_extent();
}

double ScrollController::viewport_extent() const noexcept {
    return axis_.viewport_extent();
}

double ScrollController::offset() const noexcept {
    return axis_.offset();
}

double ScrollController::maximum_offset() const noexcept {
    return axis_.maximum_offset();
}

bool ScrollController::disabled() const noexcept {
    return disabled_;
}

std::uint64_t ScrollController::generation() const noexcept {
    return axis_.generation();
}

bool ScrollController::at_start() const noexcept {
    return axis_.offset() <= 0.0;
}

bool ScrollController::at_end() const noexcept {
    return axis_.offset() >= axis_.maximum_offset();
}

bool ScrollController::configure(double content_extent, double viewport_extent) noexcept {
    return axis_.set_extents(content_extent, viewport_extent);
}

bool ScrollController::set_disabled(bool disabled) noexcept {
    if (disabled_ == disabled) {
        return false;
    }

    disabled_ = disabled;

    return true;
}

ScrollResult ScrollController::set_offset(double offset) noexcept {
    if (disabled_) {
        return ScrollResult::ignored;
    }

    if (!std::isfinite(offset)) {
        return ScrollResult::ignored;
    }

    const double before = axis_.offset();

    axis_.set_offset(offset);

    if (axis_.offset() == before) {
        return ScrollResult::handled;
    }

    return ScrollResult::changed;
}

ScrollMove ScrollController::scroll_to(double offset) noexcept {
    return commit(offset);
}

ScrollMove ScrollController::scroll_by(double delta) noexcept {
    if (!std::isfinite(delta)) {
        return {.result = ScrollResult::ignored, .overshoot = 0.0};
    }

    return commit(axis_.offset() + delta);
}

ScrollMove ScrollController::scroll_lines(double lines, double line_height) noexcept {
    if (!std::isfinite(lines)) {
        return {.result = ScrollResult::ignored, .overshoot = 0.0};
    }

    if (!std::isfinite(line_height) || line_height <= 0.0) {
        return {.result = ScrollResult::ignored, .overshoot = 0.0};
    }

    return commit(axis_.offset() + lines * line_height);
}

ScrollMove ScrollController::scroll_pages(double pages, double page_size) noexcept {
    if (!std::isfinite(pages)) {
        return {.result = ScrollResult::ignored, .overshoot = 0.0};
    }

    if (!std::isfinite(page_size) || page_size <= 0.0) {
        return {.result = ScrollResult::ignored, .overshoot = 0.0};
    }

    return commit(axis_.offset() + pages * page_size);
}

ScrollMove ScrollController::commit(double target) noexcept {
    if (disabled_) {
        return {.result = ScrollResult::ignored, .overshoot = 0.0};
    }

    if (!std::isfinite(target)) {
        return {.result = ScrollResult::ignored, .overshoot = 0.0};
    }

    const double maximum = axis_.maximum_offset();
    const double clamped = std::clamp(target, 0.0, maximum);
    const double overshoot = target - clamped;
    const double before = axis_.offset();

    axis_.set_offset(clamped);

    if (axis_.offset() == before) {
        return {.result = ScrollResult::handled, .overshoot = overshoot};
    }

    return {.result = ScrollResult::changed, .overshoot = overshoot};
}

}
