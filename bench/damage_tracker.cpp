#include "veyra/render/damage/damage_tracker.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
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
    constexpr auto damage_count = std::size_t{48};
    constexpr auto warmup_count = std::size_t{500};
    constexpr auto measured_updates = std::size_t{5'000};
    std::array<veyra::LayoutRect, damage_count> damage_storage{};
    std::array<veyra::LayoutRect, damage_count> changed_regions{};
    for (std::size_t index = 0; index < changed_regions.size(); ++index) {
        const auto column = index % 8;
        const auto row = index / 8;
        changed_regions[index] = {
            .x = static_cast<float>(column * 40 + 4),
            .y = static_cast<float>(row * 40 + 4),
            .width = 12.0F,
            .height = 12.0F,
        };
    }

    veyra::DamageTracker tracker(320, 240, damage_storage);
    if (!tracker.valid()) {
        throw std::runtime_error("damage tracker setup failed");
    }

    for (std::size_t update = 0; update < warmup_count; ++update) {
        tracker.clear();
        for (const auto& region : changed_regions) {
            if (!tracker.add(region)) {
                throw std::runtime_error("damage tracker rejected a benchmark region");
            }
        }
    }

    std::vector<double> timings;
    timings.reserve(measured_updates);
    std::size_t checksum = 0;
    for (std::size_t update = 0; update < measured_updates; ++update) {
        tracker.clear();
        const auto begin = Clock::now();
        for (const auto& region : changed_regions) {
            if (!tracker.add(region)) {
                throw std::runtime_error("damage tracker rejected a benchmark region");
            }
        }
        const auto end = Clock::now();
        timings.push_back(std::chrono::duration<double, std::nano>(end - begin).count());
        checksum += tracker.regions().size();
    }

    std::sort(timings.begin(), timings.end());
    std::cout << "scenario,platform,compiler,build_config,regions,warmup_updates,measured_updates,"
                 "p50_ns,p95_ns,p99_ns,max_ns,checksum\n"
              << "damage_tracker_48_disjoint," << VEYRA_BUILD_PLATFORM << ','
              << VEYRA_BUILD_COMPILER << ',' << VEYRA_BUILD_CONFIG << ',' << damage_count << ','
              << warmup_count << ',' << measured_updates << ',' << percentile(timings, 50) << ','
              << percentile(timings, 95) << ',' << percentile(timings, 99) << ',' << timings.back()
              << ',' << checksum << '\n';
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
