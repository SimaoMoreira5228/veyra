#include "veyra/core/event/focus_navigation.hpp"

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
    std::size_t items{1'024};
    std::size_t queries{10'000};
    std::size_t warmup{100};
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

    if (options.items < 2 || options.items > 1'000'000 || options.queries == 0 ||
        options.queries > 10'000'000 || options.warmup > 10'000'000) {
        throw std::invalid_argument("items must be 2 or more and values must be within limits");
    }
    return options;
}

double percentile(const std::vector<double>& sorted_times, std::size_t percent) {
    const auto last = sorted_times.size() - 1;
    const auto index = last / 100 * percent + last % 100 * percent / 100;
    return sorted_times[index];
}

int run(const Options& options) {
    veyra::NodeTree tree;
    tree.reserve(options.items + 1);
    const auto root = tree.create();
    std::vector<veyra::NodeId> nodes(options.items);
    std::vector<veyra::LayoutRect> bounds(options.items + 1);
    std::vector<double> timings;
    timings.reserve(options.queries);

    for (std::size_t index = 0; index < options.items; ++index) {
        const auto node = tree.create();
        if (!tree.append_child(root, node) || !tree.set_focusable(node, true)) {
            throw std::runtime_error("focus benchmark node setup failed");
        }

        nodes[index] = node;
        bounds[node.index] = {
            .x = static_cast<float>(index * 24),
            .y = static_cast<float>((index % 7) * 32),
            .width = 20.0F,
            .height = 24.0F,
        };
    }

    for (std::size_t query = 0; query < options.warmup; ++query) {
        const auto current = nodes[query % (options.items / 2)];
        const auto move = veyra::move_focus_spatially(
            tree,
            root,
            current,
            veyra::SpatialFocusDirection::right,
            bounds
        );
        if (move.result != veyra::FocusMoveResult::moved) {
            throw std::runtime_error("spatial focus failed during warmup");
        }
    }

    std::size_t checksum = 0;
    for (std::size_t query = 0; query < options.queries; ++query) {
        const auto current = nodes[query % (options.items / 2)];
        const auto begin = Clock::now();
        const auto move = veyra::move_focus_spatially(
            tree,
            root,
            current,
            veyra::SpatialFocusDirection::right,
            bounds
        );
        const auto end = Clock::now();
        if (move.result != veyra::FocusMoveResult::moved) {
            throw std::runtime_error("spatial focus failed during measurement");
        }

        timings.push_back(std::chrono::duration<double, std::nano>(end - begin).count());
        checksum += move.target.index;
    }

    std::sort(timings.begin(), timings.end());
    std::cout << "scenario,platform,compiler,build_config,focusable_nodes,warmup_queries,"
                 "measured_queries,p50_ns,p95_ns,p99_ns,max_ns,checksum\n"
              << "spatial_right_beam_preferred," << VEYRA_BUILD_PLATFORM << ','
              << VEYRA_BUILD_COMPILER << ',' << VEYRA_BUILD_CONFIG << ',' << options.items << ','
              << options.warmup << ',' << options.queries << ',' << percentile(timings, 50) << ','
              << percentile(timings, 95) << ',' << percentile(timings, 99) << ',' << timings.back()
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
