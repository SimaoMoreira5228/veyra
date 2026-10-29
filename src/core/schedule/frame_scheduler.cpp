#include "veyra/core/schedule/frame_scheduler.hpp"

namespace veyra {

void FrameScheduler::request_immediate() noexcept {
    immediate_pending_.store(true, std::memory_order_release);
}

void FrameScheduler::set_deadline(std::optional<Deadline> deadline) noexcept {
    deadline_ = deadline;
}

bool FrameScheduler::immediate_pending() const noexcept {
    return immediate_pending_.load(std::memory_order_acquire);
}

std::optional<FrameScheduler::Deadline> FrameScheduler::deadline() const noexcept {
    return deadline_;
}

bool FrameScheduler::frame_due(Deadline now) const noexcept {
    return immediate_pending_.load(std::memory_order_acquire) ||
        (deadline_.has_value() && *deadline_ <= now);
}

bool FrameScheduler::consume_frame(Deadline now) noexcept {
    const auto immediate = immediate_pending_.exchange(false, std::memory_order_acq_rel);
    const auto deadline_due = deadline_.has_value() && *deadline_ <= now;
    if (!immediate && !deadline_due) {
        return false;
    }
    if (deadline_due) {
        deadline_.reset();
    }
    return true;
}

}
