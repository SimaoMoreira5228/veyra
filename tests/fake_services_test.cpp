#include "veyra/platform/fake_services.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
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

std::string read_text(veyra::FakeServices& services) {
    std::array<char, veyra::FakeServices::kClipboardCapacity> output{};

    std::size_t length = 0;

    require(
        services.clipboard_get(output, length) == veyra::ServiceResult::ok,
        "clipboard read succeeds"
    );

    return std::string(output.data(), length);
}

void check_clipboard_round_trip() {
    veyra::FakeServices services{};

    require(services.clipboard_generation() == 0, "fresh clipboard starts at generation zero");
    require(services.clipboard_length() == 0, "fresh clipboard holds nothing");

    std::array<char, 8> empty_output{};

    std::size_t empty_length = 99;

    require(
        services.clipboard_get(empty_output, empty_length) == veyra::ServiceResult::ok,
        "empty clipboard reads clean"
    );
    require(empty_length == 0, "empty clipboard reports zero length");

    require(
        services.clipboard_set("clipboard text") == veyra::ServiceResult::ok,
        "clipboard accepts NUL-terminated text"
    );
    require(services.clipboard_generation() == 1, "successful write bumps the generation");

    require(read_text(services) == "clipboard text", "clipboard text arrives without a terminator");

    std::array<char, 4> short_output{'x', 'x', 'x', 'x'};

    std::size_t required = 0;

    require(
        services.clipboard_get(short_output, required) ==
            veyra::ServiceResult::insufficient_capacity,
        "short output reports insufficient capacity"
    );
    require(required == 14, "short output reports the required length");
    require(
        short_output == std::array<char, 4>{'x', 'x', 'x', 'x'},
        "short output stays unchanged"
    );

    require(
        services.clipboard_set(nullptr) == veyra::ServiceResult::invalid_input,
        "clipboard rejects a null input"
    );

    std::string oversized(veyra::FakeServices::kClipboardCapacity + 1, 'a');

    require(
        services.clipboard_set(oversized.c_str()) == veyra::ServiceResult::rejected,
        "oversized clipboard text is rejected"
    );
    require(read_text(services) == "clipboard text", "rejected write keeps the old text");
    require(services.clipboard_generation() == 1, "rejected write keeps the generation");

    require(services.clipboard_set("") == veyra::ServiceResult::ok, "empty write is still a write");
    require(read_text(services).empty(), "empty write clears the text");
    require(services.clipboard_generation() == 2, "empty write bumps the generation");
}

void check_clipboard_access_denied() {
    veyra::FakeServices services{};

    require(
        services.clipboard_set("before lock") == veyra::ServiceResult::ok,
        "clipboard writes before the lock"
    );

    services.set_clipboard_access(false);

    require(!services.clipboard_access_allowed(), "access flag reads back");

    std::array<char, 32> output{};

    std::size_t length = 0;

    require(
        services.clipboard_set("locked out") == veyra::ServiceResult::denied,
        "write while denied fails"
    );
    require(
        services.clipboard_get(output, length) == veyra::ServiceResult::denied,
        "read while denied fails"
    );
    require(length == 0, "denied read reports no length");
    require(services.clipboard_generation() == 1, "denied write keeps the generation");

    services.set_clipboard_access(true);

    require(read_text(services) == "before lock", "denied-then-granted flip keeps stored text");
    require(
        services.clipboard_set("after unlock") == veyra::ServiceResult::ok,
        "write works again after the flip"
    );
    require(read_text(services) == "after unlock", "post-flip text reads back");
}

void check_clipboard_persistence() {
    veyra::FakeServices services{};

    require(
        services.clipboard_set("sticky") == veyra::ServiceResult::ok,
        "clipboard writes while persistent"
    );

    services.set_clipboard_persistent(false);

    require(!services.clipboard_persistent(), "persistence flag reads back");
    require(
        services.clipboard_set("gone") == veyra::ServiceResult::unavailable,
        "write without persistence fails"
    );
    require(read_text(services) == "sticky", "unavailable write keeps the old text");
    require(services.clipboard_generation() == 1, "unavailable write keeps the generation");

    require(
        services.simulate_clipboard_external_write("outsider") == veyra::ServiceResult::unavailable,
        "external write without persistence fails too"
    );
    require(read_text(services) == "sticky", "failed external write keeps the old text");

    services.set_clipboard_persistent(true);

    require(
        services.clipboard_set("backed") == veyra::ServiceResult::ok,
        "write works once persistence returns"
    );
    require(read_text(services) == "backed", "restored text reads back");
}

void check_clipboard_generation_fencing() {
    veyra::FakeServices services{};

    require(
        services.clipboard_set("first") == veyra::ServiceResult::ok,
        "clipboard takes the first write"
    );

    const std::uint64_t observed = services.clipboard_generation();

    require(
        services.simulate_clipboard_external_write("second") == veyra::ServiceResult::ok,
        "external write lands"
    );
    require(services.clipboard_generation() == observed + 1, "external write bumps the generation");
    require(
        observed != services.clipboard_generation(),
        "reader detects the mutation through the fence"
    );
    require(read_text(services) == "second", "external text reads back");

    const std::uint64_t settled = services.clipboard_generation();

    require(read_text(services) == "second", "quiet re-read matches");
    require(
        settled == services.clipboard_generation(),
        "generation holds still when nobody writes"
    );

    require(
        services.simulate_clipboard_external_write(nullptr) == veyra::ServiceResult::invalid_input,
        "null external write is rejected"
    );

    std::string oversized(veyra::FakeServices::kClipboardCapacity + 1, 'b');

    require(
        services.simulate_clipboard_external_write(oversized.c_str()) ==
            veyra::ServiceResult::rejected,
        "oversized external write is rejected"
    );
    require(read_text(services) == "second", "rejected external write keeps the text");
}

void check_dialog_script_order() {
    veyra::FakeServices services{};

    require(
        services.dialog_script({.choice = 0, .cancelled = false}) == veyra::ServiceResult::ok,
        "first answer scripts"
    );
    require(
        services.dialog_script({.choice = 2, .cancelled = false}) == veyra::ServiceResult::ok,
        "second answer scripts"
    );
    require(
        services.dialog_script({.choice = -1, .cancelled = true}) == veyra::ServiceResult::ok,
        "cancellation scripts"
    );

    const veyra::DialogOpen first = services.dialog_open(veyra::DialogKind::confirm);

    require(first.result == veyra::ServiceResult::ok, "scripted open registers");

    const veyra::DialogOpen second = services.dialog_open(veyra::DialogKind::choice);

    require(second.result == veyra::ServiceResult::ok, "second open registers");

    const veyra::DialogOpen third = services.dialog_open(veyra::DialogKind::alert);

    require(third.result == veyra::ServiceResult::ok, "third open registers");
    require(services.dialog_requests() == 3, "every open counts as a request");
    require(
        services.last_dialog_kind() == veyra::DialogKind::alert,
        "last request kind is recorded"
    );

    const veyra::DialogPoll first_poll = services.dialog_poll(first.handle);

    require(
        first_poll.result == veyra::ServiceResult::ok && first_poll.answer.choice == 0 &&
            !first_poll.answer.cancelled,
        "first open replays the first script"
    );

    const veyra::DialogPoll second_poll = services.dialog_poll(second.handle);

    require(
        second_poll.result == veyra::ServiceResult::ok && second_poll.answer.choice == 2,
        "second open replays the second script"
    );

    const veyra::DialogPoll third_poll = services.dialog_poll(third.handle);

    require(
        third_poll.result == veyra::ServiceResult::ok && third_poll.answer.cancelled,
        "third open replays the cancellation"
    );

    require(
        services.dialog_poll(first.handle).result == veyra::ServiceResult::ok,
        "resolved answers stay readable"
    );

    require(services.dialog_close(first.handle) == veyra::ServiceResult::ok, "dialog closes");
    require(
        services.dialog_poll(first.handle).result == veyra::ServiceResult::not_found,
        "closed dialog polls as gone"
    );
    require(
        services.dialog_close(first.handle) == veyra::ServiceResult::not_found,
        "double close reports not found"
    );
}

void check_dialog_pending_shape() {
    veyra::FakeServices services{};

    const veyra::DialogOpen open = services.dialog_open(veyra::DialogKind::confirm);

    require(open.result == veyra::ServiceResult::ok, "unscripted open still registers");
    require(open.handle != 0, "open hands a nonzero handle");

    const veyra::DialogPoll waiting = services.dialog_poll(open.handle);

    require(waiting.result == veyra::ServiceResult::pending, "unscripted open polls pending");
    require(services.dialog_pending_count() == 1, "pending dialog is counted");

    require(
        services.dialog_poll(open.handle).result == veyra::ServiceResult::pending,
        "polling never blocks, it repeats pending"
    );

    require(
        services.dialog_answer_pending({.choice = 1, .cancelled = false}) ==
            veyra::ServiceResult::ok,
        "late answer resolves the oldest pending dialog"
    );

    const veyra::DialogPoll answered = services.dialog_poll(open.handle);

    require(
        answered.result == veyra::ServiceResult::ok && answered.answer.choice == 1,
        "late answer polls through"
    );
    require(services.dialog_pending_count() == 0, "resolved dialog leaves the pending set");

    require(
        services.dialog_answer_pending({.choice = 0, .cancelled = false}) ==
            veyra::ServiceResult::not_found,
        "late answer with nothing pending reports not found"
    );
    require(
        services.dialog_poll(9999).result == veyra::ServiceResult::not_found,
        "unknown handle polls as not found"
    );
    require(
        services.dialog_close(9999) == veyra::ServiceResult::not_found,
        "unknown handle closes as not found"
    );
    require(
        services.dialog_close(0) == veyra::ServiceResult::not_found,
        "reserved handle zero closes as not found"
    );
}

void check_dialog_limits() {
    veyra::FakeServices services{};

    for (std::size_t index = 0; index < veyra::FakeServices::kDialogScripts; ++index) {
        require(
            services.dialog_script({.choice = 0, .cancelled = false}) == veyra::ServiceResult::ok,
            "script queue fills to capacity"
        );
    }

    require(
        services.dialog_script({.choice = 0, .cancelled = false}) == veyra::ServiceResult::overflow,
        "script past capacity overflows"
    );
    require(
        services.dialog_script({.choice = -1, .cancelled = false}) ==
            veyra::ServiceResult::invalid_input,
        "negative choice without cancellation is rejected"
    );
    require(
        services.dialog_answer_pending({.choice = -2, .cancelled = false}) ==
            veyra::ServiceResult::invalid_input,
        "late negative choice is rejected"
    );
    require(
        services.dialog_scripted_count() == veyra::FakeServices::kDialogScripts,
        "rejected scripts never queue"
    );

    veyra::FakeServices live{};

    for (std::size_t index = 0; index < veyra::FakeServices::kLiveDialogs; ++index) {
        const veyra::DialogOpen open = live.dialog_open(veyra::DialogKind::alert);

        require(open.result == veyra::ServiceResult::ok, "live table fills to capacity");
    }

    require(live.dialog_live_count() == veyra::FakeServices::kLiveDialogs, "live count reads back");

    const veyra::DialogOpen extra = live.dialog_open(veyra::DialogKind::alert);

    require(extra.result == veyra::ServiceResult::overflow, "live past capacity overflows");
    require(extra.handle == 0, "overflowed open hands no handle");
}

void check_notice_permission_flow() {
    veyra::FakeServices services{};

    require(
        services.notice_permission() == veyra::NoticePermission::undecided,
        "fresh host is undecided"
    );

    veyra::NoticeAsk ask = services.notice_request_permission();

    require(
        ask.result == veyra::ServiceResult::pending &&
            ask.permission == veyra::NoticePermission::undecided,
        "ask with no script stays pending instead of blocking"
    );

    require(
        services.notice_show(1) == veyra::ServiceResult::denied,
        "show while undecided is denied"
    );

    require(
        services.notice_script_response(veyra::NoticePermission::undecided) ==
            veyra::ServiceResult::invalid_input,
        "scripting undecided is rejected"
    );
    require(
        services.notice_script_response(veyra::NoticePermission::granted) ==
            veyra::ServiceResult::ok,
        "grant scripts"
    );

    ask = services.notice_request_permission();

    require(
        ask.result == veyra::ServiceResult::ok &&
            ask.permission == veyra::NoticePermission::granted,
        "scripted ask resolves to granted"
    );

    ask = services.notice_request_permission();

    require(
        ask.result == veyra::ServiceResult::ok &&
            ask.permission == veyra::NoticePermission::granted,
        "decided ask answers without a script"
    );

    services.set_notice_permission(veyra::NoticePermission::denied);

    require(services.notice_show(1) == veyra::ServiceResult::denied, "show while denied fails");

    ask = services.notice_request_permission();

    require(
        ask.result == veyra::ServiceResult::ok && ask.permission == veyra::NoticePermission::denied,
        "ask reports the denial"
    );

    services.set_notice_permission(veyra::NoticePermission::granted);

    require(
        services.notice_show(1) == veyra::ServiceResult::ok,
        "denied-then-granted flip restores show"
    );
    require(services.notice_visible(1), "shown notice reads visible");
}

void check_notice_lifecycle() {
    veyra::FakeServices services{};

    services.set_notice_permission(veyra::NoticePermission::granted);

    require(services.notice_show(0) == veyra::ServiceResult::invalid_input, "zero id is rejected");
    require(services.notice_show(7) == veyra::ServiceResult::ok, "notice shows");
    require(services.notice_count() == 1, "count tracks the show");
    require(services.notice_show(7) == veyra::ServiceResult::ok, "same id shows again");
    require(services.notice_count() == 1, "replace-by-id keeps the count");

    for (std::uint64_t id = 8; id < 8 + veyra::FakeServices::kNotices - 1; ++id) {
        require(services.notice_show(id) == veyra::ServiceResult::ok, "notice table fills");
    }

    require(services.notice_count() == veyra::FakeServices::kNotices, "table reaches capacity");
    require(
        services.notice_show(100) == veyra::ServiceResult::overflow,
        "show past capacity overflows"
    );

    require(
        services.notice_dismiss(4242) == veyra::ServiceResult::not_found,
        "dismiss of an unknown id reports not found"
    );
    require(
        services.notice_count() == veyra::FakeServices::kNotices,
        "failed dismiss changes nothing"
    );

    require(services.notice_dismiss(7) == veyra::ServiceResult::ok, "known id dismisses");
    require(!services.notice_visible(7), "dismissed notice reads hidden");
    require(
        services.notice_count() == veyra::FakeServices::kNotices - 1,
        "dismiss shrinks the count"
    );
    require(services.notice_show(100) == veyra::ServiceResult::ok, "dismissed slot is reusable");
}

void check_notice_clicks() {
    veyra::FakeServices services{};

    services.set_notice_permission(veyra::NoticePermission::granted);

    std::uint64_t clicked{};

    require(!services.notice_poll_click(clicked), "empty click queue drains false");

    require(
        services.simulate_notice_click(9) == veyra::ServiceResult::not_found,
        "click on an unknown id reports not found"
    );

    require(services.notice_show(3) == veyra::ServiceResult::ok, "click target shows");
    require(services.notice_show(4) == veyra::ServiceResult::ok, "second target shows");
    require(services.simulate_notice_click(3) == veyra::ServiceResult::ok, "first click lands");
    require(services.simulate_notice_click(4) == veyra::ServiceResult::ok, "second click lands");

    require(services.notice_poll_click(clicked) && clicked == 3, "clicks drain oldest first");
    require(services.notice_poll_click(clicked) && clicked == 4, "second click follows");
    require(!services.notice_poll_click(clicked), "drained queue reads empty");

    require(services.simulate_notice_click(3) == veyra::ServiceResult::ok, "click re-arms");
    require(services.notice_dismiss(3) == veyra::ServiceResult::ok, "target dismisses");
    require(!services.notice_poll_click(clicked), "dismiss drops the queued click");
    require(
        services.simulate_notice_click(3) == veyra::ServiceResult::not_found,
        "dismissed target is not clickable"
    );

    for (std::size_t index = 0; index < veyra::FakeServices::kClickQueue; ++index) {
        require(services.simulate_notice_click(4) == veyra::ServiceResult::ok, "click queue fills");
    }

    require(
        services.simulate_notice_click(4) == veyra::ServiceResult::overflow,
        "click past capacity overflows"
    );
}

void check_power_state() {
    veyra::FakeServices services{};

    require(!services.on_battery(), "fresh host reports wall power");
    require(services.power_generation() == 0, "fresh power generation is zero");

    services.set_on_battery(true);

    require(services.on_battery(), "battery flag flips on");
    require(services.power_generation() == 0, "flag flip alone moves no generation");

    services.simulate_suspend_resume();

    require(services.power_generation() == 1, "suspend cycle bumps the generation");
    require(services.on_battery(), "suspend cycle keeps the battery flag");

    services.simulate_suspend_resume();

    require(services.power_generation() == 2, "second cycle bumps again");

    services.set_on_battery(false);

    require(!services.on_battery(), "battery flag flips off");
    require(services.power_generation() == 2, "flag flip leaves the generation alone");
}

void check_reset_clears_everything() {
    veyra::FakeServices services{};

    require(services.clipboard_set("data") == veyra::ServiceResult::ok, "clipboard fills");

    require(
        services.dialog_script({.choice = 1, .cancelled = false}) == veyra::ServiceResult::ok,
        "dialog script queues"
    );

    const veyra::DialogOpen open = services.dialog_open(veyra::DialogKind::choice);

    require(open.result == veyra::ServiceResult::ok, "dialog opens");

    services.set_notice_permission(veyra::NoticePermission::granted);

    require(services.notice_show(5) == veyra::ServiceResult::ok, "notice shows");
    require(services.simulate_notice_click(5) == veyra::ServiceResult::ok, "click queues");

    services.set_on_battery(true);
    services.simulate_suspend_resume();

    services.reset();

    require(services.clipboard_generation() == 0, "reset clears the clipboard generation");
    require(services.clipboard_length() == 0, "reset clears the clipboard text");
    require(services.clipboard_access_allowed(), "reset restores clipboard access");
    require(services.clipboard_persistent(), "reset restores persistence");
    require(services.dialog_live_count() == 0, "reset clears live dialogs");
    require(services.dialog_requests() == 0, "reset clears the request log");
    require(services.dialog_scripted_count() == 0, "reset clears queued scripts");
    require(
        services.notice_permission() == veyra::NoticePermission::undecided,
        "reset undecides permission"
    );
    require(services.notice_count() == 0, "reset clears notices");

    std::uint64_t clicked{};

    require(!services.notice_poll_click(clicked), "reset clears the click queue");
    require(!services.on_battery(), "reset clears the battery flag");
    require(services.power_generation() == 0, "reset clears power generation");

    veyra::NoticeAsk ask = services.notice_request_permission();

    require(ask.result == veyra::ServiceResult::pending, "reset clears the scripted answer");
}

}

int main() {
    try {
        check_clipboard_round_trip();
        check_clipboard_access_denied();
        check_clipboard_persistence();
        check_clipboard_generation_fencing();
        check_dialog_script_order();
        check_dialog_pending_shape();
        check_dialog_limits();
        check_notice_permission_flow();
        check_notice_lifecycle();
        check_notice_clicks();
        check_power_state();
        check_reset_clears_everything();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
