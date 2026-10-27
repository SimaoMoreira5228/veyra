#include "veyra/render/damage/damage_tracker.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace veyra {
namespace {

float round_down(double value) noexcept {
    auto result = static_cast<float>(value);
    if (static_cast<double>(result) > value) {
        result = std::nextafter(result, -std::numeric_limits<float>::infinity());
    }
    return result;
}

float round_up(double value) noexcept {
    auto result = static_cast<float>(value);
    if (static_cast<double>(result) < value) {
        result = std::nextafter(result, std::numeric_limits<float>::infinity());
    }
    return result;
}

}

DamageTracker::DamageTracker(
    std::size_t surface_width,
    std::size_t surface_height,
    std::span<LayoutRect> region_storage
) noexcept :
    surface_width_(surface_width), surface_height_(surface_height),
    region_storage_(region_storage) {
    const auto maximum_width = static_cast<double>(std::numeric_limits<float>::max());
    if (region_storage.empty() || static_cast<double>(surface_width) > maximum_width ||
        static_cast<double>(surface_height) > maximum_width) {
        return;
    }

    valid_ = true;
}

bool DamageTracker::valid() const noexcept {
    return valid_;
}

void DamageTracker::clear() noexcept {
    region_count_ = 0;
    full_surface_ = false;
}

bool DamageTracker::mark_full() noexcept {
    if (!valid_) {
        return false;
    }

    region_storage_[0] = {
        .width = round_up(static_cast<double>(surface_width_)),
        .height = round_up(static_cast<double>(surface_height_)),
    };
    region_count_ = 1;
    full_surface_ = true;
    return true;
}

bool DamageTracker::add(LayoutRect region) noexcept {
    if (!valid_ || !valid_region(region)) {
        return false;
    }
    if (full_surface_ || region.width == 0.0F || region.height == 0.0F) {
        return true;
    }

    auto merged = clip_to_surface(region);
    if (merged.width == 0.0F || merged.height == 0.0F) {
        return true;
    }

    auto index = std::size_t{0};
    auto merged_bounds = pixel_bounds(merged);
    while (index < region_count_) {
        if (!overlaps(merged_bounds, pixel_bounds(region_storage_[index]))) {
            ++index;
            continue;
        }

        merged = unite(merged, region_storage_[index]);
        merged_bounds = pixel_bounds(merged);
        --region_count_;
        region_storage_[index] = region_storage_[region_count_];
        index = 0;
    }

    if (region_count_ == region_storage_.size()) {
        return mark_full();
    }

    region_storage_[region_count_] = merged;
    ++region_count_;
    return true;
}

std::span<const LayoutRect> DamageTracker::regions() const noexcept {
    return {region_storage_.data(), region_count_};
}

bool DamageTracker::full_surface() const noexcept {
    return full_surface_;
}

bool DamageTracker::valid_region(const LayoutRect& region) noexcept {
    return std::isfinite(region.x) && std::isfinite(region.y) && std::isfinite(region.width) &&
        std::isfinite(region.height) && region.width >= 0.0F && region.height >= 0.0F;
}

DamageTracker::PixelBounds DamageTracker::pixel_bounds(const LayoutRect& region) const noexcept {
    const auto maximum_x = static_cast<double>(surface_width_);
    const auto maximum_y = static_cast<double>(surface_height_);
    const auto left = std::clamp(static_cast<double>(region.x), 0.0, maximum_x);
    const auto top = std::clamp(static_cast<double>(region.y), 0.0, maximum_y);
    const auto right = std::clamp(static_cast<double>(region.x) + region.width, 0.0, maximum_x);
    const auto bottom = std::clamp(static_cast<double>(region.y) + region.height, 0.0, maximum_y);

    return {
        .left = std::floor(left),
        .top = std::floor(top),
        .right = std::ceil(right),
        .bottom = std::ceil(bottom),
    };
}

LayoutRect DamageTracker::clip_to_surface(const LayoutRect& region) const noexcept {
    const auto maximum_x = static_cast<double>(surface_width_);
    const auto maximum_y = static_cast<double>(surface_height_);
    const auto left = std::clamp(static_cast<double>(region.x), 0.0, maximum_x);
    const auto top = std::clamp(static_cast<double>(region.y), 0.0, maximum_y);
    const auto right = std::clamp(static_cast<double>(region.x) + region.width, 0.0, maximum_x);
    const auto bottom = std::clamp(static_cast<double>(region.y) + region.height, 0.0, maximum_y);
    if (right <= left || bottom <= top) {
        return {};
    }

    const auto x = round_down(left);
    const auto y = round_down(top);
    return {
        .x = x,
        .y = y,
        .width = round_up(right - x),
        .height = round_up(bottom - y),
    };
}

LayoutRect DamageTracker::unite(const LayoutRect& left, const LayoutRect& right) noexcept {
    const auto x = std::min(static_cast<double>(left.x), static_cast<double>(right.x));
    const auto y = std::min(static_cast<double>(left.y), static_cast<double>(right.y));
    const auto right_edge = std::max(
        static_cast<double>(left.x) + left.width,
        static_cast<double>(right.x) + right.width
    );
    const auto bottom_edge = std::max(
        static_cast<double>(left.y) + left.height,
        static_cast<double>(right.y) + right.height
    );
    const auto rounded_x = round_down(x);
    const auto rounded_y = round_down(y);

    return {
        .x = rounded_x,
        .y = rounded_y,
        .width = round_up(right_edge - rounded_x),
        .height = round_up(bottom_edge - rounded_y),
    };
}

bool DamageTracker::overlaps(PixelBounds left, PixelBounds right) noexcept {
    return left.left < right.right && right.left < left.right && left.top < right.bottom &&
        right.top < left.bottom;
}

}
