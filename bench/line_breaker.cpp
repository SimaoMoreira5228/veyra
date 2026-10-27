#include "veyra/text/shaping/line_breaker.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;

constexpr std::size_t warmup = 100;
constexpr std::size_t queries = 1'000;
constexpr std::size_t sample_bytes = std::size_t{64} * std::size_t{1024};

int run() {
    std::string text;
    text.reserve(sample_bytes);
    while (text.size() < sample_bytes) {
        text.append("Veyra wraps text at Unicode line opportunities. ");
    }

    std::array<veyra::TextLineBreak, 16'384> breaks{};
    std::vector<double> timings;
    timings.reserve(queries);
    veyra::TextLineBreaker breaker("en");
    std::size_t break_count = 0;

    for (std::size_t query = 0; query < warmup; ++query) {
        if (breaker.find_breaks(text, breaks, break_count) != veyra::LineBreakResult::complete) {
            throw std::runtime_error("line breaking failed during warmup");
        }
    }

    std::size_t checksum = 0;
    for (std::size_t query = 0; query < queries; ++query) {
        const auto begin = Clock::now();
        const auto result = breaker.find_breaks(text, breaks, break_count);
        const auto end = Clock::now();
        if (result != veyra::LineBreakResult::complete) {
            throw std::runtime_error("line breaking failed during measurement");
        }
        timings.push_back(std::chrono::duration<double, std::micro>(end - begin).count());
        checksum += breaks[break_count - 1].byte_offset;
    }

    std::sort(timings.begin(), timings.end());
    const auto percentile = [&timings](std::size_t percent) {
        const auto last = timings.size() - 1;
        const auto index = last / 100 * percent + last % 100 * percent / 100;
        return timings[index];
    };
    std::cout << "scenario,platform,compiler,build_config,document_bytes,warmup_queries,"
                 "measured_queries,p50_us,p95_us,p99_us,max_us,break_count,checksum\n"
              << "line_break_64k," << VEYRA_BUILD_PLATFORM << ',' << VEYRA_BUILD_COMPILER << ','
              << VEYRA_BUILD_CONFIG << ',' << text.size() << ',' << warmup << ',' << queries << ','
              << percentile(50) << ',' << percentile(95) << ',' << percentile(99) << ','
              << timings.back() << ',' << break_count << ',' << checksum << '\n';
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
