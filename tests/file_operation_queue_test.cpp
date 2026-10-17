#include "veyra/behaviors/file_operation_queue.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string_view>

namespace {

void require(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

void check_submit_and_capacity() {
    auto queue = veyra::FileOperationQueue{};
    require(queue.count() == 0 && queue.pending_count() == 0, "a new queue is empty");

    require(
        queue.submit(veyra::FileOperationKind::copy, 0) == veyra::FileOperationResult::ignored,
        "a zero id is rejected"
    );
    require(
        queue.submit(veyra::FileOperationKind::copy, 7) == veyra::FileOperationResult::changed &&
            queue.count() == 1,
        "submitting queues the operation"
    );
    require(
        queue.submit(veyra::FileOperationKind::move, 7) == veyra::FileOperationResult::ignored &&
            queue.count() == 1,
        "a duplicate id is rejected without growing"
    );

    for (auto id = std::uint64_t{8}; id < 7 + veyra::FileOperationQueue::kCapacity; ++id) {
        require(
            queue.submit(veyra::FileOperationKind::remove, id) ==
                veyra::FileOperationResult::changed,
            "filling to capacity succeeds"
        );
    }
    require(
        queue.submit(veyra::FileOperationKind::copy, 1000) == veyra::FileOperationResult::overflow,
        "one past capacity overflows"
    );
    require(queue.pending_count() == queue.count(), "queued work is all pending");
}

void check_begin_and_progress() {
    auto queue = veyra::FileOperationQueue{};
    queue.submit(veyra::FileOperationKind::copy, 1);

    require(
        queue.begin(2) == veyra::FileOperationResult::ignored,
        "beginning an unknown id is ignored"
    );
    require(
        queue.begin(1) == veyra::FileOperationResult::started,
        "beginning a queued operation starts it"
    );
    require(
        queue.begin(1) == veyra::FileOperationResult::handled,
        "beginning an active operation stays put"
    );
    require(
        queue.advance(1, 0.5F) == veyra::FileOperationResult::changed,
        "advancing an active operation reports a change"
    );
    require(
        queue.advance(1, 0.5F) == veyra::FileOperationResult::handled,
        "repeating the same fraction stays put"
    );
    require(
        queue.advance(1, std::numeric_limits<float>::quiet_NaN()) ==
            veyra::FileOperationResult::ignored,
        "a NaN fraction is rejected"
    );
    require(
        queue.advance(1, 1.5F) == veyra::FileOperationResult::ignored,
        "a fraction past full is rejected"
    );
    require(
        queue.advance(2, 0.5F) == veyra::FileOperationResult::ignored,
        "advancing an unknown id is ignored"
    );

    veyra::FileOperation tracked{};
    require(queue.operation(1, tracked) && tracked.progress == 0.5F, "progress reads back");
    require(!queue.operation(2, tracked), "an unknown id reads nothing");

    queue.submit(veyra::FileOperationKind::move, 2);
    require(
        queue.advance(2, 0.5F) == veyra::FileOperationResult::ignored,
        "advancing a queued operation is ignored"
    );
}

void check_pause_and_resume() {
    auto queue = veyra::FileOperationQueue{};
    queue.submit(veyra::FileOperationKind::copy, 1);
    queue.submit(veyra::FileOperationKind::copy, 2);
    queue.begin(1);

    require(
        queue.pause(2) == veyra::FileOperationResult::ignored,
        "pausing a queued operation is ignored"
    );
    require(
        queue.pause(1) == veyra::FileOperationResult::changed,
        "pausing an active operation pauses it"
    );
    require(
        queue.pause(1) == veyra::FileOperationResult::handled,
        "pausing a paused operation stays put"
    );
    require(
        queue.advance(1, 0.75F) == veyra::FileOperationResult::ignored,
        "progress while paused is ignored"
    );
    require(
        queue.resume(2) == veyra::FileOperationResult::ignored,
        "resuming a queued operation is ignored"
    );
    require(
        queue.resume(1) == veyra::FileOperationResult::changed,
        "resuming reactivates the operation"
    );
    require(
        queue.resume(1) == veyra::FileOperationResult::handled,
        "resuming an active operation stays put"
    );
    require(
        queue.complete(1) == veyra::FileOperationResult::changed,
        "a reactivated operation still completes"
    );
}

void check_fail_retry_and_conflict() {
    auto queue = veyra::FileOperationQueue{};
    queue.submit(veyra::FileOperationKind::move, 1);
    queue.begin(1);

    require(
        queue.fail(1, veyra::FileOperationFault::conflict) == veyra::FileOperationResult::changed,
        "failing an active operation fails it"
    );

    veyra::FileOperation tracked{};
    require(
        queue.operation(1, tracked) && tracked.conflict &&
            tracked.state == veyra::FileOperationState::failed,
        "a conflict fault is recorded on the operation"
    );
    require(
        queue.fail(1, veyra::FileOperationFault::error) == veyra::FileOperationResult::handled,
        "failing a failed operation stays put"
    );
    require(
        queue.retry(2) == veyra::FileOperationResult::ignored,
        "retrying an unknown id is ignored"
    );
    require(
        queue.retry(1) == veyra::FileOperationResult::changed,
        "retrying re-queues the operation"
    );
    require(
        queue.operation(1, tracked) && tracked.attempts == 1 && tracked.progress == 0.0F &&
            !tracked.conflict && tracked.state == veyra::FileOperationState::queued,
        "a retry clears progress and conflict and counts the attempt"
    );

    queue.begin(1);
    queue.fail(1, veyra::FileOperationFault::error);
    require(
        queue.operation(1, tracked) && !tracked.conflict &&
            tracked.state == veyra::FileOperationState::failed,
        "a plain error is not a conflict"
    );
}

void check_cancel_complete_and_dismiss() {
    auto queue = veyra::FileOperationQueue{};
    queue.submit(veyra::FileOperationKind::copy, 1);
    queue.submit(veyra::FileOperationKind::copy, 2);
    queue.submit(veyra::FileOperationKind::copy, 3);
    queue.begin(1);
    queue.begin(2);

    require(
        queue.cancel(3) == veyra::FileOperationResult::changed,
        "canceling a queued operation cancels it"
    );
    require(
        queue.cancel(3) == veyra::FileOperationResult::handled,
        "canceling a canceled operation stays put"
    );
    require(
        queue.complete(1) == veyra::FileOperationResult::changed,
        "completing an active operation completes it"
    );
    require(
        queue.complete(1) == veyra::FileOperationResult::handled,
        "completing a done operation stays put"
    );
    require(
        queue.retry(1) == veyra::FileOperationResult::handled,
        "retrying a done operation stays put"
    );
    require(queue.pending_count() == 1, "only the running operation stays pending");

    const auto cleared = queue.dismiss_finished();
    require(
        cleared.result == veyra::FileOperationResult::changed && cleared.removed == 2 &&
            queue.count() == 1 && queue.contains(2),
        "dismissing clears finished work and keeps the running operation"
    );

    const auto empty = queue.dismiss_finished();
    require(
        empty.result == veyra::FileOperationResult::handled && empty.removed == 0,
        "dismissing with nothing finished stays put"
    );

    queue.cancel(2);
    queue.dismiss_finished();
    require(queue.count() == 0 && queue.pending_count() == 0, "the queue drains fully");
}

void check_copy_truncates() {
    auto queue = veyra::FileOperationQueue{};
    queue.submit(veyra::FileOperationKind::copy, 1);
    queue.submit(veyra::FileOperationKind::move, 2);

    auto roomy = std::array<veyra::FileOperation, 8>{};
    require(queue.copy_operations(roomy) == 2, "a big buffer copies everything");

    auto tight = std::array<veyra::FileOperation, 1>{};
    require(queue.copy_operations(tight) == 1 && tight[0].id == 1, "a short buffer truncates");
}

void check_semantics_carry_status() {
    auto queue = veyra::FileOperationQueue{};
    queue.submit(veyra::FileOperationKind::copy, 1);
    queue.begin(1);
    queue.advance(1, 0.5F);

    auto list = veyra::SemanticNodeData{};
    require(!queue.write_list_semantics({}, list), "an empty list name is rejected");
    require(queue.write_list_semantics("Operations", list), "a named list writes semantics");
    require(list.role == veyra::SemanticRole::listbox, "the queue exposes a listbox");

    auto item = veyra::SemanticNodeData{};
    require(!queue.write_item_semantics(1, {}, item), "an empty item name is rejected");
    require(!queue.write_item_semantics(0, "Copy", item), "a zero id is rejected");
    require(!queue.write_item_semantics(9, "Copy", item), "an unknown id is rejected");
    require(queue.write_item_semantics(1, "Copy", item), "a running item writes semantics");
    require(
        item.role == veyra::SemanticRole::option &&
            !veyra::has_action(item.actions, veyra::SemanticAction::dismiss),
        "a running item offers no dismiss action"
    );

    queue.fail(1, veyra::FileOperationFault::conflict);
    require(queue.write_item_semantics(1, "Copy", item), "a conflicted item writes semantics");
    require(
        item.value == std::string_view{"conflict"} &&
            veyra::has_action(item.actions, veyra::SemanticAction::dismiss),
        "a conflicted item reports conflict and offers dismiss"
    );
}

void check_terminal_transitions() {
    auto queue = veyra::FileOperationQueue{};
    queue.submit(veyra::FileOperationKind::copy, 1);
    queue.submit(veyra::FileOperationKind::copy, 2);
    queue.submit(veyra::FileOperationKind::copy, 3);
    queue.submit(veyra::FileOperationKind::copy, 4);
    queue.submit(veyra::FileOperationKind::copy, 5);
    queue.submit(veyra::FileOperationKind::copy, 6);
    queue.submit(veyra::FileOperationKind::copy, 7);
    queue.submit(veyra::FileOperationKind::copy, 8);
    queue.submit(veyra::FileOperationKind::copy, 9);
    queue.submit(veyra::FileOperationKind::copy, 10);
    queue.submit(veyra::FileOperationKind::copy, 11);
    queue.submit(veyra::FileOperationKind::copy, 12);

    require(
        queue.fail(1, veyra::FileOperationFault::error) == veyra::FileOperationResult::ignored,
        "failing a queued operation is ignored"
    );

    require(
        queue.begin(2) == veyra::FileOperationResult::started,
        "the pause-then-fail operation starts"
    );
    require(queue.pause(2) == veyra::FileOperationResult::changed, "the fail target pauses first");
    require(
        queue.fail(2, veyra::FileOperationFault::error) == veyra::FileOperationResult::changed,
        "failing a paused operation fails it"
    );

    require(
        queue.cancel(3) == veyra::FileOperationResult::changed,
        "canceling a queued operation cancels it"
    );

    veyra::FileOperation tracked{};
    require(
        queue.retry(3) == veyra::FileOperationResult::changed && queue.operation(3, tracked) &&
            tracked.attempts == 1 && tracked.state == veyra::FileOperationState::queued,
        "retrying a canceled operation re-queues it and counts the attempt"
    );

    require(
        queue.complete(4) == veyra::FileOperationResult::ignored,
        "completing a queued operation is ignored"
    );
    require(
        queue.begin(5) == veyra::FileOperationResult::started,
        "the pause-then-complete operation starts"
    );
    require(
        queue.pause(5) == veyra::FileOperationResult::changed,
        "the complete target pauses first"
    );
    require(
        queue.complete(5) == veyra::FileOperationResult::changed && queue.operation(5, tracked) &&
            tracked.progress == 1.0F && tracked.state == veyra::FileOperationState::done,
        "completing a paused operation finishes it at full progress"
    );
    require(
        queue.complete(2) == veyra::FileOperationResult::ignored,
        "completing a failed operation is ignored"
    );

    auto canceled = veyra::FileOperationQueue{};
    canceled.submit(veyra::FileOperationKind::copy, 1);
    canceled.begin(1);
    canceled.cancel(1);
    require(
        canceled.complete(1) == veyra::FileOperationResult::ignored,
        "completing a canceled operation is ignored"
    );

    require(
        queue.begin(1) == veyra::FileOperationResult::started,
        "a queued operation still starts"
    );
    require(queue.pause(1) == veyra::FileOperationResult::changed, "the begin-target pauses first");
    require(
        queue.begin(1) == veyra::FileOperationResult::ignored,
        "beginning a paused operation is ignored"
    );
    require(
        queue.begin(5) == veyra::FileOperationResult::ignored,
        "beginning a done operation is ignored"
    );
    require(
        queue.begin(2) == veyra::FileOperationResult::ignored,
        "beginning a failed operation is ignored"
    );
    require(
        queue.begin(3) == veyra::FileOperationResult::started,
        "the canceled-then-retried operation restarts"
    );
    require(
        queue.cancel(3) == veyra::FileOperationResult::changed,
        "the restarted operation cancels again"
    );
    require(
        queue.begin(3) == veyra::FileOperationResult::ignored,
        "beginning a canceled operation is ignored"
    );

    require(
        queue.begin(6) == veyra::FileOperationResult::started,
        "the cancel-active target starts"
    );
    require(
        queue.cancel(6) == veyra::FileOperationResult::changed && queue.operation(6, tracked) &&
            tracked.state == veyra::FileOperationState::canceled,
        "canceling an active operation cancels it"
    );
    require(
        queue.begin(7) == veyra::FileOperationResult::started,
        "the cancel-paused target starts"
    );
    require(
        queue.pause(7) == veyra::FileOperationResult::changed,
        "the cancel-paused target pauses first"
    );
    require(
        queue.cancel(7) == veyra::FileOperationResult::changed && queue.operation(7, tracked) &&
            tracked.state == veyra::FileOperationState::canceled,
        "canceling a paused operation cancels it"
    );
    require(
        queue.cancel(5) == veyra::FileOperationResult::handled,
        "canceling a done operation stays put"
    );
    require(
        queue.cancel(2) == veyra::FileOperationResult::handled,
        "canceling a failed operation stays put"
    );

    require(
        queue.advance(5, 0.5F) == veyra::FileOperationResult::ignored,
        "advancing a done operation is ignored"
    );
    require(
        queue.advance(2, 0.5F) == veyra::FileOperationResult::ignored,
        "advancing a failed operation is ignored"
    );
    require(
        queue.advance(6, 0.5F) == veyra::FileOperationResult::ignored,
        "advancing a canceled operation is ignored"
    );
    require(
        queue.pause(5) == veyra::FileOperationResult::ignored,
        "pausing a done operation is ignored"
    );
    require(
        queue.pause(2) == veyra::FileOperationResult::ignored,
        "pausing a failed operation is ignored"
    );
    require(
        queue.pause(6) == veyra::FileOperationResult::ignored,
        "pausing a canceled operation is ignored"
    );
    require(
        queue.resume(5) == veyra::FileOperationResult::ignored,
        "resuming a done operation is ignored"
    );
    require(
        queue.resume(2) == veyra::FileOperationResult::ignored,
        "resuming a failed operation is ignored"
    );
    require(
        queue.resume(6) == veyra::FileOperationResult::ignored,
        "resuming a canceled operation is ignored"
    );

    require(
        queue.begin(8) == veyra::FileOperationResult::started,
        "the conflict-retry operation starts"
    );
    require(
        queue.fail(8, veyra::FileOperationFault::conflict) == veyra::FileOperationResult::changed,
        "the retry target fails with a conflict"
    );
    require(
        queue.retry(8) == veyra::FileOperationResult::changed && queue.operation(8, tracked) &&
            tracked.attempts == 1 && tracked.progress == 0.0F && !tracked.conflict &&
            tracked.state == veyra::FileOperationState::queued,
        "retrying clears conflict and progress while counting the attempt"
    );

    require(
        queue.begin(9) == veyra::FileOperationResult::started,
        "the fail-twice operation starts"
    );
    require(
        queue.fail(9, veyra::FileOperationFault::error) == veyra::FileOperationResult::changed,
        "the first fail lands"
    );
    require(
        queue.fail(9, veyra::FileOperationFault::error) == veyra::FileOperationResult::handled,
        "failing a failed operation stays put"
    );
    require(
        queue.begin(10) == veyra::FileOperationResult::started,
        "the complete-twice operation starts"
    );
    require(queue.complete(10) == veyra::FileOperationResult::changed, "the first complete lands");
    require(
        queue.complete(10) == veyra::FileOperationResult::handled,
        "completing a done operation stays put"
    );
    require(
        queue.retry(10) == veyra::FileOperationResult::handled,
        "retrying a done operation stays put"
    );
    require(
        queue.retry(11) == veyra::FileOperationResult::handled,
        "retrying a queued operation stays put"
    );
    require(
        queue.begin(12) == veyra::FileOperationResult::started,
        "the retry-active target starts"
    );
    require(
        queue.retry(12) == veyra::FileOperationResult::handled,
        "retrying an active operation stays put"
    );
    require(
        queue.fail(99, veyra::FileOperationFault::error) == veyra::FileOperationResult::ignored,
        "failing an unknown id is ignored"
    );
    require(
        queue.retry(99) == veyra::FileOperationResult::ignored,
        "retrying an unknown id is ignored"
    );
    require(
        queue.complete(99) == veyra::FileOperationResult::ignored,
        "completing an unknown id is ignored"
    );
    require(
        queue.cancel(99) == veyra::FileOperationResult::ignored,
        "canceling an unknown id is ignored"
    );
}

void check_item_status_strings() {
    auto queue = veyra::FileOperationQueue{};
    queue.submit(veyra::FileOperationKind::copy, 1);
    queue.submit(veyra::FileOperationKind::move, 2);
    queue.submit(veyra::FileOperationKind::remove, 3);
    queue.submit(veyra::FileOperationKind::copy, 4);
    queue.submit(veyra::FileOperationKind::copy, 5);
    queue.begin(2);
    queue.pause(2);
    queue.begin(3);
    queue.complete(3);
    queue.begin(4);
    queue.fail(4, veyra::FileOperationFault::error);
    queue.cancel(5);

    auto item = veyra::SemanticNodeData{};
    require(queue.write_item_semantics(1, "Copy", item), "a queued item writes semantics");
    require(
        item.value == std::string_view{"queued"} &&
            !veyra::has_action(item.actions, veyra::SemanticAction::dismiss),
        "a queued item reports queued and offers no dismiss"
    );
    require(queue.write_item_semantics(2, "Move", item), "a paused item writes semantics");
    require(
        item.value == std::string_view{"paused"} &&
            !veyra::has_action(item.actions, veyra::SemanticAction::dismiss),
        "a paused item reports paused and offers no dismiss"
    );
    require(queue.write_item_semantics(3, "Remove", item), "a done item writes semantics");
    require(
        item.value == std::string_view{"done"} &&
            veyra::has_action(item.actions, veyra::SemanticAction::dismiss),
        "a done item reports done and offers dismiss"
    );
    require(queue.write_item_semantics(4, "Copy", item), "a failed item writes semantics");
    require(
        item.value == std::string_view{"failed"} &&
            veyra::has_action(item.actions, veyra::SemanticAction::dismiss),
        "a plain failure reports failed and offers dismiss"
    );
    require(queue.write_item_semantics(5, "Copy", item), "a canceled item writes semantics");
    require(
        item.value == std::string_view{"canceled"} &&
            veyra::has_action(item.actions, veyra::SemanticAction::dismiss),
        "a canceled item reports canceled and offers dismiss"
    );
}

}

int main() {
    try {
        check_submit_and_capacity();
        check_begin_and_progress();
        check_pause_and_resume();
        check_fail_retry_and_conflict();
        check_cancel_complete_and_dismiss();
        check_terminal_transitions();
        check_item_status_strings();
        check_copy_truncates();
        check_semantics_carry_status();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
