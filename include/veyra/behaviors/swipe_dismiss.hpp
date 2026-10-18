#ifndef VEYRA_BEHAVIORS_SWIPE_DISMISS_HPP
#define VEYRA_BEHAVIORS_SWIPE_DISMISS_HPP

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace veyra {

enum class SwipeDirection : std::uint8_t {
    none,
    leading,
    trailing,
};

struct SwipeDismissal {
    bool armed{};
    SwipeDirection direction{SwipeDirection::none};
    double progress{};
};

class SwipeDismiss {
  public:
    explicit SwipeDismiss(double threshold = 0.5) noexcept :
        threshold_(valid_threshold(threshold) ? threshold : 0.5) {
    }

    double threshold() const noexcept {
        return threshold_;
    }

    double progress() const noexcept {
        return progress_;
    }

    SwipeDirection direction() const noexcept {
        return direction_;
    }

    bool active() const noexcept {
        return direction_ != SwipeDirection::none && progress_ > 0.0;
    }

    bool set_threshold(double threshold) noexcept {
        if (!valid_threshold(threshold) || threshold == threshold_) {
            return false;
        }

        threshold_ = threshold;

        return true;
    }

    bool update(double progress, SwipeDirection direction) noexcept {
        if (!std::isfinite(progress) || direction == SwipeDirection::none) {
            return false;
        }

        const auto clamped = std::clamp(progress, 0.0, 1.0);

        if (clamped == progress_ && direction == direction_) {
            return false;
        }

        progress_ = clamped;
        direction_ = direction;

        return true;
    }

    bool release() const noexcept {
        return direction_ != SwipeDirection::none && progress_ >= threshold_;
    }

    SwipeDismissal commit() noexcept {
        const auto record = SwipeDismissal{
            .armed = release(),
            .direction = direction_,
            .progress = progress_,
        };

        settle();

        return record;
    }

    void settle() noexcept {
        progress_ = 0.0;
        direction_ = SwipeDirection::none;
    }

  private:
    static bool valid_threshold(double threshold) noexcept {
        return std::isfinite(threshold) && threshold > 0.0 && threshold <= 1.0;
    }

    double threshold_;
    double progress_{};
    SwipeDirection direction_{SwipeDirection::none};
};

}

#endif
