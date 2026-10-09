#include "veyra/behaviors/drag_drop.hpp"

#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

void require(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

constexpr veyra::LayoutPoint at(float x, float y) {
    return veyra::LayoutPoint{.x = x, .y = y};
}

void arm_pointer_drag(veyra::DragDropController& drag) {
    require(
        drag.begin(11, veyra::DragAction::move, 7, at(0.0F, 0.0F), 4.0F) ==
            veyra::DragResult::handled,
        "a fresh begin parks the gesture in pending"
    );
    require(
        drag.update(7, at(2.0F, 0.0F)) == veyra::DragResult::handled,
        "movement inside the threshold stays pending"
    );
    require(
        drag.update(7, at(4.0F, 0.0F)) == veyra::DragResult::armed,
        "movement at exactly the threshold arms the drag"
    );
    require(drag.dragging(), "arming flips the controller into dragging");
}

void check_threshold_arming() {
    auto drag = veyra::DragDropController{};
    require(drag.idle(), "a new controller owns no gesture");

    require(
        drag.begin(0, veyra::DragAction::move, 7, at(0.0F, 0.0F), 4.0F) ==
            veyra::DragResult::ignored,
        "a null source id is rejected"
    );
    require(
        drag.begin(11, veyra::DragAction::none, 7, at(0.0F, 0.0F), 4.0F) ==
            veyra::DragResult::ignored,
        "an empty offered mask is rejected"
    );
    require(
        drag.begin(11, veyra::DragAction::move, 0, at(0.0F, 0.0F), 4.0F) ==
            veyra::DragResult::ignored,
        "a null pointer id is rejected"
    );
    require(
        drag.begin(11, veyra::DragAction::move, 7, at(0.0F, 0.0F), -1.0F) ==
            veyra::DragResult::ignored,
        "a negative threshold is rejected"
    );
    require(drag.idle(), "rejected begins leave the controller idle");

    require(
        drag.begin(11, veyra::DragAction::move, 7, at(0.0F, 0.0F), 4.0F) ==
            veyra::DragResult::handled,
        "a valid begin parks the gesture in pending"
    );
    require(
        drag.begin(12, veyra::DragAction::copy, 8, at(0.0F, 0.0F), 4.0F) ==
            veyra::DragResult::ignored,
        "a second begin cannot hijack a live gesture"
    );

    require(
        drag.update(7, at(3.0F, 0.0F)) == veyra::DragResult::handled && !drag.dragging(),
        "sub-threshold movement holds the gesture in pending"
    );
    require(
        drag.update(7, at(5.0F, 0.0F)) == veyra::DragResult::armed && drag.dragging(),
        "crossing the threshold arms the drag"
    );
    require(
        drag.update(7, at(9.0F, 3.0F)) == veyra::DragResult::moved,
        "post-arm movement reports position updates"
    );

    const veyra::DragState state = drag.state();
    require(
        state.source == 11 && state.offered == veyra::DragAction::move && state.pointer == 7,
        "state reports the source, offered mask, and owning pointer"
    );
}

void check_capture_discipline() {
    auto drag = veyra::DragDropController{};
    drag.begin(11, veyra::DragAction::move, 7, at(0.0F, 0.0F), 4.0F);

    require(
        drag.update(9, at(50.0F, 50.0F)) == veyra::DragResult::ignored && !drag.dragging(),
        "a foreign pointer cannot arm the pending gesture"
    );
    require(
        drag.cancel(9) == veyra::DragResult::ignored && !drag.idle(),
        "a foreign pointer cannot cancel the pending gesture"
    );

    drag.update(7, at(5.0F, 0.0F));
    require(
        drag.update(9, at(50.0F, 50.0F)) == veyra::DragResult::ignored,
        "a second pointer is ignored once dragging"
    );
    require(drag.dragging(), "the ignored pointer leaves the drag intact");

    require(
        drag.enter_target(21, veyra::DragAction::move).result == veyra::DragResult::entered,
        "target negotiation works while the owner drags"
    );
    require(
        drag.cancel(7) == veyra::DragResult::cancelled && drag.idle(),
        "the owning pointer cancels the whole gesture"
    );
    require(!drag.over_target(), "cancellation clears the hovered target");
}

void check_negotiation_and_precedence() {
    auto drag = veyra::DragDropController{};
    drag.begin(
        11,
        veyra::DragAction::move | veyra::DragAction::copy | veyra::DragAction::link,
        7,
        at(0.0F, 0.0F),
        4.0F
    );
    drag.update(7, at(5.0F, 0.0F));

    require(
        drag.enter_target(0, veyra::DragAction::move).result == veyra::DragResult::ignored,
        "a null target id is rejected"
    );
    require(
        drag.move_target(21, veyra::DragAction::move).result == veyra::DragResult::ignored,
        "target movement before entry is rejected"
    );

    const veyra::DragFeedback entered =
        drag.enter_target(21, veyra::DragAction::copy | veyra::DragAction::link);
    require(
        entered.result == veyra::DragResult::entered && entered.accepts &&
            entered.negotiated == veyra::DragAction::copy,
        "entry reports acceptance with the negotiated action"
    );

    const veyra::DragFeedback moved =
        drag.move_target(21, veyra::DragAction::move | veyra::DragAction::copy);
    require(
        moved.result == veyra::DragResult::moved && moved.negotiated == veyra::DragAction::move,
        "move wins the intersection once the target accepts it"
    );

    const veyra::DragState state = drag.state();
    require(
        state.over_target && state.target == 21 &&
            state.accepted == (veyra::DragAction::move | veyra::DragAction::copy) &&
            state.negotiated == veyra::DragAction::move,
        "state exposes the hovered target and live negotiation"
    );

    require(
        drag.move_target(22, veyra::DragAction::move).result == veyra::DragResult::ignored,
        "movement for a different target is rejected without retargeting"
    );
    require(
        drag.leave_target(22) == veyra::DragResult::ignored && drag.over_target(),
        "leaving a different target leaves the hover intact"
    );
    require(
        drag.leave_target(21) == veyra::DragResult::left && !drag.over_target(),
        "leaving the hovered target clears the hover"
    );

    const veyra::DragDrop rejected = drag.drop();
    require(
        rejected.result == veyra::DragResult::rejected &&
            rejected.action == veyra::DragAction::none && rejected.source == 11 &&
            rejected.target == 0,
        "dropping with no hovered target rejects and names the source"
    );
    require(drag.idle(), "a rejected drop still ends the gesture");
}

void check_drop_and_rejection() {
    auto drag = veyra::DragDropController{};
    arm_pointer_drag(drag);

    drag.enter_target(21, veyra::DragAction::copy);
    const veyra::DragDrop rejected = drag.drop();
    require(
        rejected.result == veyra::DragResult::rejected &&
            rejected.action == veyra::DragAction::none && rejected.source == 11 &&
            rejected.target == 21,
        "a drop with a disjoint accept mask rejects and names both ends"
    );
    require(drag.idle(), "rejection ends the gesture");

    arm_pointer_drag(drag);
    drag.enter_target(21, veyra::DragAction::move | veyra::DragAction::copy);
    const veyra::DragDrop done = drag.drop();
    require(
        done.result == veyra::DragResult::dropped && done.action == veyra::DragAction::move &&
            done.source == 11 && done.target == 21,
        "a drop with an intersecting mask completes with the negotiated action"
    );
    require(drag.idle(), "a completed drop ends the gesture");

    require(
        drag.drop().result == veyra::DragResult::ignored,
        "dropping with no gesture is a no-op"
    );
    require(
        drag.leave_target(21) == veyra::DragResult::ignored,
        "leaving with no gesture is a no-op"
    );
}

void check_cancel_from_both_sides() {
    auto drag = veyra::DragDropController{};
    arm_pointer_drag(drag);
    drag.enter_target(21, veyra::DragAction::move);

    require(
        drag.cancel_target(22) == veyra::DragResult::ignored && drag.dragging(),
        "a foreign target cannot veto the hovered drag"
    );
    require(
        drag.cancel_target(21) == veyra::DragResult::cancelled && drag.idle(),
        "the hovered target cancels the drag"
    );

    arm_pointer_drag(drag);
    drag.enter_target(21, veyra::DragAction::move);
    require(
        drag.cancel(7) == veyra::DragResult::cancelled && drag.idle(),
        "the source pointer cancels the drag"
    );

    drag.begin(11, veyra::DragAction::move, 7, at(0.0F, 0.0F), 4.0F);
    require(
        drag.cancel(7) == veyra::DragResult::cancelled && drag.idle(),
        "cancelling while pending reports the tap as gone, not dragged"
    );
    require(drag.cancel(7) == veyra::DragResult::ignored, "cancelling with no gesture is a no-op");
}

void check_unmount_abort() {
    auto drag = veyra::DragDropController{};
    require(drag.abort() == veyra::DragResult::ignored, "aborting while idle is a no-op");

    drag.begin(11, veyra::DragAction::move, 7, at(0.0F, 0.0F), 4.0F);
    require(
        drag.abort() == veyra::DragResult::cancelled && drag.idle(),
        "aborting a pending gesture clears it"
    );

    arm_pointer_drag(drag);
    drag.enter_target(21, veyra::DragAction::move);
    require(
        drag.abort() == veyra::DragResult::cancelled && drag.idle() && !drag.over_target(),
        "aborting a hovered drag clears gesture and hover"
    );
    require(drag.drop().result == veyra::DragResult::ignored, "no drop survives an abort");
}

void check_keyboard_drag() {
    auto drag = veyra::DragDropController{};
    require(
        drag.begin_keyboard(0, veyra::DragAction::copy) == veyra::DragResult::ignored,
        "a keyboard drag still needs a source"
    );
    require(
        drag.begin_keyboard(11, veyra::DragAction::copy) == veyra::DragResult::armed,
        "a keyboard drag starts armed with no threshold to cross"
    );
    require(drag.dragging() && drag.state().pointer == 0, "a keyboard drag owns no pointer");

    require(
        drag.update(7, at(5.0F, 0.0F)) == veyra::DragResult::ignored,
        "pointer movement cannot steer a keyboard drag"
    );
    require(
        drag.cancel(7) == veyra::DragResult::ignored && drag.dragging(),
        "no pointer id cancels a keyboard drag"
    );

    drag.enter_target(21, veyra::DragAction::copy);
    const veyra::DragDrop done = drag.drop();
    require(
        done.result == veyra::DragResult::dropped && done.action == veyra::DragAction::copy,
        "a keyboard drag negotiates and drops like a pointer drag"
    );
    require(drag.idle(), "a keyboard drop ends the gesture");
}

void check_pending_gates_negotiation() {
    auto drag = veyra::DragDropController{};
    drag.begin(11, veyra::DragAction::move, 7, at(0.0F, 0.0F), 4.0F);

    require(
        drag.enter_target(21, veyra::DragAction::move).result == veyra::DragResult::ignored,
        "targets cannot be entered before the threshold arms"
    );
    require(
        drag.drop().result == veyra::DragResult::ignored,
        "dropping before the threshold arms is a no-op"
    );
    require(!drag.idle(), "gated calls leave the pending gesture alive");
}

}

int main() {
    try {
        check_threshold_arming();
        check_capture_discipline();
        check_negotiation_and_precedence();
        check_drop_and_rejection();
        check_cancel_from_both_sides();
        check_unmount_abort();
        check_keyboard_drag();
        check_pending_gates_negotiation();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
