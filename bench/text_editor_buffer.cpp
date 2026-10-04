#include "veyra/text/text_editor_buffer.hpp"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;

struct Options {
    std::size_t bytes{1'000'000};
    std::size_t queries{10'000};
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
        if (argument == "--bytes") {
            options.bytes = value;
        } else if (argument == "--queries") {
            options.queries = value;
        } else if (argument == "--warmup") {
            options.warmup = value;
        } else {
            throw std::invalid_argument("unknown option: " + std::string(argument));
        }
    }

    if (options.bytes == 0 || options.bytes > 64'000'000 || options.queries == 0 ||
        options.queries > 10'000'000 || options.warmup > 10'000'000) {
        throw std::invalid_argument("buffer size and query counts must be within limits");
    }
    return options;
}

double percentile(const std::vector<double>& sorted_times, std::size_t percent) {
    const auto last = sorted_times.size() - 1;
    const auto index = last / 100 * percent + last % 100 * percent / 100;
    return sorted_times[index];
}

void run_scenario(
    std::string_view name,
    veyra::TextEditorBuffer& editor,
    const Options& options,
    std::vector<double>& timings
) {
    timings.clear();
    const auto edit = [&editor]() {
        if (editor.insert("x") != veyra::TextEditResult::complete ||
            editor.erase_backward() != veyra::TextEditResult::complete) {
            throw std::runtime_error("text edit failed");
        }
    };
    for (std::size_t query = 0; query < options.warmup; ++query) {
        edit();
    }

    std::size_t checksum = 0;
    for (std::size_t query = 0; query < options.queries; ++query) {
        const auto begin = Clock::now();
        edit();
        const auto end = Clock::now();
        timings.push_back(std::chrono::duration<double, std::nano>(end - begin).count());
        checksum += editor.size() + editor.cursor();
    }

    std::sort(timings.begin(), timings.end());
    std::cout << name << ',' << VEYRA_BUILD_PLATFORM << ',' << VEYRA_BUILD_COMPILER << ','
              << VEYRA_BUILD_CONFIG << ',' << options.bytes << ',' << options.warmup << ','
              << options.queries << ',' << percentile(timings, 50) << ',' << percentile(timings, 95)
              << ',' << percentile(timings, 99) << ',' << timings.back() << ',' << checksum << '\n';
}

int run(const Options& options) {
    std::vector<char> initial_text(options.bytes, 'a');
    std::vector<char> storage(options.bytes + 1);
    std::vector<char> history_storage(256);
    std::vector<double> timings;
    timings.reserve(options.queries);

    std::cout << "scenario,platform,compiler,build_config,document_bytes,warmup_queries,"
                 "measured_queries,p50_ns,p95_ns,p99_ns,max_ns,checksum\n";
    {
        veyra::TextEditorBuffer editor(
            storage,
            std::string_view(initial_text.data(), initial_text.size())
        );
        if (editor.initialization_result() != veyra::TextEditResult::complete ||
            editor.set_cursor(options.bytes / 2) != veyra::TextEditResult::complete) {
            throw std::runtime_error("text buffer setup failed");
        }
        run_scenario("gap_buffer_local_insert_delete", editor, options, timings);
    }

    {
        veyra::TextEditorBuffer editor(
            storage,
            std::string_view(initial_text.data(), initial_text.size()),
            history_storage
        );
        if (editor.initialization_result() != veyra::TextEditResult::complete ||
            editor.set_cursor(options.bytes / 2) != veyra::TextEditResult::complete) {
            throw std::runtime_error("history buffer setup failed");
        }
        run_scenario("gap_buffer_local_insert_delete_with_history", editor, options, timings);
    }
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
