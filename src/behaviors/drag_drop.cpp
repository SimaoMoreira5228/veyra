#include "veyra/behaviors/drag_drop.hpp"

namespace veyra {

DragState DragDropController::state() const noexcept {
    DragAction accepted = DragAction::none;

    if (over_target_) {
        accepted = accepted_;
    }

    return {
        .phase = phase_,
        .source = source_,
        .offered = offered_,
        .pointer = pointer_,
        .over_target = over_target_,
        .target = target_,
        .accepted = accepted,
        .negotiated = negotiate(offered_, accepted),
    };
}

bool DragDropController::idle() const noexcept {
    return phase_ == DragPhase::idle;
}

bool DragDropController::dragging() const noexcept {
    return phase_ == DragPhase::dragging;
}

bool DragDropController::over_target() const noexcept {
    return phase_ == DragPhase::dragging && over_target_;
}

DragResult DragDropController::begin(
    std::uint64_t source,
    DragAction offered,
    std::uint64_t pointer,
    LayoutPoint at,
    float threshold
) noexcept {
    if (phase_ != DragPhase::idle) {
        return DragResult::ignored;
    }

    if (source == 0 || offered == DragAction::none || pointer == 0) {
        return DragResult::ignored;
    }

    if (!(threshold >= 0.0F)) {
        return DragResult::ignored;
    }

    source_ = source;
    offered_ = offered;
    pointer_ = pointer;
    origin_ = at;
    position_ = at;
    threshold_ = threshold;
    phase_ = DragPhase::pending;
    return DragResult::handled;
}

DragResult DragDropController::begin_keyboard(std::uint64_t source, DragAction offered) noexcept {
    if (phase_ != DragPhase::idle) {
        return DragResult::ignored;
    }

    if (source == 0 || offered == DragAction::none) {
        return DragResult::ignored;
    }

    source_ = source;
    offered_ = offered;
    pointer_ = 0;
    origin_ = LayoutPoint{};
    position_ = LayoutPoint{};
    threshold_ = 0.0F;
    phase_ = DragPhase::dragging;
    return DragResult::armed;
}

DragResult DragDropController::update(std::uint64_t pointer, LayoutPoint at) noexcept {
    if (phase_ == DragPhase::idle) {
        return DragResult::ignored;
    }

    if (pointer_ == 0 || pointer != pointer_) {
        return DragResult::ignored;
    }

    position_ = at;

    if (phase_ == DragPhase::dragging) {
        return DragResult::moved;
    }

    const float dx = at.x - origin_.x;
    const float dy = at.y - origin_.y;

    if (dx * dx + dy * dy < threshold_ * threshold_) {
        return DragResult::handled;
    }

    phase_ = DragPhase::dragging;
    return DragResult::armed;
}

DragFeedback DragDropController::enter_target(std::uint64_t target, DragAction accept) noexcept {
    if (phase_ != DragPhase::dragging || target == 0) {
        return {};
    }

    over_target_ = true;
    target_ = target;
    accepted_ = accept;

    const DragAction negotiated = negotiate(offered_, accept);

    return {
        .result = DragResult::entered,
        .accepts = negotiated != DragAction::none,
        .negotiated = negotiated,
    };
}

DragFeedback DragDropController::move_target(std::uint64_t target, DragAction accept) noexcept {
    if (phase_ != DragPhase::dragging || !over_target_ || target != target_) {
        return {};
    }

    accepted_ = accept;

    const DragAction negotiated = negotiate(offered_, accept);

    return {
        .result = DragResult::moved,
        .accepts = negotiated != DragAction::none,
        .negotiated = negotiated,
    };
}

DragResult DragDropController::leave_target(std::uint64_t target) noexcept {
    if (phase_ != DragPhase::dragging || !over_target_ || target != target_) {
        return DragResult::ignored;
    }

    over_target_ = false;
    target_ = 0;
    accepted_ = DragAction::none;
    return DragResult::left;
}

DragDrop DragDropController::drop() noexcept {
    if (phase_ != DragPhase::dragging) {
        return {};
    }

    const DragAction negotiated = over_target_ ? negotiate(offered_, accepted_) : DragAction::none;

    DragDrop outcome{};

    if (negotiated == DragAction::none) {
        outcome.result = DragResult::rejected;
    } else {
        outcome.result = DragResult::dropped;
    }

    outcome.source = source_;
    outcome.target = over_target_ ? target_ : 0;
    outcome.action = negotiated;

    reset();
    return outcome;
}

DragResult DragDropController::cancel(std::uint64_t pointer) noexcept {
    if (phase_ == DragPhase::idle) {
        return DragResult::ignored;
    }

    if (pointer_ == 0 || pointer != pointer_) {
        return DragResult::ignored;
    }

    reset();
    return DragResult::cancelled;
}

DragResult DragDropController::cancel_target(std::uint64_t target) noexcept {
    if (phase_ != DragPhase::dragging || !over_target_ || target != target_) {
        return DragResult::ignored;
    }

    reset();
    return DragResult::cancelled;
}

DragResult DragDropController::abort() noexcept {
    if (phase_ == DragPhase::idle) {
        return DragResult::ignored;
    }

    reset();
    return DragResult::cancelled;
}

DragAction DragDropController::negotiate(DragAction offered, DragAction accepted) noexcept {
    const DragAction shared = offered & accepted;

    if (has_drag_action(shared, DragAction::move)) {
        return DragAction::move;
    }

    if (has_drag_action(shared, DragAction::copy)) {
        return DragAction::copy;
    }

    if (has_drag_action(shared, DragAction::link)) {
        return DragAction::link;
    }

    return DragAction::none;
}

void DragDropController::reset() noexcept {
    phase_ = DragPhase::idle;
    source_ = 0;
    offered_ = DragAction::none;
    pointer_ = 0;
    origin_ = LayoutPoint{};
    position_ = LayoutPoint{};
    threshold_ = 0.0F;
    over_target_ = false;
    target_ = 0;
    accepted_ = DragAction::none;
}

}
