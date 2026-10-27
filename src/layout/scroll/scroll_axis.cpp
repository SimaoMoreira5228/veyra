#include "veyra/layout/scroll/scroll_axis.hpp"

#include <algorithm>
#include <cmath>

namespace veyra {

bool ScrollAxis::set_extents(double content_extent, double viewport_extent) noexcept {
    if (!std::isfinite(content_extent) || content_extent < 0.0 || !std::isfinite(viewport_extent) ||
        viewport_extent < 0.0) {
        return false;
    }

    const bool extents_changed =
        content_extent != content_extent_ || viewport_extent != viewport_extent_;

    content_extent_ = content_extent;
    viewport_extent_ = viewport_extent;

    const double clamped = std::min(offset_, maximum_offset());

    if (!extents_changed && clamped == offset_) {
        return true;
    }

    offset_ = clamped;
    ++generation_;

    return true;
}

bool ScrollAxis::set_offset(double offset) noexcept {
    if (!std::isfinite(offset)) {
        return false;
    }

    const double clamped = std::clamp(offset, 0.0, maximum_offset());

    if (clamped == offset_) {
        return true;
    }

    offset_ = clamped;
    ++generation_;

    return true;
}

bool ScrollAxis::scroll_by(double delta, double& unconsumed_delta) noexcept {
    if (!std::isfinite(delta)) {
        return false;
    }

    const auto maximum = maximum_offset();
    const auto applied_delta = std::clamp(delta, -offset_, maximum - offset_);
    const auto next_offset = std::clamp(offset_ + applied_delta, 0.0, maximum);
    const auto remaining_delta = delta - applied_delta;
    unconsumed_delta = remaining_delta;

    if (next_offset == offset_) {
        return true;
    }

    offset_ = next_offset;
    ++generation_;

    return true;
}

void ScrollAxis::set_generation(std::uint64_t generation) noexcept {
    generation_ = generation;
}

double ScrollAxis::content_extent() const noexcept {
    return content_extent_;
}

double ScrollAxis::viewport_extent() const noexcept {
    return viewport_extent_;
}

double ScrollAxis::offset() const noexcept {
    return offset_;
}

double ScrollAxis::maximum_offset() const noexcept {
    return std::max(0.0, content_extent_ - viewport_extent_);
}

std::uint64_t ScrollAxis::generation() const noexcept {
    return generation_;
}

}
