#include "veyra/media/animation_timeline.hpp"

#include <array>
#include <cstdint>
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

void check_frame_boundaries_and_visibility() {
    const std::array<std::uint32_t, 3> durations{100, 200, 50};
    veyra::AnimationTimeline timeline(durations, 2);

    require(timeline.valid(), "positive frame durations make a valid timeline");
    require(timeline.current_frame_index() == 0, "timeline starts at its first frame");
    require(timeline.time_until_next_frame_ms() == 100, "initial frame exposes its deadline");
    timeline.advance(99);
    require(timeline.current_frame_index() == 0, "frame stays active before its duration ends");
    require(timeline.time_until_next_frame_ms() == 1, "deadline shrinks with elapsed time");
    timeline.advance(1);
    require(timeline.current_frame_index() == 1, "exact boundary advances to the next frame");
    require(timeline.time_until_next_frame_ms() == 200, "next frame starts a fresh duration");
    timeline.advance(200);
    require(timeline.current_frame_index() == 2, "elapsed time crosses multiple frame boundaries");
    timeline.advance(50);
    require(timeline.current_frame_index() == 0, "completed loop returns to the first frame");

    timeline.advance(500, false);
    require(timeline.current_frame_index() == 0, "hidden timelines do not advance");
    timeline.advance(100);
    require(timeline.current_frame_index() == 1, "visible timelines resume from the frozen point");
    timeline.advance(600);
    require(timeline.finished(), "finite play count stops after its final loop");
    require(timeline.current_frame_index() == 2, "finished timeline holds its final frame");
    require(!timeline.time_until_next_frame_ms(), "finished timeline has no next-frame deadline");

    timeline.reset();
    require(!timeline.finished(), "reset restarts a finished timeline");
    require(timeline.current_frame_index() == 0, "reset selects the first frame");
}

void check_infinite_loops_and_large_elapsed_time() {
    const std::array<std::uint32_t, 2> durations{10, 20};
    veyra::AnimationTimeline timeline(durations);

    timeline.advance(std::numeric_limits<std::uint64_t>::max());
    require(timeline.valid(), "zero play count means repeat forever");
    require(!timeline.finished(), "infinite timeline never finishes");
    require(timeline.current_frame_index() == 1, "large elapsed time wraps without overflow");
    timeline.advance(15);
    require(timeline.current_frame_index() == 0, "elapsed time wraps back to the first frame");
}

void check_invalid_durations() {
    const std::array<std::uint32_t, 0> empty{};
    const std::array<std::uint32_t, 2> contains_zero{10, 0};
    veyra::AnimationTimeline empty_timeline(empty);
    veyra::AnimationTimeline zero_duration_timeline(contains_zero);

    require(!empty_timeline.valid(), "empty animation has no timeline");
    require(!zero_duration_timeline.valid(), "zero duration frames are rejected");
    require(!empty_timeline.time_until_next_frame_ms(), "invalid timeline has no deadline");
    zero_duration_timeline.advance(100);
    require(zero_duration_timeline.current_frame_index() == 0, "invalid timeline ignores updates");
}

void check_against_millisecond_reference() {
    for (std::uint32_t first_duration = 1; first_duration <= 4; ++first_duration) {
        for (std::uint32_t second_duration = 1; second_duration <= 4; ++second_duration) {
            const std::array<std::uint32_t, 2> durations{first_duration, second_duration};
            for (std::uint32_t play_count = 0; play_count <= 4; ++play_count) {
                veyra::AnimationTimeline timeline(durations, play_count);
                std::size_t expected_frame = 0;
                std::uint32_t elapsed_in_frame = 0;
                std::uint32_t completed_plays = 0;
                bool expected_finished = false;

                for (std::uint32_t step = 0; step < 80; ++step) {
                    const auto elapsed_ms = (step * 13 + first_duration * second_duration) % 9;
                    const auto visible = step % 6 != 2;
                    timeline.advance(elapsed_ms, visible);

                    if (visible && !expected_finished) {
                        for (std::uint32_t elapsed = 0; elapsed < elapsed_ms; ++elapsed) {
                            if (expected_finished) {
                                break;
                            }
                            ++elapsed_in_frame;
                            if (elapsed_in_frame != durations[expected_frame]) {
                                continue;
                            }

                            elapsed_in_frame = 0;
                            ++expected_frame;
                            if (expected_frame != durations.size()) {
                                continue;
                            }

                            ++completed_plays;
                            if (play_count != 0 && completed_plays == play_count) {
                                expected_finished = true;
                                expected_frame = durations.size() - 1;
                                elapsed_in_frame = durations.back();
                            } else {
                                expected_frame = 0;
                            }
                        }
                    }

                    require(
                        timeline.current_frame_index() == expected_frame,
                        "timeline frame matches the stepwise reference"
                    );
                    require(
                        timeline.finished() == expected_finished,
                        "timeline completion matches the stepwise reference"
                    );
                    if (expected_finished) {
                        require(
                            !timeline.time_until_next_frame_ms(),
                            "finished timeline has no deadline"
                        );
                    } else {
                        require(
                            timeline.time_until_next_frame_ms() ==
                                durations[expected_frame] - elapsed_in_frame,
                            "timeline deadline matches the stepwise reference"
                        );
                    }
                }
            }
        }
    }
}

}

int main() {
    try {
        check_frame_boundaries_and_visibility();
        check_infinite_loops_and_large_elapsed_time();
        check_invalid_durations();
        check_against_millisecond_reference();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
