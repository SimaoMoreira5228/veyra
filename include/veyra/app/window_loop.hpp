#ifndef VEYRA_APP_WINDOW_LOOP_HPP
#define VEYRA_APP_WINDOW_LOOP_HPP

#include "veyra/app/window_set.hpp"
#include "veyra/core/schedule/frame_scheduler.hpp"
#include "veyra/platform/core/run_loop.hpp"

#include <array>
#include <chrono>
#include <cstdint>
#include <optional>
#include <span>

namespace veyra::app {

inline constexpr unsigned kWindowLoopIdleSleepMs = 8;
inline constexpr unsigned kWindowLoopMaxSleepMs = 8;
inline constexpr std::size_t kWindowLoopCapacity = 32;

template <typename Host>
concept WindowHost = requires(Host& host, WindowEvent& event, WindowId& window) {
    { host.poll_event(event, window) } noexcept -> std::same_as<bool>;
    { host.windows() } noexcept -> std::same_as<WindowSet&>;
    { host.needs_frame(window) } noexcept -> std::same_as<bool>;
};

template <WindowHost Host, typename OnEvent, typename OnFrame> struct WindowLoopConfig {
    Host& host;
    FrameScheduler& scheduler;
    OnEvent on_event;
    OnFrame on_frame;
    void (*sleep_ms)(unsigned) noexcept = nullptr;
    void (*wait_ms)(void*, unsigned) noexcept = nullptr;
    void* wait_user = nullptr;
    unsigned idle_sleep_ms{kWindowLoopIdleSleepMs};
    unsigned max_sleep_ms{kWindowLoopMaxSleepMs};
    std::uint64_t frame_cap{};
};

template <WindowHost Host, typename OnEvent, typename OnFrame>
LoopResult
pump_once(WindowLoopConfig<Host, OnEvent, OnFrame>& config, FrameScheduler::Deadline now) noexcept {
    auto event = WindowEvent{};
    auto window = WindowId{};

    while (config.host.poll_event(event, window)) {
        if (config.on_event(window, event)) {
            return LoopResult::quit;
        }
        config.scheduler.request_immediate();
    }

    if (config.host.windows().should_shutdown()) {
        return LoopResult::quit;
    }

    if (!config.scheduler.frame_due(now)) {
        return LoopResult::idle;
    }
    config.scheduler.consume_frame(now);

    auto windows = std::array<WindowId, kWindowLoopCapacity>{};
    const auto count = config.host.windows().open_windows(windows);
    auto rendered = false;

    for (auto index = std::size_t{}; index < count && index < windows.size(); ++index) {
        if (!config.host.needs_frame(windows[index])) {
            continue;
        }
        const auto action = config.on_frame(windows[index], false);
        if (action.failed) {
            return LoopResult::failed;
        }
        if (action.quit) {
            return LoopResult::quit;
        }
        rendered = rendered || action.rendered;
    }

    return rendered ? LoopResult::rendered : LoopResult::idle;
}

template <WindowHost Host, typename OnEvent, typename OnFrame>
void wait_or_sleep(WindowLoopConfig<Host, OnEvent, OnFrame>& config, unsigned ms) noexcept {
    if (config.wait_ms != nullptr) {
        config.wait_ms(config.wait_user, ms);
        return;
    }
    if (config.sleep_ms != nullptr) {
        config.sleep_ms(ms);
    }
}

template <WindowHost Host, typename OnEvent, typename OnFrame>
LoopResult run_until_quit(WindowLoopConfig<Host, OnEvent, OnFrame>& config) noexcept {
    auto iterations = std::uint64_t{};

    while (true) {
        if (config.frame_cap != 0 && iterations >= config.frame_cap) {
            return LoopResult::capped;
        }
        const auto result = pump_once(config, FrameScheduler::Clock::now());
        if (result == LoopResult::quit || result == LoopResult::failed) {
            return result;
        }
        ++iterations;

        if (config.sleep_ms == nullptr && config.wait_ms == nullptr) {
            continue;
        }
        if (config.scheduler.immediate_pending()) {
            wait_or_sleep(config, 0);
            continue;
        }
        const auto deadline = config.scheduler.deadline();
        if (!deadline.has_value()) {
            wait_or_sleep(config, config.idle_sleep_ms);
            continue;
        }
        const auto remaining = *deadline - FrameScheduler::Clock::now();
        if (remaining.count() <= 0) {
            wait_or_sleep(config, 0);
            continue;
        }
        if (remaining > std::chrono::milliseconds(config.max_sleep_ms)) {
            wait_or_sleep(config, config.max_sleep_ms);
            continue;
        }
        const auto wait = std::chrono::duration_cast<std::chrono::milliseconds>(remaining);
        wait_or_sleep(config, wait.count() <= 0 ? 1 : static_cast<unsigned>(wait.count()));
    }
}

}

#endif
