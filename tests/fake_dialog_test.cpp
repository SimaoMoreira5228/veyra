#include "veyra/platform/core/run_loop.hpp"
#include "veyra/platform/headless/fake_services.hpp"
#include "veyra/platform/headless/null_window.hpp"

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

void check_reset_clears_dialogs() {
    veyra::FakeServices services{};

    require(
        services.dialog_script({.choice = 1, .cancelled = false}) == veyra::ServiceResult::ok,
        "dialog script queues"
    );

    const veyra::DialogOpen open = services.dialog_open(veyra::DialogKind::choice);

    require(open.result == veyra::ServiceResult::ok, "dialog opens");

    services.reset();

    require(services.dialog_live_count() == 0, "reset clears live dialogs");
    require(services.dialog_requests() == 0, "reset clears the request log");
    require(services.dialog_scripted_count() == 0, "reset clears queued scripts");
}

veyra::WindowEvent make_bare(veyra::WindowEventKind kind) {
    veyra::WindowEvent event{};
    event.kind = kind;
    return event;
}

void check_dialog_does_not_block_loop() {
    veyra::FakeServices services{};
    const veyra::DialogOpen open = services.dialog_open(veyra::DialogKind::confirm);
    require(open.result == veyra::ServiceResult::ok, "loop dialog opens");
    require(open.handle != 0, "loop dialog has a handle");

    veyra::NullWindow window("dialog loop", 320, 240);
    veyra::FrameScheduler scheduler;
    auto frames = 0;
    auto pending_polls = 0;
    auto events = 0;
    auto answered = false;

    auto on_event = [&](veyra::WindowEvent& event) noexcept {
        ++events;
        return event.kind == veyra::WindowEventKind::close_request;
    };
    auto on_frame = [&](bool) noexcept {
        ++frames;
        const auto poll = services.dialog_poll(open.handle);
        if (poll.result == veyra::ServiceResult::pending) {
            ++pending_polls;
        }
        if (poll.result == veyra::ServiceResult::ok) {
            answered = true;
        }
        if (frames == 3) {
            services.dialog_answer_pending({.choice = 1, .cancelled = false});
        }
        if (frames == 2) {
            window.push_event(make_bare(veyra::WindowEventKind::key_down));
        }
        scheduler.request_immediate();
        return veyra::FrameAction{.rendered = true};
    };

    veyra::RunLoopConfig config{window, scheduler, on_event, on_frame, nullptr};
    config.frame_cap = 5;
    require(window.push_event(make_bare(veyra::WindowEventKind::key_down)), "loop event queues");
    scheduler.request_immediate();

    require(
        veyra::run_until_quit(config) == veyra::LoopResult::capped,
        "the loop runs to its frame cap while the dialog is pending"
    );
    require(frames == 5, "frames keep pumping during the dialog");
    require(pending_polls == 3, "the dialog polls pending for three frames");
    require(answered, "the late answer arrives through polling");
    require(events == 2, "events keep pumping during the dialog");
    require(services.dialog_pending_count() == 0, "the dialog resolves without a nested loop");
}

}

int main() {
    try {
        check_dialog_script_order();
        check_dialog_pending_shape();
        check_dialog_limits();
        check_reset_clears_dialogs();
        check_dialog_does_not_block_loop();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
