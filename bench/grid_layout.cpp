#include "veyra/layout/grid/grid.hpp"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;

struct Options {
    std::size_t items{1'024};
    std::size_t frames{10'000};
    std::size_t warmup{1'000};
    bool fractional{};
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
        if (argument == "--fractional") {
            options.fractional = true;
            continue;
        }
        if (index + 1 >= argc) {
            throw std::invalid_argument("missing value for " + std::string(argument));
        }

        const auto value = parse_size(argv[++index]);
        if (argument == "--items") {
            options.items = value;
        } else if (argument == "--frames") {
            options.frames = value;
        } else if (argument == "--warmup") {
            options.warmup = value;
        } else {
            throw std::invalid_argument("unknown option: " + std::string(argument));
        }
    }

    if (options.items == 0 || options.items > 200'000 || options.frames == 0 ||
        options.frames > 10'000'000 || options.warmup > 10'000'000) {
        throw std::invalid_argument(
            "items and frames must be positive and all values within limits"
        );
    }
    return options;
}

double percentile(const std::vector<double>& sorted_times, std::size_t percent) {
    const auto last = sorted_times.size() - 1;
    const auto index = last / 100 * percent + last % 100 * percent / 100;
    return sorted_times[index];
}

int run(const Options& options) {
    constexpr std::size_t column_count = 8;
    const auto row_count = (options.items + column_count - 1) / column_count;
    const auto height = static_cast<float>(row_count) * 24.0F;
    if (!std::isfinite(height)) {
        throw std::invalid_argument("items exceed the representable grid height");
    }

    std::vector<veyra::GridTrack> columns(column_count);
    std::vector<veyra::GridTrack> rows(row_count);
    if (options.fractional) {
        for (auto& column : columns) {
            column.fraction = 1.0F;
        }
    }
    std::vector<veyra::GridItem> items(options.items);
    std::vector<veyra::IntrinsicSize> measurements(options.items);
    for (std::size_t index = 0; index < options.items; ++index) {
        items[index] = {
            .row = index / column_count,
            .column = index % column_count,
        };
        measurements[index] = {
            .width = {.min_content = 8.0F, .preferred = 24.0F, .max_content = 48.0F},
            .height = {.min_content = 12.0F, .preferred = 20.0F, .max_content = 32.0F},
        };
    }
    std::vector<veyra::GridTrackWorkspace> column_workspace(column_count);
    std::vector<veyra::GridTrackWorkspace> row_workspace(row_count);
    const auto workspace = veyra::GridLayoutWorkspace{
        .columns = column_workspace,
        .rows = row_workspace,
    };
    std::vector<veyra::LayoutRect> output(options.items);
    std::vector<double> timings;
    timings.reserve(options.frames);

    const auto container = veyra::GridContainer{
        .width = 640.0F,
        .height = height,
        .gap = 2.0F,
    };
    for (std::size_t frame = 0; frame < options.warmup; ++frame) {
        if (veyra::layout_grid(container, columns, rows, items, measurements, workspace, output) !=
            veyra::GridLayoutResult::complete) {
            throw std::runtime_error("grid layout failed during warmup");
        }
    }

    for (std::size_t frame = 0; frame < options.frames; ++frame) {
        const auto begin = Clock::now();
        const auto result =
            veyra::layout_grid(container, columns, rows, items, measurements, workspace, output);
        const auto end = Clock::now();
        if (result != veyra::GridLayoutResult::complete) {
            throw std::runtime_error("grid layout failed during measurement");
        }
        timings.push_back(std::chrono::duration<double, std::nano>(end - begin).count());
    }

    std::sort(timings.begin(), timings.end());
    double checksum = 0.0;
    for (const auto& rect : output) {
        checksum += rect.x + rect.y + rect.width + rect.height;
    }

    std::cout << "scenario,platform,compiler,build_config,items,warmup_frames,measured_frames,"
                 "p50_ns,p95_ns,p99_ns,max_ns,checksum\n"
              << (options.fractional ? "grid_fractional_tracks" : "grid_auto_tracks") << ','
              << VEYRA_BUILD_PLATFORM << ',' << VEYRA_BUILD_COMPILER << ',' << VEYRA_BUILD_CONFIG
              << ',' << options.items << ',' << options.warmup << ',' << options.frames << ','
              << percentile(timings, 50) << ',' << percentile(timings, 95) << ','
              << percentile(timings, 99) << ',' << timings.back() << ',' << checksum << '\n';
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
