#include "veyra/core/frame_scheduler.hpp"

namespace veyra {

void FrameScheduler::request_immediate() noexcept {
    immediate_pending_ = true;
}

void FrameScheduler::set_deadline(std::optional<Deadline> deadline) noexcept {
    deadline_ = deadline;
}

bool FrameScheduler::immediate_pending() const noexcept {
    return immediate_pending_;
}

std::optional<FrameScheduler::Deadline> FrameScheduler::deadline() const noexcept {
    return deadline_;
}

bool FrameScheduler::frame_due(Deadline now) const noexcept {
    return immediate_pending_ || (deadline_.has_value() && *deadline_ <= now);
}

bool FrameScheduler::consume_frame(Deadline now) noexcept {
    if (!frame_due(now)) {
        return false;
    }

    immediate_pending_ = false;
    if (deadline_.has_value() && *deadline_ <= now) {
        deadline_.reset();
    }
    return true;
}

}
