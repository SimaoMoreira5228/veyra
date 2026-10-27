#include "veyra/text/shaping/grapheme.hpp"

#include <algorithm>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;

double percentile(const std::vector<double>& sorted_times, std::size_t percent) {
    const auto last = sorted_times.size() - 1;
    const auto index = last / 100 * percent + last % 100 * percent / 100;
    return sorted_times[index];
}

int run() {
    constexpr std::size_t clusters = 349'525;
    constexpr std::size_t warmup = 1'000;
    constexpr std::size_t queries = 10'000;
    constexpr std::string_view cluster = "e\xcc\x81";

    std::string text;
    text.reserve(clusters * cluster.size());
    for (std::size_t index = 0; index < clusters; ++index) {
        text.append(cluster);
    }

    std::vector<char> storage(text.size() + 1);
    std::vector<double> timings;
    timings.reserve(queries);

    veyra::TextEditorBuffer editor(storage, text);
    const auto cursor = (clusters / 2) * cluster.size();
    if (editor.initialization_result() != veyra::TextEditResult::complete ||
        editor.set_cursor(cursor) != veyra::TextEditResult::complete) {
        throw std::runtime_error("text buffer setup failed");
    }

    const auto move_pair = [&editor]() {
        if (veyra::move_cursor_by_grapheme(editor, veyra::TextCursorDirection::backward) !=
                veyra::TextEditResult::complete ||
            veyra::move_cursor_by_grapheme(editor, veyra::TextCursorDirection::forward) !=
                veyra::TextEditResult::complete) {
            throw std::runtime_error("grapheme movement failed");
        }
    };

    for (std::size_t query = 0; query < warmup; ++query) {
        move_pair();
    }

    std::size_t checksum = 0;
    for (std::size_t query = 0; query < queries; ++query) {
        const auto begin = Clock::now();
        move_pair();
        const auto end = Clock::now();
        timings.push_back(std::chrono::duration<double, std::nano>(end - begin).count());
        checksum += editor.cursor();
    }

    std::sort(timings.begin(), timings.end());
    std::cout << "scenario,platform,compiler,build_config,document_bytes,warmup_queries,"
                 "measured_queries,p50_ns,p95_ns,p99_ns,max_ns,checksum\n"
              << "grapheme_backward_forward," << VEYRA_BUILD_PLATFORM << ',' << VEYRA_BUILD_COMPILER
              << ',' << VEYRA_BUILD_CONFIG << ',' << text.size() << ',' << warmup << ',' << queries
              << ',' << percentile(timings, 50) << ',' << percentile(timings, 95) << ','
              << percentile(timings, 99) << ',' << timings.back() << ',' << checksum << '\n';
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
