#include "veyra/platform/null_window.hpp"
#include "veyra/platform/run_loop.hpp"

#include <array>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>

namespace {

void require(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

std::array<unsigned, 256> sleep_calls{};
std::size_t sleep_count{};

void record_sleep(unsigned ms) noexcept {
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
    if (sleep_count < sleep_calls.size()) {
        sleep_calls[sleep_count] = ms;
    }
    ++sleep_count;
}

void reset_sleep() noexcept {
    sleep_calls = {};
    sleep_count = 0;
}

veyra::WindowEvent make_bare(veyra::WindowEventKind kind) {
    veyra::WindowEvent event{};

    event.kind = kind;

    return event;
}

veyra::WindowEvent make_key(std::uint32_t code) {
    veyra::WindowEvent event{};

    event.kind = veyra::WindowEventKind::key_down;
    event.data.key = {
        .code = code,
        .modifiers = 0,
        .repeat = false,
    };

    return event;
}

void check_scripted_session_pins_order() {
    veyra::NullWindow window("session", 320, 240);
    veyra::FrameScheduler scheduler;
    auto log = std::string{};
    auto frames = 0;

    auto on_event = [&log](veyra::WindowEvent& event) noexcept {
        log += event.kind == veyra::WindowEventKind::key_down ? 'K' : 'E';
        return event.kind == veyra::WindowEventKind::close_request;
    };
    auto on_frame = [&log, &frames](bool surface_changed) noexcept {
        ++frames;
        log += surface_changed ? 'S' : 'F';
        return veyra::FrameAction{.rendered = true};
    };
    veyra::RunLoopConfig config{window, scheduler, on_event, on_frame, nullptr};

    require(window.push_event(make_key(11)), "key scripts");
    require(window.push_event(make_bare(veyra::WindowEventKind::pointer_down)), "pointer scripts");
    scheduler.request_immediate();

    const auto now = veyra::FrameScheduler::Clock::now();
    require(
        veyra::pump_once(config, now) == veyra::LoopResult::rendered,
        "scripted events run one frame"
    );
    require(log == "KEF", "events route before the frame runs");

    require(frames == 1, "one pump runs one frame");
    require(!scheduler.immediate_pending(), "pump consumes the immediate request");
    require(config.seen_generation == 0, "null generation stays flat without acquire");

    require(window.push_event(make_bare(veyra::WindowEventKind::close_request)), "close scripts");
    require(
        veyra::pump_once(config, now) == veyra::LoopResult::quit,
        "close request quits without a frame"
    );
    require(frames == 1, "quit skips the frame callback");
}

void check_coalescing_runs_one_frame() {
    veyra::NullWindow window("coalesce", 320, 240);
    veyra::FrameScheduler scheduler;
    auto frames = 0;

    auto on_event = [](veyra::WindowEvent&) noexcept {
        return false;
    };
    auto on_frame = [&frames](bool) noexcept {
        ++frames;
        return veyra::FrameAction{.rendered = true};
    };
    veyra::RunLoopConfig config{window, scheduler, on_event, on_frame, nullptr};

    for (auto index = 0; index < 5; ++index) {
        scheduler.request_immediate();
    }

    const auto now = veyra::FrameScheduler::Clock::now();
    require(
        veyra::pump_once(config, now) == veyra::LoopResult::rendered,
        "coalesced requests run a frame"
    );
    require(frames == 1, "five requests run one frame");
    require(
        veyra::pump_once(config, now) == veyra::LoopResult::idle,
        "consumed requests leave the loop idle"
    );
    require(frames == 1, "idle pump runs no frame");
}

void check_immediate_yields_zero_sleep() {
    veyra::NullWindow window("yield", 320, 240);
    veyra::FrameScheduler scheduler;
    auto frames = 0;

    auto on_event = [](veyra::WindowEvent&) noexcept {
        return false;
    };
    auto on_frame = [&frames, &scheduler](bool) noexcept {
        ++frames;
        if (frames == 1) {
            scheduler.request_immediate();
            return veyra::FrameAction{.rendered = true};
        }
        return veyra::FrameAction{.quit = true};
    };
    veyra::RunLoopConfig config{window, scheduler, on_event, on_frame, &record_sleep};

    scheduler.request_immediate();
    reset_sleep();
    require(
        veyra::run_until_quit(config) == veyra::LoopResult::quit,
        "frame callback quits the loop"
    );
    require(frames == 2, "loop runs until the frame callback quits");
    require(sleep_count == 1, "quit returns before the second sleep");
    require(sleep_calls[0] == 0, "immediate work yields with a zero sleep");
}

void check_far_deadline_sleeps_bounded() {
    veyra::NullWindow window("bound", 320, 240);
    veyra::FrameScheduler scheduler;

    auto on_event = [](veyra::WindowEvent&) noexcept {
        return false;
    };
    auto on_frame = [](bool) noexcept {
        return veyra::FrameAction{.rendered = true};
    };
    veyra::RunLoopConfig config{window, scheduler, on_event, on_frame, &record_sleep};
    config.frame_cap = 3;

    scheduler.set_deadline(veyra::FrameScheduler::Clock::now() + std::chrono::seconds(10));

    reset_sleep();
    require(
        veyra::run_until_quit(config) == veyra::LoopResult::capped,
        "frame cap stops an idle loop"
    );
    require(sleep_count == 3, "each idle turn sleeps once");
    for (std::size_t index = 0; index < sleep_count; ++index) {
        require(
            sleep_calls[index] == veyra::kRunLoopMaxSleepMs,
            "far deadline sleeps the bounded maximum"
        );
    }
}

void check_deadline_wakes_a_frame() {
    veyra::NullWindow window("wake", 320, 240);
    veyra::FrameScheduler scheduler;
    auto frames = 0;

    auto on_event = [](veyra::WindowEvent&) noexcept {
        return false;
    };
    auto on_frame = [&frames](bool) noexcept {
        ++frames;
        return veyra::FrameAction{.rendered = true, .quit = true};
    };
    veyra::RunLoopConfig config{window, scheduler, on_event, on_frame, &record_sleep};

    scheduler.set_deadline(veyra::FrameScheduler::Clock::now() + std::chrono::milliseconds(120));

    reset_sleep();
    require(
        veyra::run_until_quit(config) == veyra::LoopResult::quit,
        "deadline wake runs the frame"
    );
    require(frames == 1, "deadline fires once");
    require(sleep_count >= 2, "wake waits through bounded sleeps");
    require(sleep_count <= 1000, "bounded sleeps cannot spin forever");
    for (std::size_t index = 0; index < sleep_count && index < sleep_calls.size(); ++index) {
        require(
            sleep_calls[index] <= veyra::kRunLoopMaxSleepMs,
            "every sleep stays within the bound"
        );
    }
}

void check_no_work_sleeps_bounded() {
    veyra::NullWindow window("idle", 320, 240);
    veyra::FrameScheduler scheduler;
    auto frames = 0;

    auto on_event = [](veyra::WindowEvent&) noexcept {
        return false;
    };
    auto on_frame = [&frames](bool) noexcept {
        ++frames;
        return veyra::FrameAction{.rendered = true};
    };
    veyra::RunLoopConfig config{window, scheduler, on_event, on_frame, &record_sleep};
    config.frame_cap = 4;

    reset_sleep();
    require(
        veyra::run_until_quit(config) == veyra::LoopResult::capped,
        "cap stops a loop with no work"
    );
    require(frames == 0, "no work runs no frame");
    require(sleep_count == 4, "each idle turn sleeps once");
    for (std::size_t index = 0; index < sleep_count; ++index) {
        require(
            sleep_calls[index] == veyra::kRunLoopIdleSleepMs,
            "idle loop sleeps the fail-safe bound"
        );
    }
}

void check_surface_generation_triggers_frame() {
    veyra::NullWindow window("surface", 64, 48);
    veyra::FrameScheduler scheduler;
    auto frames = 0;
    auto saw_changed = false;

    auto on_event = [](veyra::WindowEvent&) noexcept {
        return false;
    };
    auto on_frame = [&frames, &saw_changed](bool surface_changed) noexcept {
        ++frames;
        saw_changed = surface_changed;
        return veyra::FrameAction{.rendered = true};
    };
    veyra::RunLoopConfig config{window, scheduler, on_event, on_frame, nullptr};

    const auto now = veyra::FrameScheduler::Clock::now();
    require(
        veyra::pump_once(config, now) == veyra::LoopResult::idle,
        "flat generation leaves the loop idle"
    );
    require(frames == 0, "idle pump runs no frame");

    veyra::WindowEvent resize{};
    resize.kind = veyra::WindowEventKind::resize;
    resize.data.resize = {.width = 80.0F, .height = 30.0F};
    require(window.push_event(resize), "resize scripts");

    scheduler.request_immediate();
    require(
        veyra::pump_once(config, now) == veyra::LoopResult::rendered,
        "resize pump runs a frame"
    );
    require(!saw_changed, "generation only moves on acquire");

    std::array<veyra::SoftwarePixel, static_cast<std::size_t>(80) * 30> backing{};
    veyra::SoftwareSurface surface{};
    require(
        window.acquire_surface(surface, backing) == veyra::WindowResult::ready,
        "resized surface acquires"
    );
    require(window.present() == veyra::WindowResult::ready, "resized surface presents");

    frames = 0;
    require(
        veyra::pump_once(config, now) == veyra::LoopResult::rendered,
        "generation flip runs a frame"
    );
    require(frames == 1, "flip runs exactly one frame");
    require(saw_changed, "flip reports the surface change");
    require(config.seen_generation == 1, "pump records the new generation");
    require(
        veyra::pump_once(config, now) == veyra::LoopResult::idle,
        "recorded generation leaves the loop idle"
    );
}

void check_render_failure_aborts() {
    veyra::NullWindow window("failed", 320, 240);
    veyra::FrameScheduler scheduler;
    auto frames = 0;

    auto on_event = [](veyra::WindowEvent&) noexcept {
        return false;
    };
    auto on_frame = [&frames](bool) noexcept {
        ++frames;
        return veyra::FrameAction{.failed = true};
    };
    veyra::RunLoopConfig config{window, scheduler, on_event, on_frame, &record_sleep};

    scheduler.request_immediate();
    const auto now = veyra::FrameScheduler::Clock::now();
    require(
        veyra::pump_once(config, now) == veyra::LoopResult::failed,
        "failed frame aborts the pump"
    );

    scheduler.request_immediate();
    reset_sleep();
    require(
        veyra::run_until_quit(config) == veyra::LoopResult::failed,
        "failed frame aborts the loop"
    );
    require(frames == 2, "loop stops at the first failure");
    require(sleep_count == 0, "failure returns before sleeping");
}

void check_empty_poll_terminates_on_cap() {
    veyra::NullWindow window("empty", 320, 240);
    veyra::FrameScheduler scheduler;
    auto events = 0;
    auto frames = 0;

    auto on_event = [&events](veyra::WindowEvent&) noexcept {
        ++events;
        return false;
    };
    auto on_frame = [&frames](bool) noexcept {
        ++frames;
        return veyra::FrameAction{.rendered = true};
    };
    veyra::RunLoopConfig config{window, scheduler, on_event, on_frame, &record_sleep};
    config.frame_cap = 5;

    reset_sleep();
    require(
        veyra::run_until_quit(config) == veyra::LoopResult::capped,
        "empty poll terminates on the cap"
    );
    require(events == 0, "empty poll routes no events");
    require(frames == 0, "nothing due runs no frame");
    require(sleep_count == 5, "cap bounds every idle turn");
}

}

int main() {
    try {
        check_scripted_session_pins_order();
        check_coalescing_runs_one_frame();
        check_immediate_yields_zero_sleep();
        check_far_deadline_sleeps_bounded();
        check_deadline_wakes_a_frame();
        check_no_work_sleeps_bounded();
        check_surface_generation_triggers_frame();
        check_render_failure_aborts();
        check_empty_poll_terminates_on_cap();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
