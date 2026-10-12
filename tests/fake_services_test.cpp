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
        services.notice_show(1, "Title", "Body") == veyra::ServiceResult::denied,
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

    require(
        services.notice_show(1, "Title", "Body") == veyra::ServiceResult::denied,
        "show while denied fails"
    );

    ask = services.notice_request_permission();

    require(
        ask.result == veyra::ServiceResult::ok && ask.permission == veyra::NoticePermission::denied,
        "ask reports the denial"
    );

    services.set_notice_permission(veyra::NoticePermission::granted);

    require(
        services.notice_show(1, "Title", "Body") == veyra::ServiceResult::ok,
        "denied-then-granted flip restores show"
    );
    require(services.notice_visible(1), "shown notice reads visible");
}

void check_notice_lifecycle() {
    veyra::FakeServices services{};

    services.set_notice_permission(veyra::NoticePermission::granted);

    require(
        services.notice_show(0, "Zero", "Body") == veyra::ServiceResult::invalid_input,
        "zero id is rejected"
    );
    require(services.notice_show(7, "Seven", "Body") == veyra::ServiceResult::ok, "notice shows");
    require(services.notice_count() == 1, "count tracks the show");
    require(
        services.notice_show(7, "Seven", "Again") == veyra::ServiceResult::ok,
        "same id shows again"
    );
    require(services.notice_count() == 1, "replace-by-id keeps the count");

    for (std::uint64_t id = 8; id < 8 + veyra::FakeServices::kNotices - 1; ++id) {
        require(
            services.notice_show(id, "Title", "Body") == veyra::ServiceResult::ok,
            "notice table fills"
        );
    }

    require(services.notice_count() == veyra::FakeServices::kNotices, "table reaches capacity");
    require(
        services.notice_show(100, "Title", "Body") == veyra::ServiceResult::overflow,
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
    require(
        services.notice_show(100, "Title", "Body") == veyra::ServiceResult::ok,
        "dismissed slot is reusable"
    );
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

    require(
        services.notice_show(3, "Three", "Body") == veyra::ServiceResult::ok,
        "click target shows"
    );
    require(
        services.notice_show(4, "Four", "Body") == veyra::ServiceResult::ok,
        "second target shows"
    );
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

void check_notice_payload_round_trip() {
    veyra::FakeServices services{};

    services.set_notice_permission(veyra::NoticePermission::granted);

    require(
        services.notice_show(11, "Build done", "All 84 targets linked clean.") ==
            veyra::ServiceResult::ok,
        "payload-bearing show lands"
    );

    const veyra::NoticeRead read = services.notice_read(11);

    require(read.result == veyra::ServiceResult::ok, "stored payload reads back");
    require(read.announcement.id == 11, "read carries the shown id");
    require(
        std::string_view(read.announcement.title.data(), read.announcement.title_length) ==
            "Build done",
        "title arrives byte-identical"
    );
    require(
        std::string_view(read.announcement.body.data(), read.announcement.body_length) ==
            "All 84 targets linked clean.",
        "body arrives byte-identical"
    );
    require(!read.announcement.title_truncated, "fitting title sets no flag");
    require(!read.announcement.body_truncated, "fitting body sets no flag");
    require(
        services.notice_read(12).result == veyra::ServiceResult::not_found,
        "unshown id reads as not found"
    );
}

void check_notice_replace_updates_payload() {
    veyra::FakeServices services{};

    services.set_notice_permission(veyra::NoticePermission::granted);

    require(services.notice_show(21, "First", "one") == veyra::ServiceResult::ok, "notice shows");
    require(
        services.notice_show(22, "Other", "two") == veyra::ServiceResult::ok,
        "second notice shows"
    );
    require(
        services.notice_show(21, "Second", "updated body") == veyra::ServiceResult::ok,
        "same id shows again"
    );
    require(services.notice_count() == 2, "replace keeps the count");

    const veyra::NoticeRead read = services.notice_read(21);

    require(read.result == veyra::ServiceResult::ok, "replaced payload reads back");
    require(
        std::string_view(read.announcement.title.data(), read.announcement.title_length) ==
            "Second",
        "replace swaps the title"
    );
    require(
        std::string_view(read.announcement.body.data(), read.announcement.body_length) ==
            "updated body",
        "replace swaps the body"
    );

    require(services.notice_dismiss(21) == veyra::ServiceResult::ok, "notice dismisses");
    require(
        services.notice_read(21).result == veyra::ServiceResult::not_found,
        "dismissed payload reads as gone"
    );
    require(
        services.notice_show(21, "Fresh", "after dismiss") == veyra::ServiceResult::ok,
        "dismissed id shows fresh"
    );

    const veyra::NoticeRead fresh = services.notice_read(21);

    require(
        std::string_view(fresh.announcement.title.data(), fresh.announcement.title_length) ==
            "Fresh",
        "fresh show after dismiss carries the new title"
    );
}

void check_notice_oversized_payload_truncates() {
    veyra::FakeServices services{};

    services.set_notice_permission(veyra::NoticePermission::granted);

    const std::string long_title(veyra::kNoticeTitleCapacity + 20, 't');
    const std::string long_body(veyra::kNoticeBodyCapacity + 40, 'b');

    require(
        services.notice_show(31, long_title.c_str(), long_body.c_str()) == veyra::ServiceResult::ok,
        "oversized payload still shows"
    );

    const veyra::NoticeRead read = services.notice_read(31);

    require(read.result == veyra::ServiceResult::ok, "truncated payload reads back");
    require(
        read.announcement.title_length == veyra::kNoticeTitleCapacity,
        "title clips to capacity"
    );
    require(read.announcement.title_truncated, "clipped title sets its flag");
    require(read.announcement.body_length == veyra::kNoticeBodyCapacity, "body clips to capacity");
    require(read.announcement.body_truncated, "clipped body sets its flag");
    require(
        std::string_view(read.announcement.title.data(), read.announcement.title_length) ==
            std::string_view(long_title.data(), veyra::kNoticeTitleCapacity),
        "title keeps its prefix"
    );
    require(
        std::string_view(read.announcement.body.data(), read.announcement.body_length) ==
            std::string_view(long_body.data(), veyra::kNoticeBodyCapacity),
        "body keeps its prefix"
    );

    const std::string exact_title(veyra::kNoticeTitleCapacity, 'e');

    require(
        services.notice_show(31, exact_title.c_str(), "") == veyra::ServiceResult::ok,
        "boundary-length title replaces"
    );

    const veyra::NoticeRead exact = services.notice_read(31);

    require(!exact.announcement.title_truncated, "exact-fit title sets no flag");
    require(exact.announcement.body_length == 0, "empty body stores zero length");
    require(!exact.announcement.body_truncated, "empty body sets no flag");
}

void check_notice_permission_payload_interaction() {
    veyra::FakeServices services{};

    require(
        services.notice_show(41, "Early", "nope") == veyra::ServiceResult::denied,
        "show while undecided is denied"
    );
    require(
        services.notice_read(41).result == veyra::ServiceResult::not_found,
        "denied show stores nothing"
    );
    require(
        services.notice_show(0, "Zero", "Body") == veyra::ServiceResult::invalid_input,
        "reserved id fails even without permission"
    );
    require(
        services.notice_show(41, nullptr, "Body") == veyra::ServiceResult::invalid_input,
        "null title fails even without permission"
    );
    require(
        services.notice_show(41, "Title", nullptr) == veyra::ServiceResult::invalid_input,
        "null body fails even without permission"
    );

    services.set_notice_permission(veyra::NoticePermission::granted);

    require(
        services.notice_show(41, "Kept", "stays") == veyra::ServiceResult::ok,
        "granted show lands"
    );

    services.set_notice_permission(veyra::NoticePermission::denied);

    require(
        services.notice_show(42, "Lost", "denied") == veyra::ServiceResult::denied,
        "show while denied fails"
    );
    require(
        services.notice_read(42).result == veyra::ServiceResult::not_found,
        "denied show stores nothing either"
    );

    const veyra::NoticeRead kept = services.notice_read(41);

    require(
        kept.result == veyra::ServiceResult::ok &&
            std::string_view(kept.announcement.title.data(), kept.announcement.title_length) ==
                "Kept",
        "denial keeps the earlier payload"
    );

    services.set_notice_permission(veyra::NoticePermission::granted);

    require(
        services.notice_show(42, "Back", "again") == veyra::ServiceResult::ok,
        "grant restores show"
    );
    require(
        services.notice_read(42).result == veyra::ServiceResult::ok,
        "restored show reads back"
    );
}

void check_notice_click_routing() {
    veyra::FakeServices services{};

    services.set_notice_permission(veyra::NoticePermission::granted);

    require(
        services.notice_show(51, "First", "one") == veyra::ServiceResult::ok,
        "first notice shows"
    );
    require(
        services.notice_show(52, "Second", "two") == veyra::ServiceResult::ok,
        "second notice shows"
    );
    require(
        services.notice_show(53, "Third", "three") == veyra::ServiceResult::ok,
        "third notice shows"
    );
    require(services.simulate_notice_click(52) == veyra::ServiceResult::ok, "middle click lands");
    require(services.simulate_notice_click(52) == veyra::ServiceResult::ok, "repeat click queues");

    std::uint64_t clicked{};

    require(services.notice_poll_click(clicked) && clicked == 52, "click names the right id");
    require(services.notice_poll_click(clicked) && clicked == 52, "repeat click follows");
    require(services.simulate_notice_click(51) == veyra::ServiceResult::ok, "first click lands");
    require(services.simulate_notice_click(53) == veyra::ServiceResult::ok, "third click lands");
    require(services.notice_dismiss(51) == veyra::ServiceResult::ok, "first notice dismisses");

    require(services.notice_poll_click(clicked) && clicked == 53, "dismiss keeps other ids");
    require(!services.notice_poll_click(clicked), "dismissed id leaves no click behind");
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

    require(services.notice_show(5, "Five", "Body") == veyra::ServiceResult::ok, "notice shows");
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
    require(
        services.notice_read(5).result == veyra::ServiceResult::not_found,
        "reset clears stored payloads"
    );

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
        check_notice_payload_round_trip();
        check_notice_replace_updates_payload();
        check_notice_oversized_payload_truncates();
        check_notice_permission_payload_interaction();
        check_notice_click_routing();
        check_notice_clicks();
        check_power_state();
        check_reset_clears_everything();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
