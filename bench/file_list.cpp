#include "veyra/files/file_list.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;

constexpr std::size_t entry_count = 100'000;
constexpr std::size_t warmup_count = 10;
constexpr std::size_t query_count = 100;

double percentile(std::span<const double> sorted_times, std::size_t percent) {
    const auto last = sorted_times.size() - 1;
    const auto index = last / 100 * percent + last % 100 * percent / 100;
    return sorted_times[index];
}

int run() {
    std::vector<std::string> names;
    names.reserve(entry_count);
    for (std::size_t index = 0; index < entry_count; ++index) {
        names.push_back("file-" + std::to_string(index) + ".dat");
    }

    std::vector<veyra::FileListEntry> entries;
    entries.reserve(entry_count);
    for (std::size_t index = 0; index < entry_count; ++index) {
        entries.push_back({
            .id = static_cast<std::uint64_t>(index + 1),
            .name = names[index],
            .size_bytes = static_cast<std::uint64_t>(index * 17),
            .modified_nanoseconds = static_cast<std::int64_t>(entry_count - index),
            .is_directory = index % 16 == 0,
        });
    }
    std::reverse(entries.begin(), entries.end());

    std::vector<std::size_t> output(entry_count);
    std::array<double, query_count> times{};
    auto output_count = std::size_t{0};
    const auto sort = veyra::FileListSort{
        .key = veyra::FileListSortKey::modified_time,
        .direction = veyra::FileListSortDirection::descending,
    };
    const auto run_query = [&]() {
        const auto result =
            veyra::build_file_list_order(entries, "file-9", sort, output, output_count);
        if (result != veyra::FileListResult::complete) {
            throw std::runtime_error("file list query failed");
        }
    };

    for (std::size_t index = 0; index < warmup_count; ++index) {
        run_query();
    }

    std::size_t checksum = 0;
    for (auto& elapsed : times) {
        const auto begin = Clock::now();
        run_query();
        const auto end = Clock::now();
        elapsed = std::chrono::duration<double, std::micro>(end - begin).count();
        checksum += output_count + output[0];
    }
    std::sort(times.begin(), times.end());

    std::cout << "scenario,platform,compiler,build_config,entries,warmup_queries,measured_queries,"
                 "p50_us,p95_us,p99_us,max_us,checksum\n";
    std::cout << "file_list_filter_sort," << VEYRA_BUILD_PLATFORM << ',' << VEYRA_BUILD_COMPILER
              << ',' << VEYRA_BUILD_CONFIG << ',' << entry_count << ',' << warmup_count << ','
              << query_count << ',' << percentile(times, 50) << ',' << percentile(times, 95) << ','
              << percentile(times, 99) << ',' << times.back() << ',' << checksum << '\n';
    return 0;
}

}

int main() {
    try {
        return run();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
