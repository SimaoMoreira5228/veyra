#include "allocation_counter.hpp"
#include "veyra/render/noga_frame.hpp"
#include "veyra/render/noga_rect.hpp"
#include "veyra/render/software_renderer.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

void require(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

constexpr std::size_t kWidth = 64;
constexpr std::size_t kHeight = 48;
constexpr std::size_t kPixels = kWidth * kHeight;
constexpr std::size_t kTile = 8;

std::span<const veyra::DisplayCommand>
build_scene(std::array<veyra::DisplayCommand, 2>& storage) noexcept {
    storage[0] = veyra::FillRect{
        .bounds = {.width = static_cast<float>(kWidth), .height = static_cast<float>(kHeight)},
        .color = {.red = 12, .green = 20, .blue = 28, .alpha = 255},
    };
    storage[1] = veyra::FillRect{
        .bounds = {.x = 8.0F, .y = 8.0F, .width = 32.0F, .height = 20.0F},
        .color = {.red = 200, .green = 40, .blue = 30, .alpha = 128},
    };

    return std::span<const veyra::DisplayCommand>{storage.data(), storage.size()};
}

void check_healthy_reports_timing(
    veyra::NogaContext& context,
    veyra::NogaFramePipeline& pipeline,
    std::span<veyra::SoftwarePixel> pixels
) {
    require(!veyra::noga_context_device_lost(context), "fresh device is not lost");
    require(veyra::noga_context_generation(context) == 1, "first live generation is one");

    std::array<veyra::DisplayCommand, 2> storage{};
    auto stats = veyra::NogaFrameStats{};

    require(
        veyra::render_noga_frame(context, pipeline, build_scene(storage), pixels, stats) ==
            veyra::NogaRenderResult::complete,
        "healthy device renders"
    );

    std::cout << "noga timestamp_period_ns=" << context.info.timestamp_period_ns << '\n';
    std::cout << "noga frame gpu_timed=" << (stats.gpu_timed ? "yes" : "no");
    std::cout << " gpu_ns=" << stats.gpu_ns << '\n';

    if (context.info.timestamp_period_ns > 0.0F) {
        require(stats.gpu_timed, "supported device times the frame");
        require(stats.gpu_ns > 0, "non-empty frame takes measurable GPU time");
    } else {
        require(!stats.gpu_timed, "unsupported device leaves the frame untimed");
        require(stats.gpu_ns == 0, "unsupported device reports zero GPU time");
    }

    require(!veyra::noga_context_device_lost(context), "healthy render leaves device healthy");
}

void check_unchanged_frames_stay_untimed(
    veyra::NogaContext& context,
    veyra::NogaFramePipeline& pipeline,
    std::span<veyra::SoftwarePixel> pixels
) {
    std::array<veyra::DisplayCommand, 2> storage{};
    auto stats = veyra::NogaFrameStats{};

    require(
        veyra::render_noga_frame_damaged(
            context,
            pipeline,
            build_scene(storage),
            std::span<const veyra::LayoutRect>{},
            pixels,
            stats
        ) == veyra::NogaRenderResult::unchanged,
        "empty damage skips the frame"
    );
    require(!stats.gpu_timed, "skipped frame issues no GPU work");
    require(stats.gpu_ns == 0, "skipped frame reports zero GPU time");

    const std::array<veyra::LayoutRect, 1> outside{
        veyra::LayoutRect{
            .x = static_cast<float>(kWidth) + 4.0F,
            .width = 8.0F,
            .height = 8.0F,
        },
    };

    require(
        veyra::render_noga_frame_damaged(
            context,
            pipeline,
            build_scene(storage),
            std::span<const veyra::LayoutRect>{outside.data(), outside.size()},
            pixels,
            stats
        ) == veyra::NogaRenderResult::unchanged,
        "fully outside damage skips the frame"
    );
    require(!stats.gpu_timed, "outside damage issues no GPU work");
    require(stats.gpu_ns == 0, "outside damage reports zero GPU time");
    require(!veyra::noga_context_device_lost(context), "skipped frames are not device loss");
}

void check_empty_submit_stays_timed(
    veyra::NogaContext& context,
    veyra::NogaFramePipeline& pipeline,
    std::span<veyra::SoftwarePixel> pixels
) {
    const std::array<veyra::DisplayCommand, 1> skipped{
        veyra::FillRect{
            .bounds = {.x = 4.0F, .y = 4.0F, .width = 16.0F, .height = 12.0F},
            .color = {.red = 200, .green = 40, .blue = 30, .alpha = 0},
        },
    };

    auto stats = veyra::NogaFrameStats{};

    require(
        veyra::render_noga_frame(
            context,
            pipeline,
            std::span<const veyra::DisplayCommand>{skipped.data(), skipped.size()},
            pixels,
            stats
        ) == veyra::NogaRenderResult::complete,
        "drawless frame still submits"
    );
    require(
        stats.gpu_timed == (context.info.timestamp_period_ns > 0.0F),
        "drawless frame follows device timestamp support"
    );

    std::cout << "noga drawless gpu_ns=" << stats.gpu_ns << '\n';
    require(!veyra::noga_context_device_lost(context), "drawless frame is not device loss");
}

void check_recovery_round_trip(
    veyra::NogaContext& context,
    veyra::NogaFramePipeline& pipeline,
    std::span<veyra::SoftwarePixel> pixels
) {
    auto tile = std::array<veyra::SoftwarePixel, kTile * kTile>{};

    for (auto y = std::size_t{}; y < kTile; ++y) {
        for (auto x = std::size_t{}; x < kTile; ++x) {
            tile[y * kTile + x] = veyra::SoftwarePixel{
                .red = static_cast<std::uint8_t>(x * 32),
                .green = static_cast<std::uint8_t>(y * 32),
                .blue = 140,
                .alpha = 255,
            };
        }
    }

    const auto image = veyra::SoftwareImage{
        .width = kTile,
        .height = kTile,
        .stride = kTile,
        .pixels = std::span<const veyra::SoftwarePixel>{tile.data(), tile.size()},
        .generation = 7,
    };

    const std::array<veyra::DisplayCommand, 1> storage{
        veyra::DrawImage{
            .source = {.width = static_cast<float>(kTile), .height = static_cast<float>(kTile)},
            .destination =
                {.width = static_cast<float>(kWidth), .height = static_cast<float>(kHeight)},
            .image = &image,
            .sampling = veyra::SoftwareImageSampling::nearest,
            .opacity = 255,
        },
    };

    const auto commands = std::span<const veyra::DisplayCommand>{storage.data(), storage.size()};
    auto stats = veyra::NogaFrameStats{};

    require(
        veyra::render_noga_frame(context, pipeline, commands, pixels, stats) ==
            veyra::NogaRenderResult::complete,
        "image frame renders"
    );
    require(stats.uploads == 1, "first image frame uploads once");

    require(
        veyra::render_noga_frame(context, pipeline, commands, pixels, stats) ==
            veyra::NogaRenderResult::complete,
        "resident image frame renders"
    );
    require(stats.uploads == 0, "resident image frame uploads nothing");

    auto before = std::array<veyra::SoftwarePixel, kPixels>{};

    for (auto i = std::size_t{}; i < kPixels; ++i) {
        before[i] = pixels[i];
    }

    const auto generation = veyra::noga_context_generation(context);

    require(
        veyra::recover_noga_context(context, pipeline) == veyra::NogaContextResult::ready,
        "healthy context recovers"
    );
    require(
        veyra::noga_context_generation(context) == generation + 1,
        "recovery bumps the generation once"
    );
    require(!veyra::noga_context_device_lost(context), "recovered device is not lost");

    require(
        veyra::render_noga_frame(context, pipeline, commands, pixels, stats) ==
            veyra::NogaRenderResult::complete,
        "post-recovery frame renders"
    );
    require(stats.uploads == 1, "recovery drops caches so textures re-upload exactly once");

    for (auto i = std::size_t{}; i < kPixels; ++i) {
        require(
            pixels[i].red == before[i].red && pixels[i].green == before[i].green &&
                pixels[i].blue == before[i].blue && pixels[i].alpha == before[i].alpha,
            "pixels match across recovery"
        );
    }

    require(
        veyra::render_noga_frame(context, pipeline, commands, pixels, stats) ==
            veyra::NogaRenderResult::complete,
        "second post-recovery frame renders"
    );
    require(stats.uploads == 0, "re-uploaded textures stay resident");
}

void check_invalid_input_is_not_loss(
    veyra::NogaContext& context,
    veyra::NogaFramePipeline& pipeline,
    std::span<veyra::SoftwarePixel> pixels
) {
    const std::array<veyra::DisplayCommand, 1> broken{
        veyra::FillRect{
            .bounds = {.width = -1.0F, .height = 1.0F},
            .color = {.red = 200, .green = 40, .blue = 40, .alpha = 255},
        },
    };

    auto stats = veyra::NogaFrameStats{};

    require(
        veyra::render_noga_frame(
            context,
            pipeline,
            std::span<const veyra::DisplayCommand>{broken.data(), broken.size()},
            pixels,
            stats
        ) == veyra::NogaRenderResult::invalid_input,
        "invalid input still reports invalid input"
    );
    require(
        !veyra::noga_context_device_lost(context),
        "rejected input does not mark the device lost"
    );
}

void check_steady_timed_allocates_nothing(
    veyra::NogaContext& context,
    veyra::NogaFramePipeline& pipeline,
    std::span<veyra::SoftwarePixel> pixels
) {
    std::array<veyra::DisplayCommand, 2> storage{};
    auto stats = veyra::NogaFrameStats{};

    require(
        veyra::render_noga_frame(context, pipeline, build_scene(storage), pixels, stats) ==
            veyra::NogaRenderResult::complete,
        "steady timed window opens clean"
    );

    const auto supported = context.info.timestamp_period_ns > 0.0F;

    g_allocation_count.store(0, std::memory_order_relaxed);
    g_counting.store(true, std::memory_order_relaxed);

    for (auto repeat = std::size_t{}; repeat < 60; ++repeat) {
        const auto result =
            veyra::render_noga_frame(context, pipeline, build_scene(storage), pixels, stats);

        if (result != veyra::NogaRenderResult::complete || (supported && !stats.gpu_timed)) {
            g_counting.store(false, std::memory_order_relaxed);

            throw std::runtime_error("steady timed repeats stay complete and timed");
        }
    }

    g_counting.store(false, std::memory_order_relaxed);

    std::cout << "noga timed repeats=60 host_allocations="
              << g_allocation_count.load(std::memory_order_relaxed) << '\n';

    require(
        g_allocation_count.load(std::memory_order_relaxed) == 0,
        "timed frames allocate nothing"
    );
}

void check_torn_down_rejects() {
    auto context = veyra::NogaContext{};
    auto pipeline = veyra::NogaFramePipeline{};
    auto pixels = std::array<veyra::SoftwarePixel, kPixels>{};
    auto stats = veyra::NogaFrameStats{};

    const std::array<veyra::DisplayCommand, 1> storage{
        veyra::FillRect{
            .bounds = {.width = static_cast<float>(kWidth), .height = static_cast<float>(kHeight)},
            .color = {.red = 12, .green = 20, .blue = 28, .alpha = 255},
        },
    };

    const auto commands = std::span<const veyra::DisplayCommand>{storage.data(), storage.size()};

    require(
        veyra::render_noga_frame(
            context,
            pipeline,
            commands,
            std::span<veyra::SoftwarePixel>{pixels.data(), pixels.size()},
            stats
        ) == veyra::NogaRenderResult::invalid_input,
        "torn-down context rejects frames"
    );
    require(
        veyra::render_noga_frame_damaged(
            context,
            pipeline,
            commands,
            std::span<const veyra::LayoutRect>{},
            std::span<veyra::SoftwarePixel>{pixels.data(), pixels.size()},
            stats
        ) == veyra::NogaRenderResult::invalid_input,
        "torn-down context rejects damaged frames"
    );
    require(!veyra::noga_context_device_lost(context), "torn-down context reads as not lost");
    require(veyra::noga_context_generation(context) == 0, "torn-down context has no generation");
    require(
        veyra::recover_noga_context(context, pipeline) == veyra::NogaContextResult::invalid_input,
        "torn-down context refuses recovery"
    );
}

int run() {
    auto context = veyra::NogaContext{};

    require(
        veyra::create_noga_context(context, kWidth, kHeight) == veyra::NogaContextResult::ready,
        "noga context comes up"
    );

    auto pipeline = veyra::NogaFramePipeline{};

    require(
        veyra::create_noga_frame_pipeline(context, pipeline) == veyra::NogaRenderResult::complete,
        "frame pipeline comes up"
    );

    auto pixels = std::array<veyra::SoftwarePixel, kPixels>{};
    const auto output = std::span<veyra::SoftwarePixel>{pixels.data(), pixels.size()};

    check_healthy_reports_timing(context, pipeline, output);
    check_unchanged_frames_stay_untimed(context, pipeline, output);
    check_empty_submit_stays_timed(context, pipeline, output);
    check_recovery_round_trip(context, pipeline, output);
    check_invalid_input_is_not_loss(context, pipeline, output);
    check_steady_timed_allocates_nothing(context, pipeline, output);

    veyra::destroy_noga_frame_pipeline(context, pipeline);
    veyra::destroy_noga_context(context);

    require(!veyra::noga_context_ready(context), "context tears down");

    check_torn_down_rejects();

    return 0;
}

}

int main() {
    try {
        return run();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
