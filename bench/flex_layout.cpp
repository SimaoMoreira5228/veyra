#include "veyra/layout/flex/flex.hpp"

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
    std::size_t items{64};
    std::size_t frames{10'000};
    std::size_t warmup{1'000};
    bool wrap{};
    bool baseline{};
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
        if (argument == "--wrap") {
            options.wrap = true;
            continue;
        }
        if (argument == "--baseline") {
            options.baseline = true;
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

    if (options.items == 0 || options.frames == 0 || options.items > 1'000'000 ||
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
    const auto width = options.wrap ? 640.0F : static_cast<float>(options.items) * 32.0F;
    const auto height = options.wrap ? static_cast<float>(options.items) * 14.0F : 32.0F;
    if (!std::isfinite(width)) {
        throw std::invalid_argument("items exceed the representable layout width");
    }

    std::vector<veyra::FlexItemStyle> styles(options.items);
    std::vector<veyra::IntrinsicSize> intrinsic(options.items);
    for (std::size_t index = 0; index < options.items; ++index) {
        styles[index].grow = 1.0F;
        intrinsic[index] = {
            .width =
                {
                    .min_content = 0.0F,
                    .preferred = 8.0F,
                    .max_content = std::numeric_limits<float>::max(),
                },
            .height = {
                .min_content = 0.0F,
                .preferred = 12.0F,
                .max_content = std::numeric_limits<float>::max(),
            },
        };
        if (options.baseline) {
            intrinsic[index].baseline = 5.0F + static_cast<float>(index % 7);
        }
    }
    std::vector<veyra::FlexItem> resolved(options.items);
    std::vector<veyra::LayoutRect> output(options.items);
    std::vector<double> timings;
    timings.reserve(options.frames);

    const auto measured_container = veyra::FlexContainer{
        .width = width,
        .height = height,
        .gap = 2.0F,
        .direction = veyra::FlexDirection::row,
        .wrap = options.wrap ? veyra::FlexWrap::wrap : veyra::FlexWrap::no_wrap,
        .align_items = options.baseline ? veyra::FlexAlign::baseline : veyra::FlexAlign::stretch,
        .justify_content = veyra::FlexJustify::start,
    };

    for (std::size_t frame = 0; frame < options.warmup; ++frame) {
        if (veyra::layout_flex_measured(measured_container, styles, intrinsic, resolved, output) !=
            veyra::FlexLayoutResult::complete) {
            throw std::runtime_error("flex layout failed during warmup");
        }
    }

    for (std::size_t frame = 0; frame < options.frames; ++frame) {
        const auto begin = Clock::now();
        const auto result =
            veyra::layout_flex_measured(measured_container, styles, intrinsic, resolved, output);
        const auto end = Clock::now();
        if (result != veyra::FlexLayoutResult::complete) {
            throw std::runtime_error("flex layout failed during measurement");
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
              << (options.baseline
                      ? (options.wrap ? "row_measured_baseline_wrap" : "row_measured_baseline")
                      : (options.wrap ? "row_measured_wrap" : "row_measured_equal_grow"))
              << ',' << VEYRA_BUILD_PLATFORM << ',' << VEYRA_BUILD_COMPILER << ','
              << VEYRA_BUILD_CONFIG << ',' << options.items << ',' << options.warmup << ','
              << options.frames << ',' << percentile(timings, 50) << ',' << percentile(timings, 95)
              << ',' << percentile(timings, 99) << ',' << timings.back() << ',' << checksum << '\n';
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
