#include "veyra/behaviors/swipe_dismiss.hpp"

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

constexpr auto kLeading = veyra::SwipeDirection::leading;
constexpr auto kTrailing = veyra::SwipeDirection::trailing;
constexpr auto kNone = veyra::SwipeDirection::none;

void check_progress_tracks_direction_for_reveal() {
    auto swipe = veyra::SwipeDismiss{};

    require(!swipe.active(), "a new swipe tracks nothing");
    require(swipe.progress() == 0.0, "a new swipe starts at zero progress");
    require(swipe.direction() == kNone, "a new swipe has no direction");
    require(swipe.threshold() == 0.5, "the default threshold matches the spec");

    require(swipe.update(0.25, kTrailing), "a finite drag reports a change");
    require(swipe.active(), "a displaced drag counts as active");
    require(swipe.progress() == 0.25, "progress stays readable for reveal-behind");
    require(swipe.direction() == kTrailing, "direction stays readable for reveal-behind");
    require(!swipe.release(), "a drag below threshold does not dismiss");

    require(!swipe.update(0.25, kTrailing), "repeating the drag reports no change");
    require(swipe.update(0.75, kLeading), "changing side reports a change");
    require(swipe.direction() == kLeading, "the latest side wins");
    require(swipe.release(), "a drag past threshold reads as dismissible");
}

void check_progress_clamps_and_rejects_junk() {
    auto swipe = veyra::SwipeDismiss{};

    require(swipe.update(4.0, kLeading), "an over-dragged swipe reports a change");
    require(swipe.progress() == 1.0, "progress clamps at full displacement");
    require(swipe.release(), "a clamped over-drag dismisses");

    require(swipe.update(-3.0, kLeading), "a pushed-back swipe reports a change");
    require(swipe.progress() == 0.0, "negative progress clamps at rest");
    require(!swipe.active(), "a clamped rest swipe counts as inactive");
    require(!swipe.release(), "a rest swipe does not dismiss");

    require(
        !swipe.update(std::numeric_limits<double>::quiet_NaN(), kLeading),
        "a non-finite drag is rejected"
    );
    require(swipe.progress() == 0.0, "a rejected drag leaves progress alone");
    require(!swipe.update(0.9, kNone), "a directionless drag is rejected");
    require(swipe.direction() == kLeading, "a rejected drag leaves direction alone");
}

void check_threshold_guards_release() {
    auto swipe = veyra::SwipeDismiss{0.25};

    require(swipe.threshold() == 0.25, "the constructor keeps a valid threshold");
    require(swipe.update(0.24, kTrailing), "a near-threshold drag reports a change");
    require(!swipe.release(), "just below threshold holds the row");
    require(swipe.update(0.25, kTrailing), "reaching threshold reports a change");
    require(swipe.release(), "exactly at threshold dismisses");

    require(!swipe.set_threshold(0.25), "repeating the threshold reports no change");
    require(swipe.set_threshold(0.9), "a valid threshold reports a change");
    require(!swipe.release(), "raising the threshold re-arms the same drag");
    require(!swipe.set_threshold(0.0), "a zero threshold is rejected");
    require(
        !swipe.set_threshold(std::numeric_limits<double>::infinity()),
        "a non-finite threshold is rejected"
    );
    require(swipe.threshold() == 0.9, "rejected thresholds leave the threshold alone");

    auto fallback = veyra::SwipeDismiss{-1.0};
    require(fallback.threshold() == 0.5, "an invalid constructor threshold falls back");
}

void check_commit_arms_record_and_settle_parks() {
    auto swipe = veyra::SwipeDismiss{};

    require(swipe.update(0.8, kTrailing), "a past-threshold drag reports a change");

    const auto dismissal = swipe.commit();
    require(dismissal.armed, "committing a past-threshold drag arms the record");
    require(dismissal.direction == kTrailing, "the record keeps the swipe side");
    require(dismissal.progress == 0.8, "the record keeps the release progress");
    require(!swipe.active(), "committing returns the tracker to idle");
    require(swipe.progress() == 0.0, "committing clears progress");
    require(swipe.direction() == kNone, "committing clears direction");

    require(swipe.update(0.1, kLeading), "a short drag reports a change");
    require(!swipe.release(), "the short drag stays below threshold");

    const auto vetoed = swipe.commit();
    require(!vetoed.armed, "committing a short drag records the veto");
    require(vetoed.direction == kLeading, "a vetoed record still keeps its side");

    require(swipe.update(0.6, kLeading), "a fresh drag reports a change");
    swipe.settle();
    require(!swipe.active(), "settling parks the tracker at idle");
    require(!swipe.release(), "a settled tracker does not dismiss");

    const auto idle = swipe.commit();
    require(!idle.armed, "committing an idle tracker stays disarmed");
    require(idle.direction == kNone, "an idle commit records no side");
}

}

int main() {
    try {
        check_progress_tracks_direction_for_reveal();
        check_progress_clamps_and_rejects_junk();
        check_threshold_guards_release();
        check_commit_arms_record_and_settle_parks();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
