#ifndef VEYRA_BEHAVIORS_PULL_REFRESH_HPP
#define VEYRA_BEHAVIORS_PULL_REFRESH_HPP

#include <cmath>

namespace veyra {

class PullRefresh {
  public:
    explicit PullRefresh(double threshold_distance = 64.0) noexcept :
        threshold_(valid_distance(threshold_distance) ? threshold_distance : 64.0) {
    }

    double threshold_distance() const noexcept {
        return threshold_;
    }

    double progress() const noexcept {
        return progress_;
    }

    bool set_threshold_distance(double threshold_distance) noexcept {
        if (!valid_distance(threshold_distance) || threshold_distance == threshold_) {
            return false;
        }

        threshold_ = threshold_distance;

        return true;
    }

    bool pull(double distance) noexcept {
        if (!std::isfinite(distance)) {
            return false;
        }

        const auto next = distance <= 0.0 ? 0.0 : distance / threshold_;

        if (next == progress_) {
            return false;
        }

        progress_ = next;

        return true;
    }

    bool release() const noexcept {
        return progress_ >= 1.0;
    }

    void reset() noexcept {
        progress_ = 0.0;
    }

  private:
    static bool valid_distance(double distance) noexcept {
        return std::isfinite(distance) && distance > 0.0;
    }

    double threshold_;
    double progress_{};
};

}

namespace veyra::behaviors {
using ::veyra::PullRefresh;
}

namespace veyra::behaviors::overlay {
using ::veyra::PullRefresh;
}

#endif
