#include "veyra/files/file_list.hpp"

#include <algorithm>
#include <charconv>
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

struct Options {
    std::size_t entries{100'000};
    std::size_t warmup{10};
    std::size_t queries{100};
};

std::size_t parse_count(
    std::string_view value,
    std::string_view option,
    std::size_t maximum,
    bool allow_zero = false
) {
    auto count = std::size_t{};
    const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), count);
    if (error != std::errc{} || end != value.data() + value.size() || (!allow_zero && count == 0) ||
        count > maximum) {
        throw std::invalid_argument("invalid value for " + std::string(option));
    }
    return count;
}

Options parse_options(int argc, char** argv) {
    auto options = Options{};
    for (auto index = 1; index < argc; ++index) {
        const auto option = std::string_view(argv[index]);
        if (index + 1 >= argc) {
            throw std::invalid_argument("missing value for " + std::string(option));
        }

        const auto value = std::string_view(argv[++index]);
        if (option == "--entries") {
            options.entries = parse_count(value, option, 1'000'000);
        } else if (option == "--warmup") {
            options.warmup = parse_count(value, option, 1'000'000, true);
        } else if (option == "--queries") {
            options.queries = parse_count(value, option, 1'000'000);
        } else {
            throw std::invalid_argument("unknown option " + std::string(option));
        }
    }
    return options;
}

double percentile(std::span<const double> sorted_times, std::size_t percent) {
    const auto last = sorted_times.size() - 1;
    const auto index = last / 100 * percent + last % 100 * percent / 100;
    return sorted_times[index];
}

int run(const Options& options) {
    std::vector<std::string> names;
    names.reserve(options.entries);
    for (std::size_t index = 0; index < options.entries; ++index) {
        names.push_back("file-" + std::to_string(index) + ".dat");
    }

    std::vector<veyra::FileListEntry> entries;
    entries.reserve(options.entries);
    for (std::size_t index = 0; index < options.entries; ++index) {
        entries.push_back({
            .id = static_cast<std::uint64_t>(index + 1),
            .name = names[index],
            .size_bytes = static_cast<std::uint64_t>(index * 17),
            .modified_nanoseconds = static_cast<std::int64_t>(options.entries - index),
            .is_directory = index % 16 == 0,
        });
    }
    std::reverse(entries.begin(), entries.end());

    std::vector<std::size_t> output(options.entries);
    std::vector<double> times(options.queries);
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

    for (std::size_t index = 0; index < options.warmup; ++index) {
        run_query();
    }

    std::uint64_t checksum = 0;
    for (auto& elapsed : times) {
        const auto begin = Clock::now();
        run_query();
        const auto end = Clock::now();
        elapsed = std::chrono::duration<double, std::micro>(end - begin).count();
        checksum += output_count + output[0];
    }
    std::sort(times.begin(), times.end());

    const auto all_entries_result =
        veyra::build_file_list_order(entries, {}, sort, output, output_count);
    if (all_entries_result != veyra::FileListResult::complete) {
        throw std::runtime_error("unfiltered file list query failed");
    }

    std::vector<std::uint64_t> selection_storage(options.entries);
    auto selection = veyra::FileListSelection(selection_storage);
    const auto visible_order = std::span<const std::size_t>(output.data(), output_count);
    const auto anchor_position = visible_order.size() / 2;
    const auto range_end = std::min(visible_order.size() - 1, anchor_position + 1023);
    std::vector<double> selection_times(options.queries);
    const auto anchor_result = selection.update(entries, visible_order, anchor_position);
    if (anchor_result != veyra::FileListSelectionResult::complete) {
        throw std::runtime_error("file list selection anchor failed");
    }

    const auto run_selection = [&]() {
        const auto result = selection.update(
            entries,
            visible_order,
            range_end,
            {
                .extend_range = true,
            }
        );
        if (result != veyra::FileListSelectionResult::complete) {
            throw std::runtime_error("file list range selection failed");
        }
    };
    for (std::size_t index = 0; index < options.warmup; ++index) {
        run_selection();
    }

    std::uint64_t selection_checksum = 0;
    for (auto& elapsed : selection_times) {
        const auto begin = Clock::now();
        run_selection();
        const auto end = Clock::now();
        elapsed = std::chrono::duration<double, std::micro>(end - begin).count();
        selection_checksum += selection.ids().size() + selection.ids().front();
    }
    std::sort(selection_times.begin(), selection_times.end());

    std::cout << "scenario,platform,compiler,build_config,entries,warmup_queries,measured_queries,"
                 "p50_us,p95_us,p99_us,max_us,checksum\n";
    std::cout << "file_list_filter_sort," << VEYRA_BUILD_PLATFORM << ',' << VEYRA_BUILD_COMPILER
              << ',' << VEYRA_BUILD_CONFIG << ',' << options.entries << ',' << options.warmup << ','
              << options.queries << ',' << percentile(times, 50) << ',' << percentile(times, 95)
              << ',' << percentile(times, 99) << ',' << times.back() << ',' << checksum << '\n';
    std::cout << "file_list_range_selection," << VEYRA_BUILD_PLATFORM << ',' << VEYRA_BUILD_COMPILER
              << ',' << VEYRA_BUILD_CONFIG << ',' << options.entries << ',' << options.warmup << ','
              << options.queries << ',' << percentile(selection_times, 50) << ','
              << percentile(selection_times, 95) << ',' << percentile(selection_times, 99) << ','
              << selection_times.back() << ',' << selection_checksum << '\n';
    return 0;
}

}

int main(int argc, char** argv) {
    try {
        const auto options = parse_options(argc, argv);
        return run(options);
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
