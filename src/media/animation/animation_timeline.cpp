#include "veyra/media/animation/animation_timeline.hpp"

#include <limits>

namespace veyra {

AnimationTimeline::AnimationTimeline(
    std::span<const std::uint32_t> frame_durations_ms,
    std::uint32_t play_count
) noexcept : frame_durations_ms_(frame_durations_ms), play_count_(play_count) {
    if (frame_durations_ms.empty()) {
        return;
    }

    for (const auto duration_ms : frame_durations_ms) {
        if (duration_ms == 0 ||
            total_duration_ms_ > std::numeric_limits<std::uint64_t>::max() - duration_ms) {
            total_duration_ms_ = 0;
            return;
        }
        total_duration_ms_ += duration_ms;
    }

    valid_ = true;
}

bool AnimationTimeline::valid() const noexcept {
    return valid_;
}

bool AnimationTimeline::finished() const noexcept {
    return finished_;
}

std::size_t AnimationTimeline::current_frame_index() const noexcept {
    return frame_index_;
}

std::optional<std::uint32_t> AnimationTimeline::time_until_next_frame_ms() const noexcept {
    if (!valid_ || finished_) {
        return std::nullopt;
    }

    return frame_durations_ms_[frame_index_] - elapsed_in_frame_ms_;
}

void AnimationTimeline::reset() noexcept {
    elapsed_in_loop_ms_ = 0;
    completed_plays_ = 0;
    frame_index_ = 0;
    elapsed_in_frame_ms_ = 0;
    finished_ = false;
}

bool AnimationTimeline::advance(std::uint64_t elapsed_ms, bool visible) noexcept {
    if (!valid_ || finished_ || !visible || elapsed_ms == 0) {
        return false;
    }

    const auto previous_frame_index = frame_index_;
    const auto until_loop_end = total_duration_ms_ - elapsed_in_loop_ms_;
    if (elapsed_ms < until_loop_end) {
        advance_within_loop(elapsed_ms);
        return frame_index_ != previous_frame_index;
    }

    elapsed_ms -= until_loop_end;
    if (play_count_ != 0) {
        ++completed_plays_;
        if (completed_plays_ == play_count_) {
            finish();
            return frame_index_ != previous_frame_index;
        }
    }

    frame_index_ = 0;
    elapsed_in_frame_ms_ = 0;
    elapsed_in_loop_ms_ = 0;

    if (elapsed_ms >= total_duration_ms_) {
        const auto complete_loops = elapsed_ms / total_duration_ms_;
        elapsed_ms %= total_duration_ms_;
        if (play_count_ != 0) {
            const auto remaining_plays = play_count_ - completed_plays_;
            if (complete_loops >= remaining_plays) {
                finish();
                return frame_index_ != previous_frame_index;
            }
            completed_plays_ += static_cast<std::uint32_t>(complete_loops);
        }
    }

    advance_within_loop(elapsed_ms);
    return frame_index_ != previous_frame_index;
}

void AnimationTimeline::advance_within_loop(std::uint64_t elapsed_ms) noexcept {
    elapsed_in_loop_ms_ += elapsed_ms;

    while (elapsed_ms != 0) {
        const auto frame_duration_ms = frame_durations_ms_[frame_index_];
        const auto remaining_in_frame = frame_duration_ms - elapsed_in_frame_ms_;
        if (elapsed_ms < remaining_in_frame) {
            elapsed_in_frame_ms_ += static_cast<std::uint32_t>(elapsed_ms);
            return;
        }

        elapsed_ms -= remaining_in_frame;
        elapsed_in_frame_ms_ = 0;
        ++frame_index_;
    }
}

void AnimationTimeline::finish() noexcept {
    finished_ = true;
    frame_index_ = frame_durations_ms_.size() - 1;
    elapsed_in_frame_ms_ = frame_durations_ms_[frame_index_];
    elapsed_in_loop_ms_ = total_duration_ms_;
}

}
