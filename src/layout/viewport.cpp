#include "veyra/layout/viewport.hpp"

#include <cmath>

namespace veyra {

namespace {

void bump_generation(std::uint64_t& generation) noexcept {
    ++generation;

    if (generation == 0) {
        generation = 1;
    }
}

}

ViewportUpdate Viewport::set_size(float width, float height) noexcept {
    if (!std::isfinite(width) || !std::isfinite(height)) {
        return {false, {}};
    }

    if (width < 0.0F || height < 0.0F) {
        return {false, {}};
    }

    if (width == width_ && height == height_) {
        return {true, {}};
    }

    width_ = width;
    height_ = height;
    bump_generation(layout_generation_);

    ViewportInvalidation invalidation;
    invalidation.bits = ViewportInvalidation::kLayout | ViewportInvalidation::kDamage;

    return {true, invalidation};
}

ViewportUpdate Viewport::set_scale(float scale) noexcept {
    if (!std::isfinite(scale)) {
        return {false, {}};
    }

    if (scale <= 0.0F) {
        return {false, {}};
    }

    if (scale == scale_) {
        return {true, {}};
    }

    scale_ = scale;
    bump_generation(layout_generation_);
    bump_generation(scale_generation_);

    ViewportInvalidation invalidation;
    invalidation.bits = ViewportInvalidation::kLayout | ViewportInvalidation::kText |
        ViewportInvalidation::kImages | ViewportInvalidation::kDamage;

    return {true, invalidation};
}

float Viewport::width() const noexcept {
    return width_;
}

float Viewport::height() const noexcept {
    return height_;
}

float Viewport::scale() const noexcept {
    return scale_;
}

bool Viewport::has_area() const noexcept {
    return width_ > 0.0F && height_ > 0.0F;
}

std::uint64_t Viewport::layout_generation() const noexcept {
    return layout_generation_;
}

std::uint64_t Viewport::scale_generation() const noexcept {
    return scale_generation_;
}

LayoutConstraints Viewport::root_constraints() const noexcept {
    LayoutConstraints constraints;
    constraints.width.minimum = width_;
    constraints.width.maximum = width_;
    constraints.height.minimum = height_;
    constraints.height.maximum = height_;

    return constraints;
}

}
