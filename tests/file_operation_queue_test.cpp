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

}

int main() {
    try {
        check_submit_and_capacity();
        check_begin_and_progress();
        check_pause_and_resume();
        check_fail_retry_and_conflict();
        check_cancel_complete_and_dismiss();
        check_copy_truncates();
        check_semantics_carry_status();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
