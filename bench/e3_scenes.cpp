#include "e3_scenes_fixtures.hpp"
#include "veyra/render/common/display_batches.hpp"
#include "veyra/render/cpu/software_renderer.hpp"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;

struct Options {
    bool all{};
    veyra::E3Scene scene{veyra::E3Scene::dashboard_12};
    std::size_t frames{200};
    std::size_t warmup{20};
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
        if (argument == "--all") {
            options.all = true;
            continue;
        }
        if (index + 1 >= argc) {
            throw std::invalid_argument("missing value for " + std::string(argument));
        }
        const auto value = std::string_view(argv[++index]);
        if (argument == "--scene") {
            if (!veyra::e3_parse_scene(value, options.scene)) {
                throw std::invalid_argument("unknown scene: " + std::string(value));
            }
        } else if (argument == "--frames") {
            options.frames = parse_size(argv[index]);
        } else if (argument == "--warmup") {
            options.warmup = parse_size(argv[index]);
        } else {
            throw std::invalid_argument("unknown option: " + std::string(argument));
        }
    }
    if (options.frames == 0 || options.frames > 100000 || options.warmup > 100000) {
        throw std::invalid_argument("frame counts must be positive and within limits");
    }
    return options;
}

double percentile(const std::vector<double>& sorted_times, std::size_t percent) {
    const auto last = sorted_times.size() - 1;
    const auto index = last / 100 * percent + last % 100 * percent / 100;
    return sorted_times[index];
}

void run_scene(veyra::E3Scene scene, const Options& options, bool header) {
    const auto needed = veyra::e3_max_commands(scene);
    auto commands = std::vector<veyra::DisplayCommand>(needed);
    auto scratch = veyra::E3Scratch{};
    auto pixels = std::vector<veyra::SoftwarePixel>(veyra::kE3Width * veyra::kE3Height);
    auto clips = std::array<veyra::SoftwareClipRect, 1>{};

    const auto surface = veyra::SoftwareSurface{
        .width = veyra::kE3Width,
        .height = veyra::kE3Height,
        .stride = veyra::kE3Width,
        .pixels = pixels,
    };

    const auto workspace = veyra::SoftwareRenderWorkspace{
        .clips = clips,
    };

    auto runs = std::vector<veyra::BatchRun>(needed);
    auto scopes = std::array<std::uint64_t, 8>{};
    auto batch_count = std::size_t{};
    auto content_hash = std::uint64_t{};

    {
        const auto built = veyra::e3_build_scene(
            scene,
            0x9E3779B97F4A7C15ULL,
            0,
            std::span<veyra::DisplayCommand>(commands),
            scratch
        );
        if (built.status != veyra::E3BuildStatus::complete) {
            throw std::runtime_error("e3 scene failed to build");
        }
        if (veyra::analyze_display_batches(
                std::span<const veyra::DisplayCommand>(commands),
                runs,
                scopes,
                batch_count
            ) != veyra::BatchAnalysisResult::complete) {
            throw std::runtime_error("e3 scene failed to analyze");
        }
        content_hash = built.content_hash;
    }

    auto build_timings = std::vector<double>{};
    auto render_timings = std::vector<double>{};
    build_timings.reserve(options.frames);
    render_timings.reserve(options.frames);

    for (std::size_t frame = 0; frame < options.warmup + options.frames; ++frame) {
        const auto measuring = frame >= options.warmup;

        const auto build_begin = Clock::now();
        const auto built = veyra::e3_build_scene(
            scene,
            0x9E3779B97F4A7C15ULL,
            frame,
            std::span<veyra::DisplayCommand>(commands),
            scratch
        );
        const auto build_end = Clock::now();
        if (built.status != veyra::E3BuildStatus::complete) {
            throw std::runtime_error("e3 scene failed during measurement");
        }

        std::fill(pixels.begin(), pixels.end(), veyra::SoftwarePixel{});
        const auto render_begin = Clock::now();
        const auto rendered = veyra::render_software(
            std::span<const veyra::DisplayCommand>(commands),
            surface,
            workspace
        );
        const auto render_end = Clock::now();
        if (rendered != veyra::SoftwareRenderResult::complete) {
            throw std::runtime_error("e3 scene failed to render during measurement");
        }

        if (measuring) {
            build_timings.push_back(
                std::chrono::duration<double, std::nano>(build_end - build_begin).count()
            );
            render_timings.push_back(
                std::chrono::duration<double, std::nano>(render_end - render_begin).count()
            );
        }
    }

    std::sort(build_timings.begin(), build_timings.end());
    std::sort(render_timings.begin(), render_timings.end());

    if (header) {
        std::cout << "scene,platform,compiler,build_config,width,height,commands,warmup_frames,"
                     "measured_frames,build_p50_ns,render_p50_ns,render_p95_ns,render_p99_ns,"
                     "batches,content_hash\n";
    }
    std::cout << veyra::e3_scene_name(scene) << ',' << VEYRA_BUILD_PLATFORM << ','
              << VEYRA_BUILD_COMPILER << ',' << VEYRA_BUILD_CONFIG << ',' << veyra::kE3Width << ','
              << veyra::kE3Height << ',' << needed << ',' << options.warmup << ',' << options.frames
              << ',' << percentile(build_timings, 50) << ',' << percentile(render_timings, 50)
              << ',' << percentile(render_timings, 95) << ',' << percentile(render_timings, 99)
              << ',' << batch_count << ',' << content_hash << '\n';
}

int run(const Options& options) {
    constexpr std::array<veyra::E3Scene, 8> scenes{
        veyra::E3Scene::shadowed_boxes_600,
        veyra::E3Scene::row_scroll_1000,
        veyra::E3Scene::virtual_rows_100k,
        veyra::E3Scene::dashboard_12,
        veyra::E3Scene::changing_text,
        veyra::E3Scene::chat_cjk_arabic,
        veyra::E3Scene::translucent_overlays,
        veyra::E3Scene::explorer_1m,
    };

    auto first = true;
    if (options.all) {
        for (const auto scene : scenes) {
            run_scene(scene, options, first);
            first = false;
        }
        return 0;
    }

    run_scene(options.scene, options, true);
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
