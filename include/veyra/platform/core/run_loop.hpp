#ifndef VEYRA_PLATFORM_RUN_LOOP_HPP
#define VEYRA_PLATFORM_RUN_LOOP_HPP

#include "veyra/core/schedule/frame_scheduler.hpp"
#include "veyra/platform/core/window.hpp"

#include <chrono>
#include <cstdint>
#include <optional>

namespace veyra {

enum class LoopResult : std::uint8_t {
    idle,
    rendered,
    quit,
    failed,
    capped,
};

struct FrameAction {
    bool rendered{};
    bool quit{};
    bool failed{};
};

// NOTE: run-loop sleep is always bounded. Immediate or past-due work yields with 0ms, a
// future deadline sleeps at most max_sleep_ms, and a loop with no work sleeps idle_sleep_ms.
inline constexpr unsigned kRunLoopIdleSleepMs = 8;
inline constexpr unsigned kRunLoopMaxSleepMs = 8;

template <WindowBackend Backend, typename OnEvent, typename OnFrame> struct RunLoopConfig {
    Backend& backend;
    FrameScheduler& scheduler;
    OnEvent on_event;
    OnFrame on_frame;
    void (*sleep_ms)(unsigned) noexcept = nullptr;
    std::uint64_t seen_generation{};
    unsigned idle_sleep_ms{kRunLoopIdleSleepMs};
    unsigned max_sleep_ms{kRunLoopMaxSleepMs};
    std::uint64_t frame_cap{};
};

template <WindowBackend Backend, typename OnEvent, typename OnFrame>
LoopResult
pump_once(RunLoopConfig<Backend, OnEvent, OnFrame>& config, FrameScheduler::Deadline now) noexcept {
    auto event = WindowEvent{};
    while (config.backend.poll_event(event)) {
        if (config.on_event(event)) {
            return LoopResult::quit;
        }
    }

    const auto generation = config.backend.surface_generation();
    const bool surface_changed = generation != config.seen_generation;
    config.seen_generation = generation;

    const bool due = config.scheduler.frame_due(now);
    if (!due && !surface_changed) {
        return LoopResult::idle;
    }
    if (due) {
        config.scheduler.consume_frame(now);
    }

    const FrameAction action = config.on_frame(surface_changed);
    if (action.failed) {
        return LoopResult::failed;
    }
    if (action.quit) {
        return LoopResult::quit;
    }
    if (action.rendered) {
        return LoopResult::rendered;
    }
    return LoopResult::idle;
}

template <WindowBackend Backend, typename OnEvent, typename OnFrame>
LoopResult run_until_quit(RunLoopConfig<Backend, OnEvent, OnFrame>& config) noexcept {
    auto iterations = std::uint64_t{};
    while (true) {
        if (config.frame_cap != 0 && iterations >= config.frame_cap) {
            return LoopResult::capped;
        }
        const LoopResult result = pump_once(config, FrameScheduler::Clock::now());
        if (result == LoopResult::quit || result == LoopResult::failed) {
            return result;
        }
        ++iterations;

        if (config.sleep_ms == nullptr) {
            continue;
        }
        if (config.scheduler.immediate_pending()) {
            config.sleep_ms(0);
            continue;
        }
        const std::optional<FrameScheduler::Deadline> deadline = config.scheduler.deadline();
        if (!deadline.has_value()) {
            config.sleep_ms(config.idle_sleep_ms);
            continue;
        }
        const auto remaining = *deadline - FrameScheduler::Clock::now();
        if (remaining.count() <= 0) {
            config.sleep_ms(0);
            continue;
        }
        if (remaining > std::chrono::milliseconds(config.max_sleep_ms)) {
            config.sleep_ms(config.max_sleep_ms);
            continue;
        }
        const auto wait = std::chrono::duration_cast<std::chrono::milliseconds>(remaining);
        config.sleep_ms(wait.count() <= 0 ? 1 : static_cast<unsigned>(wait.count()));
    }
}

}

#endif
