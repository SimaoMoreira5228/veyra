#include "veyra/behaviors/toast.hpp"

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

void check_push_dismiss_expire_ordering() {
    veyra::ToastQueue toasts;

    require(toasts.empty(), "queue starts empty");
    require(toasts.size() == 0, "size starts at zero");
    require(toasts.now_ms() == 0, "clock starts at zero");
    require(!toasts.time_until_next_expiry().has_value(), "empty queue schedules nothing");

    const auto first = toasts.push(1, veyra::ToastKind::info, 100, "saved");
    require(first.result == veyra::ToastResult::pushed, "first toast pushes");
    require(first.announcement.id == 1, "push announces the id");
    require(!first.announcement.assertive, "info announces politely");

    const auto second = toasts.push(2, veyra::ToastKind::warning, 200, "low disk");
    require(second.result == veyra::ToastResult::pushed, "second toast pushes");
    require(!second.announcement.assertive, "warning announces politely");

    const auto third = toasts.push(3, veyra::ToastKind::error, 300, "write failed");
    require(third.result == veyra::ToastResult::pushed, "third toast pushes");
    require(third.announcement.assertive, "error announces assertively");

    require(toasts.size() == 3, "queue holds three toasts");
    require(toasts.contains(1) && toasts.contains(2) && toasts.contains(3), "ids are present");

    std::array<std::uint64_t, 6> expired{};
    std::size_t expired_count{};

    auto result = toasts.advance(250, expired, expired_count);
    require(result == veyra::ToastResult::complete, "advance drains");
    require(expired_count == 2, "two toasts expire");
    require(expired[0] == 1 && expired[1] == 2, "expiry reports insertion order");
    require(toasts.size() == 1, "expired toasts leave the queue");
    require(!toasts.contains(1) && !toasts.contains(2), "expired ids are gone");
    require(toasts.contains(3), "unexpired toast stays");
    require(toasts.now_ms() == 250, "advance moves the clock");

    const auto dismissed = toasts.dismiss(3);
    require(dismissed.result == veyra::ToastResult::dismissed, "live toast dismisses");
    require(dismissed.id == 3, "dismiss reports the id");
    require(dismissed.kind == veyra::ToastKind::error, "dismiss reports the kind");
    require(toasts.empty(), "queue empties");

    const auto missing = toasts.dismiss(3);
    require(missing.result == veyra::ToastResult::ignored, "dismissing a gone id is ignored");

    const auto null_dismiss = toasts.dismiss(0);
    require(null_dismiss.result == veyra::ToastResult::ignored, "dismissing id zero is ignored");
}

void check_sticky_toasts_need_explicit_dismiss() {
    veyra::ToastQueue toasts;

    const auto sticky = toasts.push(1, veyra::ToastKind::error, 0, "disk gone");
    require(sticky.result == veyra::ToastResult::pushed, "zero ttl pushes a sticky toast");
    require(sticky.announcement.assertive, "sticky error still announces assertively");

    require(
        toasts.push(2, veyra::ToastKind::info, 100, "saved").result == veyra::ToastResult::pushed,
        "finite toast pushes beside the sticky one"
    );

    std::array<std::uint64_t, 6> expired{};
    std::size_t expired_count{};

    auto result = toasts.advance(1000000, expired, expired_count);
    require(result == veyra::ToastResult::complete, "far-future advance drains");
    require(expired_count == 1 && expired[0] == 2, "only the finite toast expires");
    require(toasts.contains(1), "sticky toast survives any advance");
    require(!toasts.time_until_next_expiry().has_value(), "sticky-only queue schedules nothing");

    result = toasts.advance(std::numeric_limits<std::uint64_t>::max(), expired, expired_count);
    require(result == veyra::ToastResult::complete, "end-of-time advance drains");
    require(expired_count == 0, "sticky toast never expires on its own");

    const auto reset = toasts.dismiss_all();
    require(reset.dismissed == 1, "dismiss all clears the sticky toast");
    require(toasts.empty(), "queue empties after dismiss all");
}

void check_duplicate_id_replaces_in_place() {
    veyra::ToastQueue toasts;

    require(
        toasts.push(1, veyra::ToastKind::info, 100, "saving").result == veyra::ToastResult::pushed,
        "first push lands"
    );

    std::array<std::uint64_t, 6> expired{};
    std::size_t expired_count{};

    require(
        toasts.advance(80, expired, expired_count) == veyra::ToastResult::complete,
        "time passes"
    );

    const auto replaced = toasts.push(1, veyra::ToastKind::error, 500, "save failed");
    require(replaced.result == veyra::ToastResult::replaced, "live duplicate replaces");
    require(toasts.size() == 1, "replace keeps one entry");
    require(replaced.announcement.kind == veyra::ToastKind::error, "replace updates the kind");
    require(replaced.announcement.assertive, "re-announcement follows the new kind");

    auto result = toasts.advance(200, expired, expired_count);
    require(
        result == veyra::ToastResult::complete && expired_count == 0,
        "replace refreshes expiry"
    );

    result = toasts.advance(580, expired, expired_count);
    require(
        result == veyra::ToastResult::complete && expired_count == 1 && expired[0] == 1,
        "replaced toast expires on its refreshed deadline"
    );

    require(
        toasts.push(1, veyra::ToastKind::info, 50, "back").result == veyra::ToastResult::pushed,
        "push after expiry is a fresh push, not a replace"
    );

    require(toasts.dismiss(1).result == veyra::ToastResult::dismissed, "fresh toast dismisses");
    require(
        toasts.push(1, veyra::ToastKind::info, 50, "again").result == veyra::ToastResult::pushed,
        "push after dismiss is new, never a silent resurrection"
    );
}

void check_overflow_leaves_state_untouched() {
    veyra::ToastQueue toasts;

    for (std::size_t i = 1; i <= veyra::ToastQueue::kCapacity; ++i) {
        const auto pushed = toasts.push(i, veyra::ToastKind::info, 1000, "burst");
        require(pushed.result == veyra::ToastResult::pushed, "entries push up to capacity");
    }
    require(toasts.size() == veyra::ToastQueue::kCapacity, "queue fills to capacity");

    const auto before = toasts.time_until_next_expiry();

    const auto full = toasts.push(99, veyra::ToastKind::error, 1000, "one too many");
    require(full.result == veyra::ToastResult::overflow, "push past capacity overflows");
    require(toasts.size() == veyra::ToastQueue::kCapacity, "overflow keeps every entry");
    require(!toasts.contains(99), "overflowed toast never lands");
    require(toasts.contains(1), "overflow evicts nothing to make room");
    require(toasts.time_until_next_expiry() == before, "overflow moves no deadlines");

    require(toasts.dismiss(1).result == veyra::ToastResult::dismissed, "room frees after dismiss");
    require(
        toasts.push(99, veyra::ToastKind::error, 1000, "fits now").result ==
            veyra::ToastResult::pushed,
        "push fits once a slot frees"
    );
}

void check_backwards_time_is_rejected() {
    veyra::ToastQueue toasts;

    require(
        toasts.push(1, veyra::ToastKind::info, 200, "later").result == veyra::ToastResult::pushed,
        "toast pushes"
    );

    std::array<std::uint64_t, 6> expired{};
    std::size_t expired_count{};

    require(
        toasts.advance(100, expired, expired_count) == veyra::ToastResult::complete,
        "forward advance works"
    );
    require(toasts.now_ms() == 100, "clock records the forward advance");

    expired_count = 7;

    const auto skewed = toasts.advance(50, expired, expired_count);
    require(skewed == veyra::ToastResult::rejected, "backwards advance is rejected");
    require(expired_count == 0, "rejected advance reports no expiries");
    require(toasts.now_ms() == 100, "rejected advance moves no clock");
    require(toasts.contains(1), "rejected advance drops nothing");

    require(
        toasts.advance(100, expired, expired_count) == veyra::ToastResult::complete,
        "repeating the clock is fine"
    );
    require(expired_count == 0, "nothing is due yet");
    require(toasts.contains(1), "repeat advance drops nothing");
}

void check_ttl_edges() {
    veyra::ToastQueue toasts;

    require(
        toasts.push(0, veyra::ToastKind::info, 100, "null").result == veyra::ToastResult::ignored,
        "id zero is rejected"
    );
    require(
        toasts.push(1, veyra::ToastKind::info, 100, "").result == veyra::ToastResult::ignored,
        "empty body is rejected"
    );
    require(toasts.empty(), "rejected pushes leave the queue alone");

    require(
        toasts.push(1, veyra::ToastKind::info, 1, "blink").result == veyra::ToastResult::pushed,
        "minimal ttl pushes"
    );

    std::array<std::uint64_t, 6> expired{};
    std::size_t expired_count{};

    auto result = toasts.advance(0, expired, expired_count);
    require(result == veyra::ToastResult::complete && expired_count == 0, "deadline is exclusive");

    result = toasts.advance(1, expired, expired_count);
    require(
        result == veyra::ToastResult::complete && expired_count == 1,
        "deadline is inclusive at expiry"
    );

    require(
        toasts.advance(1000, expired, expired_count) == veyra::ToastResult::complete,
        "clock jumps forward"
    );
    require(
        toasts.push(2, veyra::ToastKind::info, 100, "later").result == veyra::ToastResult::pushed,
        "push anchors to the current clock"
    );

    result = toasts.advance(1099, expired, expired_count);
    require(
        result == veyra::ToastResult::complete && expired_count == 0,
        "ttl counts from push time"
    );

    result = toasts.advance(1100, expired, expired_count);
    require(
        result == veyra::ToastResult::complete && expired_count == 1 && expired[0] == 2,
        "anchored deadline expires"
    );

    require(
        toasts.advance(std::numeric_limits<std::uint64_t>::max(), expired, expired_count) ==
            veyra::ToastResult::complete,
        "clock reaches the end of time"
    );
    require(
        toasts.push(3, veyra::ToastKind::info, std::numeric_limits<std::uint32_t>::max(), "far")
                .result == veyra::ToastResult::pushed,
        "huge ttl saturates instead of wrapping"
    );

    const auto wait = toasts.time_until_next_expiry();
    require(wait.has_value() && *wait == 0, "saturated deadline is already due");

    result = toasts.advance(std::numeric_limits<std::uint64_t>::max(), expired, expired_count);
    require(
        result == veyra::ToastResult::complete && expired_count == 1 && expired[0] == 3,
        "saturated toast still expires"
    );
}

void check_next_expiry_scheduling() {
    veyra::ToastQueue toasts;

    std::array<std::uint64_t, 6> expired{};
    std::size_t expired_count{};

    require(
        toasts.advance(5000, expired, expired_count) == veyra::ToastResult::complete,
        "advance on empty queue completes"
    );
    require(expired_count == 0, "empty advance expires nothing");
    require(toasts.now_ms() == 5000, "empty advance still moves the clock");

    require(
        toasts.push(1, veyra::ToastKind::info, 300, "slow").result == veyra::ToastResult::pushed,
        "slow toast pushes"
    );
    require(
        toasts.push(2, veyra::ToastKind::error, 100, "fast").result == veyra::ToastResult::pushed,
        "fast toast pushes"
    );

    auto wait = toasts.time_until_next_expiry();
    require(wait.has_value() && *wait == 100, "next expiry is the nearest deadline");

    require(
        toasts.advance(5100, expired, expired_count) == veyra::ToastResult::complete,
        "fast deadline passes"
    );
    require(expired_count == 1 && expired[0] == 2, "fast toast expires");

    wait = toasts.time_until_next_expiry();
    require(wait.has_value() && *wait == 200, "schedule tracks the survivor");

    const auto reset = toasts.dismiss_all();
    require(reset.dismissed == 1, "dismiss all clears the survivor");
    require(!toasts.time_until_next_expiry().has_value(), "cleared queue schedules nothing");

    const auto empty_reset = toasts.dismiss_all();
    require(empty_reset.dismissed == 0, "dismiss all on empty queue clears nothing");
}

void check_insufficient_span_keeps_state_atomic() {
    veyra::ToastQueue toasts;

    require(
        toasts.push(1, veyra::ToastKind::info, 100, "one").result == veyra::ToastResult::pushed,
        "first toast pushes"
    );
    require(
        toasts.push(2, veyra::ToastKind::info, 100, "two").result == veyra::ToastResult::pushed,
        "second toast pushes"
    );

    std::array<std::uint64_t, 1> small{};
    std::size_t expired_count{};

    const auto short_result = toasts.advance(200, small, expired_count);
    require(short_result == veyra::ToastResult::insufficient_output, "short span is reported");
    require(expired_count == 2, "short span reports how many expired");
    require(toasts.size() == 2, "short span expires nothing");
    require(toasts.now_ms() == 0, "short span moves no clock");

    const auto wait = toasts.time_until_next_expiry();
    require(wait.has_value() && *wait == 100, "short span moves no deadlines");

    std::array<std::uint64_t, 6> roomy{};
    const auto full_result = toasts.advance(200, roomy, expired_count);
    require(full_result == veyra::ToastResult::complete, "retry with room completes");
    require(expired_count == 2 && roomy[0] == 1 && roomy[1] == 2, "retry expires both");
    require(toasts.empty(), "retry drains the queue");
}

}

int main() {
    try {
        check_push_dismiss_expire_ordering();
        check_sticky_toasts_need_explicit_dismiss();
        check_duplicate_id_replaces_in_place();
        check_overflow_leaves_state_untouched();
        check_backwards_time_is_rejected();
        check_ttl_edges();
        check_next_expiry_scheduling();
        check_insufficient_span_keeps_state_atomic();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
