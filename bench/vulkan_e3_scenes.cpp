#include "veyra/render/e3_scenes.hpp"
#include "veyra/render/vulkan_context.hpp"
#include "veyra/render/vulkan_rect_pipeline.hpp"

#include <algorithm>
#include <array>
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

enum class BenchMode : std::uint8_t {
    full,
    scroll,
    damage,
};

struct Options {
    bool all{};
    veyra::E3Scene scene{veyra::E3Scene::dashboard_12};
    std::size_t frames{200};
    std::size_t warmup{20};
    BenchMode mode{BenchMode::full};
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
        } else if (argument == "--mode") {
            if (value == "full") {
                options.mode = BenchMode::full;
            } else if (value == "scroll") {
                options.mode = BenchMode::scroll;
            } else if (value == "damage") {
                options.mode = BenchMode::damage;
            } else {
                throw std::invalid_argument("unknown mode: " + std::string(value));
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

void require(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

struct VariantResult {
    std::string_view name;
    std::vector<double> cpu_ns;
    std::vector<double> gpu_ns;
    std::size_t draws{};
    std::size_t submitted{};
};

const char* mode_name(BenchMode mode) noexcept {
    switch (mode) {
    case BenchMode::full:
        return "full";
    case BenchMode::scroll:
        return "scroll";
    case BenchMode::damage:
        return "damage";
    }

    return "full";
}

void render_once(
    veyra::VulkanContext& context,
    veyra::VulkanRectPipeline& pipeline,
    std::span<const veyra::DisplayCommand> commands,
    std::span<veyra::SoftwarePixel> pixels,
    const Options& options,
    veyra::VulkanRectStats& stats
) {
    if (options.mode == BenchMode::scroll) {
        const auto scrolled = veyra::render_vulkan_scrolled(
            context,
            pipeline,
            commands,
            pixels,
            stats,
            std::span<const veyra::LayoutRect>{},
            0,
            -8
        );

        require(scrolled == veyra::VulkanRenderResult::complete, "scrolled frame completes");

        return;
    }

    if (options.mode == BenchMode::damage) {
        const std::array<veyra::LayoutRect, 1> damaged{
            veyra::LayoutRect{
                .x = static_cast<float>((veyra::kE3Width - 256) / 2),
                .y = static_cast<float>((veyra::kE3Height - 144) / 2),
                .width = 256.0F,
                .height = 144.0F,
            },
        };

        const auto rendered = veyra::render_vulkan_rects(
            context,
            pipeline,
            commands,
            pixels,
            stats,
            std::span<const veyra::LayoutRect>{damaged.data(), damaged.size()}
        );

        require(rendered == veyra::VulkanRenderResult::complete, "damaged frame completes");

        return;
    }

    const auto rendered = veyra::render_vulkan_rects(context, pipeline, commands, pixels, stats);

    require(rendered == veyra::VulkanRenderResult::complete, "full frame completes");
}

void run_variant(
    veyra::E3Scene scene,
    veyra::VulkanContext& context,
    veyra::VulkanRectPipeline& pipeline,
    std::span<veyra::DisplayCommand> commands,
    std::span<veyra::SoftwarePixel> pixels,
    veyra::E3Scratch& scratch,
    const Options& options,
    VariantResult& result
) {
    result.cpu_ns.reserve(options.frames);
    result.gpu_ns.reserve(options.frames);

    for (std::size_t frame = 0; frame < options.warmup + options.frames; ++frame) {
        const auto measuring = frame >= options.warmup;

        if (options.mode == BenchMode::full || frame == 0) {
            const auto built =
                veyra::e3_build_scene(scene, 0x9E3779B97F4A7C15ULL, frame, commands, scratch);

            require(built.status == veyra::E3BuildStatus::complete, "e3 scene builds during bench");
        }

        auto stats = veyra::VulkanRectStats{};
        const auto render_begin = Clock::now();
        render_once(
            context,
            pipeline,
            std::span<const veyra::DisplayCommand>(commands),
            pixels,
            options,
            stats
        );
        const auto render_end = Clock::now();

        if (measuring) {
            result.cpu_ns.push_back(
                std::chrono::duration<double, std::nano>(render_end - render_begin).count()
            );
            result.gpu_ns.push_back(static_cast<double>(stats.gpu_ns));
            result.draws = stats.draws;
            result.submitted = stats.submitted;
        }
    }

    std::sort(result.cpu_ns.begin(), result.cpu_ns.end());
    std::sort(result.gpu_ns.begin(), result.gpu_ns.end());
}

void run_scene(
    veyra::E3Scene scene,
    veyra::VulkanContext& context,
    const Options& options,
    bool header
) {
    const auto needed = veyra::e3_max_commands(scene);
    auto commands = std::vector<veyra::DisplayCommand>(needed);
    auto pixels = std::vector<veyra::SoftwarePixel>(veyra::kE3Width * veyra::kE3Height);
    auto scratch = veyra::E3Scratch{};

    auto specialized = veyra::VulkanRectPipeline{};
    require(
        veyra::create_vulkan_rect_pipeline(context, specialized) ==
            veyra::VulkanRenderResult::complete,
        "specialized pipeline comes up for bench"
    );

    auto unified = veyra::VulkanRectPipeline{};
    require(
        veyra::create_vulkan_unified_pipeline(context, unified) ==
            veyra::VulkanRenderResult::complete,
        "unified pipeline comes up for bench"
    );

    auto specialized_result = VariantResult{.name = "specialized"};
    auto unified_result = VariantResult{.name = "unified"};

    run_variant(
        scene,
        context,
        specialized,
        std::span<veyra::DisplayCommand>(commands),
        std::span<veyra::SoftwarePixel>(pixels),
        scratch,
        options,
        specialized_result
    );
    run_variant(
        scene,
        context,
        unified,
        std::span<veyra::DisplayCommand>(commands),
        std::span<veyra::SoftwarePixel>(pixels),
        scratch,
        options,
        unified_result
    );

    veyra::destroy_vulkan_rect_pipeline(context, unified);
    veyra::destroy_vulkan_rect_pipeline(context, specialized);

    if (header) {
        std::cout << "scene,platform,compiler,build_config,variant,mode,width,height,commands,"
                     "warmup_frames,measured_frames,cpu_p50_ns,cpu_p95_ns,cpu_p99_ns,gpu_p50_ns,"
                     "gpu_p95_ns,gpu_p99_ns,draws,submitted\n";
    }

    for (const auto* result : {&specialized_result, &unified_result}) {
        std::cout << veyra::e3_scene_name(scene) << ',' << VEYRA_BUILD_PLATFORM << ','
                  << VEYRA_BUILD_COMPILER << ',' << VEYRA_BUILD_CONFIG << ',' << result->name << ','
                  << mode_name(options.mode) << ',' << veyra::kE3Width << ',' << veyra::kE3Height
                  << ',' << needed << ',' << options.warmup << ',' << options.frames << ','
                  << percentile(result->cpu_ns, 50) << ',' << percentile(result->cpu_ns, 95) << ','
                  << percentile(result->cpu_ns, 99) << ',' << percentile(result->gpu_ns, 50) << ','
                  << percentile(result->gpu_ns, 95) << ',' << percentile(result->gpu_ns, 99) << ','
                  << result->draws << ',' << result->submitted << '\n';
    }
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

    auto context = veyra::VulkanContext{};
    require(
        veyra::create_vulkan_context(context, veyra::kE3Width, veyra::kE3Height) ==
            veyra::VulkanContextResult::ready,
        "vulkan context comes up for bench"
    );

    auto first = true;
    if (options.all) {
        for (const auto scene : scenes) {
            run_scene(scene, context, options, first);
            first = false;
        }
    } else {
        run_scene(options.scene, context, options, true);
    }

    veyra::destroy_vulkan_context(context);

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
