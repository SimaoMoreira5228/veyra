#include "veyra/behaviors/pull_refresh.hpp"

#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

void require(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

void check_pull_tracks_progress_past_full() {
    auto gesture = veyra::PullRefresh{};

    require(gesture.progress() == 0.0, "a new gesture starts at zero progress");
    require(gesture.threshold_distance() == 64.0, "the default threshold matches the spec");
    require(!gesture.release(), "a new gesture does not refresh");

    require(gesture.pull(32.0), "a half-threshold pull reports a change");
    require(gesture.progress() == 0.5, "progress tracks pulled distance");
    require(!gesture.release(), "a half pull does not refresh");

    require(!gesture.pull(32.0), "repeating the pull reports no change");
    require(gesture.pull(96.0), "an over-pull reports a change");
    require(gesture.progress() == 1.5, "progress runs past full while over-pulled");
    require(gesture.release(), "an over-pull releases into a refresh");

    require(gesture.pull(-10.0), "a pushed-back pull reports a change");
    require(gesture.progress() == 0.0, "negative distance clamps at rest");
    require(!gesture.pull(-10.0), "repeating rest reports no change");
    require(
        !gesture.pull(std::numeric_limits<double>::quiet_NaN()),
        "a non-finite pull is rejected"
    );
    require(gesture.progress() == 0.0, "a rejected pull leaves progress alone");
}

void check_threshold_guards_release() {
    auto gesture = veyra::PullRefresh{100.0};

    require(gesture.pull(99.0), "a near-threshold pull reports a change");
    require(!gesture.release(), "just below threshold holds the gesture");
    require(gesture.pull(100.0), "reaching threshold reports a change");
    require(gesture.release(), "exactly at threshold releases");

    require(!gesture.set_threshold_distance(100.0), "repeating the threshold reports no change");
    require(gesture.set_threshold_distance(50.0), "a valid threshold reports a change");
    require(gesture.release(), "shrinking the threshold keeps the release armed");
    require(!gesture.set_threshold_distance(0.0), "a zero threshold is rejected");
    require(
        !gesture.set_threshold_distance(std::numeric_limits<double>::infinity()),
        "a non-finite threshold is rejected"
    );
    require(gesture.threshold_distance() == 50.0, "rejected thresholds leave distance alone");

    gesture.reset();
    require(gesture.progress() == 0.0, "reset parks progress at rest");
    require(!gesture.release(), "a reset gesture does not refresh");

    auto fallback = veyra::PullRefresh{-4.0};
    require(fallback.threshold_distance() == 64.0, "an invalid threshold falls back");
}

}

int main() {
    try {
        check_pull_tracks_progress_past_full();
        check_threshold_guards_release();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
