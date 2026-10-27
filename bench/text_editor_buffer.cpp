#include "veyra/text/editing/text_editor_buffer.hpp"

#include <algorithm>
#include <array>
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

template <typename Operation>
void run_scenario(
    std::string_view name,
    veyra::TextEditorBuffer& editor,
    const Options& options,
    std::vector<double>& timings,
    Operation operation
) {
    timings.clear();
    for (std::size_t query = 0; query < options.warmup; ++query) {
        operation();
    }

    std::size_t checksum = 0;
    for (std::size_t query = 0; query < options.queries; ++query) {
        const auto begin = Clock::now();
        operation();
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
    std::vector<char> multiline_text(options.bytes, 'a');
    std::vector<double> timings;
    timings.reserve(options.queries);

    for (std::size_t index = 80; index < multiline_text.size(); index += 80) {
        multiline_text[index] = '\n';
    }

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
        run_scenario("gap_buffer_local_insert_delete", editor, options, timings, [&editor]() {
            if (editor.insert("x") != veyra::TextEditResult::complete ||
                editor.erase_backward() != veyra::TextEditResult::complete) {
                throw std::runtime_error("text edit failed");
            }
        });
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
        run_scenario(
            "gap_buffer_local_insert_delete_with_history",
            editor,
            options,
            timings,
            [&editor]() {
                if (editor.insert("x") != veyra::TextEditResult::complete ||
                    editor.erase_backward() != veyra::TextEditResult::complete) {
                    throw std::runtime_error("text edit failed");
                }
            }
        );
    }

    {
        veyra::TextEditorBuffer editor(
            storage,
            std::string_view(multiline_text.data(), multiline_text.size())
        );
        if (editor.initialization_result() != veyra::TextEditResult::complete ||
            editor.set_cursor(options.bytes / 2) != veyra::TextEditResult::complete) {
            throw std::runtime_error("multiline buffer setup failed");
        }
        run_scenario("hard_line_home_end", editor, options, timings, [&editor]() {
            if (editor.move_cursor_to_line_start() != veyra::TextEditResult::complete ||
                editor.move_cursor_to_line_end() != veyra::TextEditResult::complete) {
                throw std::runtime_error("line navigation failed");
            }
        });
    }

    {
        std::fill(multiline_text.begin(), multiline_text.end(), 'a');
        for (std::size_t index = 79; index < multiline_text.size(); index += 80) {
            multiline_text[index] = '\n';
        }
        if (options.bytes > 80) {
            const auto first_line_end = std::min(std::size_t{79}, options.bytes);
            const auto second_line_end = std::min(std::size_t{159}, options.bytes);
            std::vector<veyra::TextCaretStop> first_line_stops;
            std::vector<veyra::TextCaretStop> second_line_stops;
            first_line_stops.reserve(first_line_end + 1);
            second_line_stops.reserve(second_line_end - 79);
            for (std::size_t offset = 0; offset <= first_line_end; ++offset) {
                first_line_stops.push_back({
                    .byte_offset = offset,
                    .x = static_cast<float>(offset),
                });
            }
            for (std::size_t offset = 80; offset <= second_line_end; ++offset) {
                second_line_stops.push_back({
                    .byte_offset = offset,
                    .x = static_cast<float>(offset - 80),
                });
            }

            const std::array lines{
                veyra::TextVisualLine{.caret_stops = first_line_stops},
                veyra::TextVisualLine{.caret_stops = second_line_stops},
            };

            veyra::TextEditorBuffer edge_editor(
                storage,
                std::string_view(multiline_text.data(), multiline_text.size())
            );
            if (edge_editor.initialization_result() != veyra::TextEditResult::complete ||
                edge_editor.set_cursor(40) != veyra::TextEditResult::complete) {
                throw std::runtime_error("visual edge buffer setup failed");
            }
            auto selected_stop = std::size_t{40};
            run_scenario(
                "visual_line_edge_navigation",
                edge_editor,
                options,
                timings,
                [&edge_editor, &first_line_stops, &selected_stop]() {
                    const auto edge = selected_stop == 0 ? veyra::TextVisualEdge::right
                                                         : veyra::TextVisualEdge::left;
                    if (edge_editor
                            .move_cursor_to_visual_edge(first_line_stops, edge, selected_stop) !=
                        veyra::TextEditResult::complete) {
                        throw std::runtime_error("visual line edge navigation failed");
                    }
                }
            );

            veyra::TextEditorBuffer editor(
                storage,
                std::string_view(multiline_text.data(), multiline_text.size())
            );
            if (editor.initialization_result() != veyra::TextEditResult::complete ||
                editor.set_cursor(40) != veyra::TextEditResult::complete) {
                throw std::runtime_error("visual line buffer setup failed");
            }
            auto position = veyra::TextVisualPosition{.line_index = 0, .stop_index = 40};
            run_scenario(
                "vertical_visual_line_navigation",
                editor,
                options,
                timings,
                [&editor, &lines, &position]() {
                    const auto direction = position.line_index == 0
                        ? veyra::TextVerticalDirection::down
                        : veyra::TextVerticalDirection::up;
                    if (editor
                            .move_cursor_vertically(lines, position, direction, 40.0F, position) !=
                        veyra::TextEditResult::complete) {
                        throw std::runtime_error("vertical line navigation failed");
                    }
                }
            );
        }
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
