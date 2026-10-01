#include "veyra/engine.hpp"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <random>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;

struct Options {
    std::size_t items{100'000};
    std::size_t frames{400};
    std::size_t warmup{100};
    std::size_t updates{200};
    std::size_t visible{60};
    std::uint32_t seed{42};
    std::string paradigm{"all"};
};

struct FrameTiming {
    double update_us;
    double frame_us;
    double total_us;
};

struct TimingSummary {
    double p50;
    double p95;
    double p99;
    double max;
};

std::size_t parse_size(const char* value) {
    const std::string_view text(value);
    std::size_t parsed = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), parsed);
    if (error != std::errc{} || end != text.data() + text.size()) {
        throw std::invalid_argument("invalid non-negative integer: " + std::string(text));
    }
    return parsed;
}

Options parse_options(int argc, char** argv) {
    Options options;
    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index];
        if (index + 1 >= argc) {
            throw std::invalid_argument("missing value for " + argument);
        }

        const char* value = argv[++index];
        if (argument == "--paradigm") {
            options.paradigm = value;
        } else if (argument == "--items") {
            options.items = parse_size(value);
        } else if (argument == "--frames") {
            options.frames = parse_size(value);
        } else if (argument == "--warmup") {
            options.warmup = parse_size(value);
        } else if (argument == "--updates") {
            options.updates = parse_size(value);
        } else if (argument == "--visible") {
            options.visible = parse_size(value);
        } else if (argument == "--seed") {
            const auto seed = parse_size(value);
            if (seed > UINT32_MAX) {
                throw std::invalid_argument("seed must fit in a 32-bit unsigned integer");
            }
            options.seed = static_cast<std::uint32_t>(seed);
        } else {
            throw std::invalid_argument("unknown option: " + argument);
        }
    }

    if (options.items == 0 || options.frames == 0) {
        throw std::invalid_argument("items and frames must be positive");
    }
    if (options.items > UINT32_MAX) {
        throw std::invalid_argument("items must fit in a 32-bit update id");
    }
    if (options.frames > UINT32_MAX || options.warmup > UINT32_MAX - options.frames) {
        throw std::invalid_argument("warmup and frames must fit in a 32-bit update value");
    }

    return options;
}

std::vector<veyra::Update> make_updates(const Options& options) {
    const auto total_frames = options.warmup + options.frames;
    if (options.updates != 0 && total_frames > SIZE_MAX / options.updates) {
        throw std::length_error("update stream is too large");
    }

    std::vector<veyra::Update> updates(total_frames * options.updates);
    std::mt19937 generator(options.seed);
    for (std::size_t frame = 0; frame < total_frames; ++frame) {
        const auto offset = frame * options.updates;
        for (std::size_t index = 0; index < options.updates; ++index) {
            updates[offset + index] = {
                static_cast<std::uint32_t>(generator() % options.items),
                static_cast<std::uint32_t>(frame + 1),
            };
        }
    }
    return updates;
}

double percentile(
    const std::vector<FrameTiming>& sorted_times,
    std::size_t percent,
    double FrameTiming::* metric
) {
    const auto last = sorted_times.size() - 1;
    const auto index = last / 100 * percent + last % 100 * percent / 100;
    return sorted_times[index].*metric;
}

TimingSummary summarize(std::vector<FrameTiming>& timings, double FrameTiming::* metric) {
    std::sort(timings.begin(), timings.end(), [metric](const auto& left, const auto& right) {
        return left.*metric < right.*metric;
    });

    return {
        percentile(timings, 50, metric),
        percentile(timings, 95, metric),
        percentile(timings, 99, metric),
        timings.back().*metric,
    };
}

void run_engine(
    std::string_view name,
    const Options& options,
    const std::vector<veyra::Update>& updates
) {
    auto engine = veyra::create_engine(name);
    engine->reset(options.items);

    std::vector<veyra::Rect> draw;
    draw.reserve(options.visible);
    std::vector<FrameTiming> timings;
    timings.reserve(options.frames);
    std::uint64_t invalidations = 0;
    std::uint64_t rectangles = 0;
    const auto first = options.items > options.visible ? options.items - options.visible : 0;

    for (std::size_t frame = 0; frame < options.warmup; ++frame) {
        const auto offset = frame * options.updates;
        const auto batch = std::span<const veyra::Update>(updates).subspan(offset, options.updates);

        engine->apply(batch);
        engine->frame(first, options.visible, draw);
    }

    for (std::size_t frame = 0; frame < options.frames; ++frame) {
        const auto stream_frame = options.warmup + frame;
        const auto offset = stream_frame * options.updates;
        const auto batch = std::span<const veyra::Update>(updates).subspan(offset, options.updates);

        const auto update_begin = Clock::now();
        engine->apply(batch);
        const auto update_end = Clock::now();
        const auto frame_begin = Clock::now();
        const auto stats = engine->frame(first, options.visible, draw);
        const auto frame_end = Clock::now();

        timings.push_back({
            std::chrono::duration<double, std::micro>(update_end - update_begin).count(),
            std::chrono::duration<double, std::micro>(frame_end - frame_begin).count(),
            std::chrono::duration<double, std::micro>(frame_end - update_begin).count(),
        });
        invalidations += stats.invalidated;
        rectangles += stats.emitted;
    }

    const auto update_summary = summarize(timings, &FrameTiming::update_us);
    const auto frame_summary = summarize(timings, &FrameTiming::frame_us);
    const auto total_summary = summarize(timings, &FrameTiming::total_us);

    std::cout << "random_updates_visible_suffix," << name << ',' << VEYRA_BUILD_PLATFORM << ','
              << VEYRA_BUILD_COMPILER << ',' << VEYRA_BUILD_CONFIG << ','
              << std::thread::hardware_concurrency() << ',' << options.seed << ',' << options.items
              << ',' << options.warmup << ',' << options.frames << ',' << options.updates << ','
              << options.visible << ',' << std::fixed << std::setprecision(3) << update_summary.p50
              << ',' << update_summary.p95 << ',' << update_summary.p99 << ',' << update_summary.max
              << ',' << frame_summary.p50 << ',' << frame_summary.p95 << ',' << frame_summary.p99
              << ',' << frame_summary.max << ',' << total_summary.p50 << ',' << total_summary.p95
              << ',' << total_summary.p99 << ',' << total_summary.max << ',' << engine->checksum()
              << ',' << invalidations << ',' << rectangles << '\n';
}

}

int main(int argc, char** argv) {
    try {
        const auto options = parse_options(argc, argv);
        const auto updates = make_updates(options);

        std::cout << "scenario,engine,platform,compiler,build_config,hardware_threads,seed,items,"
                     "warmup_frames,measured_frames,updates_per_frame,visible,update_p50_us,"
                     "update_p95_us,update_p99_us,update_max_us,frame_p50_us,frame_p95_us,"
                     "frame_p99_us,frame_max_us,total_p50_us,total_p95_us,total_p99_us,"
                     "total_max_us,checksum,invalidations,rects\n";
        if (options.paradigm == "all") {
            for (const auto name : veyra::available_engines()) {
                run_engine(name, options, updates);
            }
        } else {
            run_engine(options.paradigm, options, updates);
        }
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 2;
    }
}
