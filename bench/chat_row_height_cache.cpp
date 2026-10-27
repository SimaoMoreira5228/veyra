#include "veyra/chat/rows/row_height_cache.hpp"

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
    constexpr auto slot_count = std::size_t{1024};
    constexpr auto key_count = std::size_t{128};
    constexpr auto warmup_count = std::size_t{1'000};
    constexpr auto query_count = std::size_t{100'000};
    auto slots = std::array<veyra::ChatRowHeightCacheSlot, slot_count>{};
    auto keys = std::array<veyra::ChatRowHeightKey, key_count>{};
    auto cache = veyra::ChatRowHeightCache(slots);
    if (!cache.valid()) {
        throw std::runtime_error("chat row height cache setup failed");
    }

    for (std::size_t index = 0; index < key_count; ++index) {
        keys[index] = {
            .message_id = index + 1,
            .available_width = static_cast<float>(480 + index % 3 * 80),
            .style_generation = index % 2,
        };
        if (cache.insert(keys[index], 24.0F + static_cast<float>(index % 5)) !=
            veyra::ChatRowHeightCacheResult::inserted) {
            throw std::runtime_error("chat row height cache warmup failed");
        }
    }

    auto height = 0.0F;
    for (std::size_t query = 0; query < warmup_count; ++query) {
        if (cache.find(keys[query % key_count], height) != veyra::ChatRowHeightCacheResult::hit) {
            throw std::runtime_error("chat row height cache warmup lookup missed");
        }
    }

    auto timings = std::vector<double>{};
    timings.reserve(query_count);
    auto checksum = std::uint64_t{0};
    for (std::size_t query = 0; query < query_count; ++query) {
        const auto& key = keys[query % key_count];
        const auto begin = Clock::now();
        const auto result = cache.find(key, height);
        const auto end = Clock::now();
        if (result != veyra::ChatRowHeightCacheResult::hit) {
            throw std::runtime_error("chat row height cache measurement lookup missed");
        }
        timings.push_back(std::chrono::duration<double, std::nano>(end - begin).count());
        checksum += static_cast<std::uint64_t>(height);
    }

    std::sort(timings.begin(), timings.end());
    std::cout
        << "scenario,platform,compiler,build_config,slots,keys,warmup_lookups,measured_lookups,"
           "p50_ns,p95_ns,p99_ns,max_ns,checksum\n"
        << "chat_row_height_cache_hit," << VEYRA_BUILD_PLATFORM << ',' << VEYRA_BUILD_COMPILER
        << ',' << VEYRA_BUILD_CONFIG << ',' << slot_count << ',' << key_count << ',' << warmup_count
        << ',' << query_count << ',' << percentile(timings, 50) << ',' << percentile(timings, 95)
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
