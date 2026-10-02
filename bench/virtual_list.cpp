#include "veyra/layout/virtual_list.hpp"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;

struct Options {
    std::size_t items{1'000'000};
    std::size_t queries{100'000};
    std::size_t warmup{1'000};
};

std::size_t parse_size(const char* value) {
    const std::string_view text(value);
    std::size_t parsed = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), parsed);
    if (error != std::errc{} || end != text.data() + text.size()) {
        throw std::invalid_argument("expected a non-negative integer: " + std::string(text));
    }
    return parsed;
}

Options parse_options(int argc, char** argv) {
    Options options;
    for (int index = 1; index < argc; ++index) {
        const std::string_view argument(argv[index]);
        if (index + 1 >= argc) {
            throw std::invalid_argument("missing value for " + std::string(argument));
        }

        const auto value = parse_size(argv[++index]);
        if (argument == "--items") {
            options.items = value;
        } else if (argument == "--queries") {
            options.queries = value;
        } else if (argument == "--warmup") {
            options.warmup = value;
        } else {
            throw std::invalid_argument("unknown option: " + std::string(argument));
        }
    }

    if (options.items == 0 || options.queries == 0 || options.items > 10'000'000 ||
        options.queries > 10'000'000 || options.warmup > 10'000'000) {
        throw std::invalid_argument(
            "items and queries must be positive and all values within limits"
        );
    }
    return options;
}

double percentile(const std::vector<double>& sorted_times, std::size_t percent) {
    const auto last = sorted_times.size() - 1;
    const auto index = last / 100 * percent + last % 100 * percent / 100;
    return sorted_times[index];
}

double scroll_offset(std::size_t query, double total_extent) {
    constexpr std::uint64_t modulus = 1'000'003;
    const auto sample = (static_cast<std::uint64_t>(query) * 48'271 + 17) % modulus;
    return total_extent * static_cast<double>(sample) / static_cast<double>(modulus);
}

int run(const Options& options) {
    std::vector<float> extents(options.items);
    for (std::size_t index = 0; index < extents.size(); ++index) {
        extents[index] = 18.0F + static_cast<float>(index % 13);
    }
    std::vector<double> fenwick(options.items + 1);
    veyra::VirtualListIndex index;

    const auto build_begin = Clock::now();
    if (!index.reset(extents, fenwick)) {
        throw std::runtime_error("virtual list index initialization failed");
    }
    const auto build_end = Clock::now();

    std::vector<double> timings;
    timings.reserve(options.queries);
    veyra::VirtualListRange range;
    for (std::size_t query = 0; query < options.warmup; ++query) {
        if (!index.visible_range(scroll_offset(query, index.total_extent()), 720.0, 2, range)) {
            throw std::runtime_error("virtual range query failed during warmup");
        }
    }

    double checksum = 0.0;
    for (std::size_t query = 0; query < options.queries; ++query) {
        const auto scroll = scroll_offset(query + options.warmup, index.total_extent());
        const auto begin = Clock::now();
        const auto valid = index.visible_range(scroll, 720.0, 2, range);
        const auto end = Clock::now();
        if (!valid) {
            throw std::runtime_error("virtual range query failed during measurement");
        }

        timings.push_back(std::chrono::duration<double, std::nano>(end - begin).count());
        checksum += static_cast<double>(range.first_item + range.last_item) + range.first_offset;
    }

    std::sort(timings.begin(), timings.end());
    const auto build_ns = std::chrono::duration<double, std::nano>(build_end - build_begin).count();
    std::cout << "scenario,platform,compiler,build_config,items,warmup_queries,measured_queries,"
                 "build_ns,p50_ns,p95_ns,p99_ns,max_ns,total_extent,checksum\n"
              << "variable_height_random_access," << VEYRA_BUILD_PLATFORM << ','
              << VEYRA_BUILD_COMPILER << ',' << VEYRA_BUILD_CONFIG << ',' << options.items << ','
              << options.warmup << ',' << options.queries << ',' << build_ns << ','
              << percentile(timings, 50) << ',' << percentile(timings, 95) << ','
              << percentile(timings, 99) << ',' << timings.back() << ',' << index.total_extent()
              << ',' << checksum << '\n';
    return 0;
}

}

int main(int argc, char** argv) {
    try {
        return run(parse_options(argc, argv));
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 2;
    }
}
