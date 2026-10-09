#include "veyra/layout/container_queries.hpp"

#include <limits>

namespace veyra {

bool ContainerQuerySet::configure(std::span<const float> thresholds) noexcept {
    BreakpointController probe;

    if (!probe.configure(thresholds)) {
        return false;
    }

    for (std::size_t i = 0; i < thresholds.size(); ++i) {
        thresholds_[i] = thresholds[i];
    }

    threshold_count_ = thresholds.size();

    for (std::size_t i = 0; i < count_; ++i) {
        slots_[i].controller.configure(thresholds);
    }

    return true;
}

bool ContainerQuerySet::attach(std::uint64_t id) noexcept {
    if (id == 0) {
        return false;
    }

    if (find(id) != kMaxContainers) {
        return false;
    }

    if (count_ >= kMaxContainers) {
        return false;
    }

    Slot fresh{};

    fresh.id = id;
    fresh.controller.configure(std::span<const float>{thresholds_.data(), threshold_count_});
    slots_[count_] = fresh;
    ++count_;

    return true;
}

bool ContainerQuerySet::detach(std::uint64_t id) noexcept {
    const std::size_t index = find(id);

    if (index == kMaxContainers) {
        return false;
    }

    --count_;
    slots_[index] = slots_[count_];

    return true;
}

BreakpointUpdate ContainerQuerySet::update(std::uint64_t id, float width) noexcept {
    const std::size_t index = find(id);

    if (index == kMaxContainers) {
        return {false, 0, false, false};
    }

    return slots_[index].controller.update(width);
}

ContainerVariant ContainerQuerySet::variant_of(std::uint64_t id) const noexcept {
    const std::size_t index = find(id);

    if (index == kMaxContainers) {
        return {false, 0, false};
    }

    const BreakpointController& controller = slots_[index].controller;

    return {true, controller.active_variant(), controller.holding()};
}

std::size_t ContainerQuerySet::container_count() const noexcept {
    return count_;
}

std::size_t ContainerQuerySet::band_count() const noexcept {
    return threshold_count_ + 1;
}

std::size_t ContainerQuerySet::threshold_count() const noexcept {
    return threshold_count_;
}

float ContainerQuerySet::threshold_at(std::size_t index) const noexcept {
    if (index >= threshold_count_) {
        return std::numeric_limits<float>::quiet_NaN();
    }

    return thresholds_[index];
}

std::size_t ContainerQuerySet::find(std::uint64_t id) const noexcept {
    for (std::size_t i = 0; i < count_; ++i) {
        if (slots_[i].id == id) {
            return i;
        }
    }

    return kMaxContainers;
}

}
