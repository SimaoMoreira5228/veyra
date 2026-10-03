#include "veyra/text/bidi_resolver.hpp"

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
constexpr std::size_t sample_bytes = std::size_t{4} * std::size_t{1024};

int run() {
    constexpr std::string_view phrase = "Latin text \xd7\x90\xd7\x91\xd7\x92 123; ";
    std::string text;
    text.reserve(sample_bytes);
    while (text.size() < sample_bytes) {
        text.append(phrase);
    }

    veyra::TextBidiResolver resolver(text.size());
    std::array<veyra::TextBidiRun, 1'024> runs{};
    std::vector<double> timings;
    timings.reserve(queries);
    std::size_t run_count = 0;

    for (std::size_t query = 0; query < warmup; ++query) {
        if (resolver.set_paragraph(
                text,
                veyra::TextBidiParagraphDirection::automatic_left_to_right
            ) != veyra::TextBidiResult::complete ||
            resolver.visual_runs_for_line(0, text.size(), runs, run_count) !=
                veyra::TextBidiResult::complete) {
            throw std::runtime_error("bidirectional layout failed during warmup");
        }
    }

    std::size_t checksum = 0;
    for (std::size_t query = 0; query < queries; ++query) {
        const auto begin = Clock::now();
        const auto paragraph_result = resolver.set_paragraph(
            text,
            veyra::TextBidiParagraphDirection::automatic_left_to_right
        );
        const auto line_result = resolver.visual_runs_for_line(0, text.size(), runs, run_count);
        const auto end = Clock::now();
        if (paragraph_result != veyra::TextBidiResult::complete ||
            line_result != veyra::TextBidiResult::complete) {
            throw std::runtime_error("bidirectional layout failed during measurement");
        }
        timings.push_back(std::chrono::duration<double, std::micro>(end - begin).count());
        checksum += runs[run_count - 1].byte_offset;
    }

    std::sort(timings.begin(), timings.end());
    const auto percentile = [&timings](std::size_t percent) {
        const auto last = timings.size() - 1;
        const auto index = last / 100 * percent + last % 100 * percent / 100;
        return timings[index];
    };
    std::cout << "scenario,platform,compiler,build_config,document_bytes,warmup_queries,"
                 "measured_queries,p50_us,p95_us,p99_us,max_us,visual_run_count,checksum\n"
              << "bidi_resolve_line_4k," << VEYRA_BUILD_PLATFORM << ',' << VEYRA_BUILD_COMPILER
              << ',' << VEYRA_BUILD_CONFIG << ',' << text.size() << ',' << warmup << ',' << queries
              << ',' << percentile(50) << ',' << percentile(95) << ',' << percentile(99) << ','
              << timings.back() << ',' << run_count << ',' << checksum << '\n';
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
