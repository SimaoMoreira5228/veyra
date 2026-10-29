#ifndef VEYRA_CORE_FRAME_SCHEDULER_HPP
#define VEYRA_CORE_FRAME_SCHEDULER_HPP

#include <atomic>
#include <chrono>
#include <optional>

namespace veyra {

class FrameScheduler {
  public:
    using Clock = std::chrono::steady_clock;
    using Deadline = Clock::time_point;

    void request_immediate() noexcept;
    void set_deadline(std::optional<Deadline> deadline) noexcept;
    bool immediate_pending() const noexcept;
    std::optional<Deadline> deadline() const noexcept;
    bool frame_due(Deadline now) const noexcept;
    bool consume_frame(Deadline now) noexcept;

  private:
    std::optional<Deadline> deadline_;
    std::atomic<bool> immediate_pending_{};
};

}

namespace veyra::core {
using ::veyra::FrameScheduler;
}

namespace veyra::core::schedule {
using ::veyra::FrameScheduler;
}

#endif
