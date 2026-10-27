#include "veyra/media/animation/animation_timeline.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;

double percentile(const std::vector<double>& sorted_times, std::size_t percent) {
    const auto last = sorted_times.size() - 1;
    const auto index = last / 100 * percent + last % 100 * percent / 100;
    return sorted_times[index];
}

int run() {
    constexpr auto update_count = std::size_t{100'000};
    constexpr auto warmup_count = std::size_t{1'000};
    constexpr auto elapsed_per_update = std::uint64_t{16};
    const std::array<std::uint32_t, 6> durations{40, 60, 80, 100, 120, 160};
    veyra::AnimationTimeline timeline(durations);
    if (!timeline.valid()) {
        throw std::runtime_error("animation timeline setup failed");
    }

    for (std::size_t update = 0; update < warmup_count; ++update) {
        timeline.advance(elapsed_per_update);
    }

    std::vector<double> timings;
    timings.reserve(update_count);
    std::uint64_t checksum = 0;
    for (std::size_t update = 0; update < update_count; ++update) {
        const auto begin = Clock::now();
        const auto changed = timeline.advance(elapsed_per_update);
        const auto frame_index = timeline.current_frame_index();
        const auto until_next_frame = timeline.time_until_next_frame_ms();
        const auto end = Clock::now();
        timings.push_back(std::chrono::duration<double, std::nano>(end - begin).count());
        checksum +=
            frame_index + until_next_frame.value_or(0) + static_cast<std::uint64_t>(changed);
    }

    std::sort(timings.begin(), timings.end());
    std::cout << "scenario,platform,compiler,build_config,frames,warmup_updates,measured_updates,"
                 "p50_ns,p95_ns,p99_ns,max_ns,checksum\n"
              << "animation_timeline_update," << VEYRA_BUILD_PLATFORM << ',' << VEYRA_BUILD_COMPILER
              << ',' << VEYRA_BUILD_CONFIG << ',' << durations.size() << ',' << warmup_count << ','
              << update_count << ',' << percentile(timings, 50) << ',' << percentile(timings, 95)
              << ',' << percentile(timings, 99) << ',' << timings.back() << ',' << checksum << '\n';
    return 0;
}

}

int main() {
    try {
        return run();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 2;
    }
}
