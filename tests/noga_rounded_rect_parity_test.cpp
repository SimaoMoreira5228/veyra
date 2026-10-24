#include "allocation_counter.hpp"
#include "veyra/render/noga_frame.hpp"
#include "veyra/render/noga_rect.hpp"
#include "veyra/render/software_renderer.hpp"

#include "noga_parity_helpers.hpp"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <new>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

constexpr std::size_t kWidth = 96;
constexpr std::size_t kHeight = 72;
constexpr std::size_t kSmallWidth = 64;
constexpr std::size_t kSmallHeight = 48;
constexpr std::size_t kPixels = kWidth * kHeight;

using veyra::noga_parity::check_parity;
using veyra::noga_parity::render_reference;
using veyra::noga_parity::require;
using veyra::noga_parity::SoftwareRooms;

void check_rounded_parity(
    veyra::NogaContext& context,
    veyra::NogaFramePipeline& pipeline,
    std::span<veyra::SoftwarePixel> expected,
    std::span<veyra::SoftwarePixel> actual,
    SoftwareRooms& rooms
) {
    const std::array<veyra::DisplayCommand, 6> storage{
        veyra::FillRect{
            .bounds = {.width = static_cast<float>(kWidth), .height = static_cast<float>(kHeight)},
            .color = {.red = 240, .green = 240, .blue = 244, .alpha = 255},
        },
        veyra::FillRoundedRect{
            .bounds = {.x = 8.0F, .y = 10.0F, .width = 40.0F, .height = 30.0F},
            .radius = 8.0F,
            .color = {.red = 30, .green = 140, .blue = 60, .alpha = 255},
        },
        veyra::FillRect{
            .bounds = {.x = 24.0F, .y = 20.0F, .width = 50.0F, .height = 24.0F},
            .color = {.red = 210, .green = 60, .blue = 40, .alpha = 160},
        },
        veyra::FillRoundedRect{
            .bounds = {.x = 40.0F, .y = 32.0F, .width = 44.0F, .height = 28.0F},
            .radius = 14.0F,
            .color = {.red = 40, .green = 70, .blue = 210, .alpha = 128},
        },
        veyra::FillRoundedRect{
            .bounds = {.x = 4.0F, .y = 50.0F, .width = 60.0F, .height = 16.0F},
            .radius = 0.0F,
            .color = {.red = 230, .green = 180, .blue = 20, .alpha = 255},
        },
        veyra::FillRoundedRect{
            .bounds = {.x = 60.0F, .y = 4.0F, .width = 30.0F, .height = 20.0F},
            .radius = 40.0F,
            .color = {.red = 150, .green = 40, .blue = 170, .alpha = 255},
        },
    };

    const auto scene = std::span<const veyra::DisplayCommand>{storage.data(), storage.size()};

    render_reference(scene, expected, kWidth, kHeight, rooms);

    auto stats = veyra::NogaFrameStats{};

    require(
        veyra::render_noga_frame(context, pipeline, scene, actual, stats) ==
            veyra::NogaRenderResult::complete,
        "mixed rect and rounded scene completes"
    );
    require(stats.submitted == 6, "every rect submits");
    require(stats.rect_draws >= 1, "rects draw through the rect pipeline");
    require(stats.rounded_draws >= 1, "rounded rects draw through the rounded pipeline");

    check_parity(context, pipeline, scene, expected, actual, "rounded", 1);
}

void check_stroke_bars(
    veyra::NogaContext& context,
    std::span<veyra::SoftwarePixel> expected,
    std::span<veyra::SoftwarePixel> actual,
    SoftwareRooms& rooms
) {
    auto bars = veyra::NogaFramePipeline{};

    require(
        veyra::create_noga_frame_pipeline(context, bars) == veyra::NogaRenderResult::complete,
        "frame pipeline comes up for strokes"
    );

    const std::array<veyra::DisplayCommand, 2> storage{
        veyra::FillRect{
            .bounds = {.x = 8.0F, .y = 8.0F, .width = 32.0F, .height = 24.0F},
            .color = {.red = 30, .green = 140, .blue = 60, .alpha = 255},
        },
        veyra::StrokeRect{
            .bounds = {.x = 8.0F, .y = 40.0F, .width = 32.0F, .height = 16.0F},
            .thickness = 2.0F,
            .color = {.red = 210, .green = 60, .blue = 40, .alpha = 255},
        },
    };

    const auto scene = std::span<const veyra::DisplayCommand>{storage.data(), storage.size()};

    render_reference(scene, expected, kWidth, kHeight, rooms);

    auto stats = veyra::NogaFrameStats{};

    require(
        veyra::render_noga_frame(context, bars, scene, actual, stats) ==
            veyra::NogaRenderResult::complete,
        "strokes complete through the frame pipeline"
    );
    require(stats.rect_draws >= 1, "lowered stroke bars batch through the rect pipe");
    require(stats.rounded_draws == 0, "plain fills never touch the rounded pipe");

    check_parity(context, bars, scene, expected, actual, "stroke-bars", 1);

    veyra::destroy_noga_frame_pipeline(context, bars);
}

void check_integer_rect_parity(
    veyra::NogaContext& context,
    std::span<veyra::SoftwarePixel> expected,
    std::span<veyra::SoftwarePixel> actual,
    SoftwareRooms& rooms
) {
    auto integer = veyra::NogaFramePipeline{};

    require(
        veyra::create_noga_frame_pipeline(context, integer) == veyra::NogaRenderResult::complete,
        "frame pipeline comes up for plain rects"
    );

    const std::array<veyra::DisplayCommand, 3> storage{
        veyra::FillRect{
            .bounds = {.width = static_cast<float>(kWidth), .height = static_cast<float>(kHeight)},
            .color = {.red = 240, .green = 240, .blue = 244, .alpha = 255},
        },
        veyra::FillRect{
            .bounds = {.x = 10.0F, .y = 12.0F, .width = 40.0F, .height = 30.0F},
            .color = {.red = 30, .green = 140, .blue = 60, .alpha = 200},
        },
        veyra::FillRect{
            .bounds = {.x = -4.0F, .y = 50.0F, .width = 40.0F, .height = 30.0F},
            .color = {.red = 210, .green = 60, .blue = 40, .alpha = 255},
        },
    };

    const auto scene = std::span<const veyra::DisplayCommand>{storage.data(), storage.size()};

    render_reference(scene, expected, kWidth, kHeight, rooms);

    auto stats = veyra::NogaFrameStats{};

    require(
        veyra::render_noga_frame(context, integer, scene, actual, stats) ==
            veyra::NogaRenderResult::complete,
        "integer rects complete through the frame pipeline"
    );
    require(stats.rect_draws >= 1, "plain rects batch through the rect pipe");

    check_parity(context, integer, scene, expected, actual, "integer", 1);

    veyra::destroy_noga_frame_pipeline(context, integer);
}

void check_clipped_rounded_parity(
    veyra::NogaContext& context,
    veyra::NogaFramePipeline& pipeline,
    std::span<veyra::SoftwarePixel> expected,
    std::span<veyra::SoftwarePixel> actual,
    SoftwareRooms& rooms
) {
    const std::array<veyra::DisplayCommand, 4> storage{
        veyra::FillRect{
            .bounds = {.width = static_cast<float>(kWidth), .height = static_cast<float>(kHeight)},
            .color = {.red = 240, .green = 240, .blue = 244, .alpha = 255},
        },
        veyra::FillRoundedRect{
            .bounds = {.x = 10.5F, .y = 12.0F, .width = 40.0F, .height = 30.0F},
            .radius = 8.0F,
            .color = {.red = 30, .green = 140, .blue = 60, .alpha = 255},
        },
        veyra::FillRoundedRect{
            .bounds = {.x = -8.0F, .y = 56.0F, .width = 40.0F, .height = 30.0F},
            .radius = 10.0F,
            .color = {.red = 210, .green = 60, .blue = 40, .alpha = 200},
        },
        veyra::FillRoundedRect{
            .bounds = {.x = 70.0F, .y = 60.0F, .width = 40.0F, .height = 30.0F},
            .radius = 10.0F,
            .color = {.red = 40, .green = 70, .blue = 210, .alpha = 200},
        },
    };

    const auto scene = std::span<const veyra::DisplayCommand>{storage.data(), storage.size()};

    render_reference(scene, expected, kWidth, kHeight, rooms);

    auto stats = veyra::NogaFrameStats{};

    require(
        veyra::render_noga_frame(context, pipeline, scene, actual, stats) ==
            veyra::NogaRenderResult::complete,
        "fractional and clipped rounded rects complete"
    );

    check_parity(context, pipeline, scene, expected, actual, "rounded-clipped", 2);
}

int run() {
    auto context = veyra::NogaContext{};

    const auto context_result = veyra::create_noga_context(context, kWidth, kHeight);

    require(context_result == veyra::NogaContextResult::ready, "noga context comes up");

    auto pipeline = veyra::NogaFramePipeline{};

    const auto pipeline_result = veyra::create_noga_frame_pipeline(context, pipeline);

    require(pipeline_result == veyra::NogaRenderResult::complete, "rect pipeline comes up");

    auto rooms = SoftwareRooms{};
    auto expected = std::array<veyra::SoftwarePixel, kPixels>{};
    auto actual = std::array<veyra::SoftwarePixel, kPixels>{};

    check_rounded_parity(
        context,
        pipeline,
        std::span<veyra::SoftwarePixel>{expected.data(), kWidth * kHeight},
        std::span<veyra::SoftwarePixel>{actual.data(), kWidth * kHeight},
        rooms
    );

    check_clipped_rounded_parity(
        context,
        pipeline,
        std::span<veyra::SoftwarePixel>{expected.data(), kWidth * kHeight},
        std::span<veyra::SoftwarePixel>{actual.data(), kWidth * kHeight},
        rooms
    );

    check_integer_rect_parity(
        context,
        std::span<veyra::SoftwarePixel>{expected.data(), kWidth * kHeight},
        std::span<veyra::SoftwarePixel>{actual.data(), kWidth * kHeight},
        rooms
    );

    check_stroke_bars(
        context,
        std::span<veyra::SoftwarePixel>{expected.data(), kWidth * kHeight},
        std::span<veyra::SoftwarePixel>{actual.data(), kWidth * kHeight},
        rooms
    );

    veyra::destroy_noga_frame_pipeline(context, pipeline);
    veyra::destroy_noga_context(context);

    require(!veyra::noga_context_ready(context), "context tears down");

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
