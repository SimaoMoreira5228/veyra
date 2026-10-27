#include "veyra/platform/headless/fake_services.hpp"

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

void check_reset_clears_notices() {
    veyra::FakeServices services{};

    services.set_notice_permission(veyra::NoticePermission::granted);

    require(services.notice_show(5, "Five", "Body") == veyra::ServiceResult::ok, "notice shows");
    require(services.simulate_notice_click(5) == veyra::ServiceResult::ok, "click queues");

    services.reset();

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

    veyra::NoticeAsk ask = services.notice_request_permission();

    require(ask.result == veyra::ServiceResult::pending, "reset clears the scripted answer");
}

void check_reset_clears_power() {
    veyra::FakeServices services{};

    services.set_on_battery(true);
    services.simulate_suspend_resume();

    services.reset();

    require(!services.on_battery(), "reset clears the battery flag");
    require(services.power_generation() == 0, "reset clears power generation");
}

}

int main() {
    try {
        check_notice_permission_flow();
        check_notice_lifecycle();
        check_notice_clicks();
        check_notice_payload_round_trip();
        check_notice_replace_updates_payload();
        check_notice_oversized_payload_truncates();
        check_notice_permission_payload_interaction();
        check_notice_click_routing();
        check_power_state();
        check_reset_clears_notices();
        check_reset_clears_power();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
