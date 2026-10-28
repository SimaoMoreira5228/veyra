#ifndef VEYRA_MEDIA_ANIMATION_INTERPOLATION_HPP
#define VEYRA_MEDIA_ANIMATION_INTERPOLATION_HPP

#include "veyra/core/tree/node_tree.hpp"
#include "veyra/foundation/color.hpp"

#include <cmath>
#include <cstdint>

namespace veyra {

enum class AnimationEasing : std::uint8_t {
    linear,
    ease_out_cubic,
    ease_in_out_cubic,
};

enum class AnimatedProperty : std::uint8_t {
    opacity,
    transform,
    color,
    geometry,
};

enum class AnimationInvalidation : std::uint8_t {
    none,
    paint,
    layout,
};

constexpr float animation_clamp01(float value) noexcept {
    if (value <= 0.0F) {
        return 0.0F;
    }

    if (value >= 1.0F) {
        return 1.0F;
    }

    return value;
}

constexpr float easing_position(AnimationEasing easing, float progress) noexcept {
    const auto clamped = animation_clamp01(progress);

    switch (easing) {
    case AnimationEasing::linear:
        return clamped;
    case AnimationEasing::ease_out_cubic: {
        const auto remaining = 1.0F - clamped;
        return 1.0F - remaining * remaining * remaining;
    }
    case AnimationEasing::ease_in_out_cubic: {
        if (clamped < 0.5F) {
            return 4.0F * clamped * clamped * clamped;
        }

        const auto folded = -2.0F * clamped + 2.0F;
        return 1.0F - folded * folded * folded / 2.0F;
    }
    }

    return clamped;
}

inline float interpolate(
    float from,
    float to,
    std::uint64_t elapsed_ms,
    std::uint64_t duration_ms,
    AnimationEasing easing
) noexcept {
    if (duration_ms == 0 || elapsed_ms >= duration_ms) {
        return to;
    }

    if (from == to) {
        return to;
    }

    const auto progress = static_cast<float>(elapsed_ms) / static_cast<float>(duration_ms);
    return from + (to - from) * easing_position(easing, progress);
}

inline float approach(
    float current,
    float target,
    std::uint64_t elapsed_ms,
    std::uint64_t time_constant_ms
) noexcept {
    if (current == target || elapsed_ms == 0) {
        return current;
    }

    if (time_constant_ms == 0) {
        return target;
    }

    const auto ratio = static_cast<float>(elapsed_ms) / static_cast<float>(time_constant_ms);
    const auto factor = 1.0F - std::exp(-ratio);
    return current + (target - current) * factor;
}

constexpr Srgba8 mix_srgba8(Srgba8 from, Srgba8 to, float progress) noexcept {
    const auto clamped = animation_clamp01(progress);

    const auto mix_channel = [clamped](std::uint8_t first, std::uint8_t second) noexcept {
        const auto value = static_cast<float>(first) + static_cast<float>(second - first) * clamped;
        return static_cast<std::uint8_t>(value + 0.5F);
    };

    return {
        .red = mix_channel(from.red, to.red),
        .green = mix_channel(from.green, to.green),
        .blue = mix_channel(from.blue, to.blue),
        .alpha = mix_channel(from.alpha, to.alpha),
    };
}

constexpr AnimationInvalidation invalidation_for(AnimatedProperty property) noexcept {
    switch (property) {
    case AnimatedProperty::opacity:
        return AnimationInvalidation::none;
    case AnimatedProperty::transform:
        return AnimationInvalidation::none;
    case AnimatedProperty::color:
        return AnimationInvalidation::paint;
    case AnimatedProperty::geometry:
        return AnimationInvalidation::layout;
    }

    return AnimationInvalidation::layout;
}

inline bool mark_animation_dirty(NodeTree& tree, NodeId id, AnimatedProperty property) noexcept {
    const auto invalidation = invalidation_for(property);

    if (invalidation == AnimationInvalidation::none) {
        return false;
    }

    if (invalidation == AnimationInvalidation::paint) {
        return tree.mark_dirty(id, DirtyCategory::paint);
    }

    return tree.mark_dirty(id, DirtyCategory::layout);
}

class ScalarAnimator {
  public:
    ScalarAnimator() noexcept = default;

    void start(
        float from,
        float target,
        std::uint64_t now_ms,
        std::uint64_t duration_ms,
        AnimationEasing easing
    ) noexcept {
        from_ = from;
        target_ = target;
        current_ = from;
        elapsed_ms_ = 0;
        duration_ms_ = duration_ms;
        last_tick_ms_ = now_ms;
        easing_ = easing;

        if (duration_ms_ == 0 || from_ == target_) {
            current_ = target_;
            running_ = false;
            return;
        }

        running_ = true;
    }

    void retarget(float target, std::uint64_t now_ms) noexcept {
        advance(now_ms);

        from_ = current_;
        target_ = target;
        elapsed_ms_ = 0;

        if (duration_ms_ == 0 || from_ == target_) {
            current_ = target_;
            running_ = false;
            return;
        }

        running_ = true;
    }

    void set_paused(bool paused) noexcept {
        paused_ = paused;
    }

    void set_throttled(bool throttled) noexcept {
        throttled_ = throttled;
    }

    void set_reduced_motion(bool reduced_motion) noexcept {
        reduced_motion_ = reduced_motion;
    }

    float advance(std::uint64_t now_ms) noexcept {
        auto delta_ms = std::uint64_t{};
        if (now_ms > last_tick_ms_) {
            delta_ms = now_ms - last_tick_ms_;
            last_tick_ms_ = now_ms;
        }

        if (!running_) {
            return current_;
        }

        if (paused_ || throttled_ || reduced_motion_) {
            return current_;
        }

        if (delta_ms == 0) {
            return current_;
        }

        const auto remaining = duration_ms_ - elapsed_ms_;
        if (delta_ms >= remaining) {
            elapsed_ms_ = duration_ms_;
            current_ = target_;
            running_ = false;
            return current_;
        }

        elapsed_ms_ += delta_ms;
        current_ = interpolate(from_, target_, elapsed_ms_, duration_ms_, easing_);
        return current_;
    }

    float value() const noexcept {
        return current_;
    }

    float target() const noexcept {
        return target_;
    }

    bool running() const noexcept {
        return running_;
    }

    bool finished() const noexcept {
        return !running_;
    }

    bool paused() const noexcept {
        return paused_;
    }

    bool throttled() const noexcept {
        return throttled_;
    }

    bool reduced_motion() const noexcept {
        return reduced_motion_;
    }

  private:
    float from_{};
    float target_{};
    float current_{};
    std::uint64_t elapsed_ms_{};
    std::uint64_t duration_ms_{};
    std::uint64_t last_tick_ms_{};
    AnimationEasing easing_{AnimationEasing::linear};
    bool running_{};
    bool paused_{};
    bool throttled_{};
    bool reduced_motion_{};
};

}

namespace veyra::media {
using ::veyra::AnimatedProperty;
using ::veyra::AnimationEasing;
using ::veyra::AnimationInvalidation;
using ::veyra::ScalarAnimator;
using ::veyra::animation_clamp01;
using ::veyra::approach;
using ::veyra::easing_position;
using ::veyra::interpolate;
using ::veyra::invalidation_for;
using ::veyra::mark_animation_dirty;
using ::veyra::mix_srgba8;
}

namespace veyra::media::animation {
using ::veyra::AnimatedProperty;
using ::veyra::AnimationEasing;
using ::veyra::AnimationInvalidation;
using ::veyra::ScalarAnimator;
using ::veyra::animation_clamp01;
using ::veyra::approach;
using ::veyra::easing_position;
using ::veyra::interpolate;
using ::veyra::invalidation_for;
using ::veyra::mark_animation_dirty;
using ::veyra::mix_srgba8;
}

#endif
