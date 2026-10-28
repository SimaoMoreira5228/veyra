#ifndef VEYRA_MEDIA_ANIMATION_TIMELINE_HPP
#define VEYRA_MEDIA_ANIMATION_TIMELINE_HPP

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace veyra {

class AnimationTimeline {
  public:
    AnimationTimeline(
        std::span<const std::uint32_t> frame_durations_ms,
        std::uint32_t play_count = 0
    ) noexcept;

    bool valid() const noexcept;
    bool finished() const noexcept;
    std::size_t current_frame_index() const noexcept;
    std::optional<std::uint32_t> time_until_next_frame_ms() const noexcept;
    void reset() noexcept;
    bool advance(std::uint64_t elapsed_ms, bool visible = true) noexcept;

  private:
    std::span<const std::uint32_t> frame_durations_ms_;
    std::uint64_t total_duration_ms_{};
    std::uint64_t elapsed_in_loop_ms_{};
    std::uint32_t play_count_{};
    std::uint32_t completed_plays_{};
    std::size_t frame_index_{};
    std::uint32_t elapsed_in_frame_ms_{};
    bool valid_{};
    bool finished_{};

    void advance_within_loop(std::uint64_t elapsed_ms) noexcept;
    void finish() noexcept;
};

}

namespace veyra::media {
using ::veyra::AnimationTimeline;
}

namespace veyra::media::animation {
using ::veyra::AnimationTimeline;
}

#endif
