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
#include <limits>
#include <new>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

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

void check_rejections(
    veyra::NogaContext& context,
    veyra::NogaFramePipeline& pipeline,
    std::span<veyra::SoftwarePixel> scratch
) {
    const std::array<veyra::DisplayCommand, 2> translucent{
        veyra::FillRect{
            .bounds = {.width = 8.0F, .height = 8.0F},
            .color = {.red = 255, .alpha = 128},
        },
        veyra::FillRect{
            .bounds = {.x = 8.0F, .width = 8.0F, .height = 8.0F},
            .color = {.red = 255, .alpha = 255},
        },
    };

    auto stats = veyra::NogaFrameStats{};

    const auto translucent_result =
        veyra::render_noga_frame(context, pipeline, translucent, scratch, stats);

    require(translucent_result == veyra::NogaRenderResult::complete, "translucent frame completes");
    require(stats.submitted == 2, "translucent rects submit instead of skipping");
    require(stats.draws == 1, "one draw covers the pair");

    const std::array<veyra::DisplayCommand, 1> stroked{
        veyra::StrokeRect{
            .bounds = {.width = 8.0F, .height = 8.0F},
            .thickness = 1.0F,
            .color = {.red = 255, .alpha = 255},
        },
    };

    const auto stroke_result = veyra::render_noga_frame(context, pipeline, stroked, scratch, stats);

    require(
        stroke_result == veyra::NogaRenderResult::complete,
        "rect strokes lower to fills instead of rejecting"
    );
    require(stats.submitted == 4, "an 8x8 outline lowers to four bars");

    const std::array<veyra::DisplayCommand, 1> popped{
        veyra::PopClip{},
    };

    const auto pop_result = veyra::render_noga_frame(context, pipeline, popped, scratch, stats);

    require(
        pop_result == veyra::NogaRenderResult::invalid_input,
        "popping an empty clip stack is rejected"
    );

    const std::array<veyra::DisplayCommand, 1> unclosed{
        veyra::PushClip{.bounds = {.width = 8.0F, .height = 8.0F}, .clip_id = 7},
    };

    const auto unclosed_result =
        veyra::render_noga_frame(context, pipeline, unclosed, scratch, stats);

    require(
        unclosed_result == veyra::NogaRenderResult::invalid_input,
        "leaving a clip open is rejected"
    );
}

void check_stroke_parity(
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
        veyra::StrokeRect{
            .bounds = {.x = 8.0F, .y = 10.0F, .width = 40.0F, .height = 24.0F},
            .thickness = 3.0F,
            .color = {.red = 30, .green = 140, .blue = 60, .alpha = 255},
        },
        veyra::StrokeRect{
            .bounds = {.x = 20.0F, .y = 20.0F, .width = 50.0F, .height = 24.0F},
            .thickness = 2.0F,
            .color = {.red = 210, .green = 60, .blue = 40, .alpha = 160},
        },
        veyra::StrokeRect{
            .bounds = {.x = -4.0F, .y = 44.0F, .width = 24.0F, .height = 20.0F},
            .thickness = 10.0F,
            .color = {.red = 40, .green = 70, .blue = 210, .alpha = 255},
        },
        veyra::StrokeRect{
            .bounds = {.x = 60.0F, .y = 8.0F, .width = 28.0F, .height = 16.0F},
            .thickness = 20.0F,
            .color = {.red = 150, .green = 40, .blue = 170, .alpha = 255},
        },
        veyra::StrokeRect{
            .bounds = {.x = 50.0F, .y = 50.0F, .width = 12.0F, .height = 12.0F},
            .thickness = 0.0F,
            .color = {.red = 150, .green = 40, .blue = 170, .alpha = 255},
        },
    };

    const auto scene = std::span<const veyra::DisplayCommand>{storage.data(), storage.size()};

    render_reference(scene, expected, kWidth, kHeight, rooms);

    auto stats = veyra::NogaFrameStats{};

    require(
        veyra::render_noga_frame(context, pipeline, scene, actual, stats) ==
            veyra::NogaRenderResult::complete,
        "stroked rects lower to fills"
    );

    check_parity(context, pipeline, scene, expected, actual, "stroke", 1);

    const std::array<veyra::DisplayCommand, 1> negative{
        veyra::StrokeRect{
            .bounds = {.width = 8.0F, .height = 8.0F},
            .thickness = -1.0F,
            .color = {.red = 255, .alpha = 255},
        },
    };

    require(
        veyra::render_noga_frame(
            context,
            pipeline,
            std::span<const veyra::DisplayCommand>(negative),
            actual,
            stats
        ) == veyra::NogaRenderResult::invalid_input,
        "negative thickness is rejected"
    );
}

void check_stroke_edges(
    veyra::NogaContext& context,
    veyra::NogaFramePipeline& pipeline,
    std::span<veyra::SoftwarePixel> expected,
    std::span<veyra::SoftwarePixel> actual,
    SoftwareRooms& rooms
) {
    const auto nan = std::numeric_limits<float>::quiet_NaN();
    const auto inf = std::numeric_limits<float>::infinity();

    const std::array<veyra::DisplayCommand, 8> storage{
        veyra::FillRect{
            .bounds = {.width = static_cast<float>(kWidth), .height = static_cast<float>(kHeight)},
            .color = {.red = 240, .green = 240, .blue = 244, .alpha = 255},
        },
        veyra::StrokeRect{
            .bounds = {.x = 10.5F, .y = 10.0F, .width = 30.0F, .height = 20.0F},
            .thickness = 3.0F,
            .color = {.red = 30, .green = 140, .blue = 60, .alpha = 255},
        },
        veyra::PushTranslation{.offset = {.x = 4.25F, .y = -3.5F}},
        veyra::StrokeRect{
            .bounds = {.x = 40.0F, .y = 30.0F, .width = 24.0F, .height = 16.0F},
            .thickness = 2.0F,
            .color = {.red = 210, .green = 60, .blue = 40, .alpha = 200},
        },
        veyra::PopTranslation{},
        veyra::StrokeRect{
            .bounds = {.x = 60.0F, .y = 50.0F, .width = 12.0F, .height = 12.0F},
            .thickness = 1.0F,
            .color = {.red = 150, .green = 40, .blue = 170, .alpha = 0},
        },
        veyra::StrokeRect{
            .bounds = {.width = 0.0F, .height = 8.0F},
            .thickness = 1.0F,
            .color = {.red = 150, .green = 40, .blue = 170, .alpha = 255},
        },
        veyra::StrokeRect{
            .bounds = {.x = 70.0F, .y = 50.0F, .width = 0.0F, .height = 0.0F},
            .thickness = 0.0F,
            .color = {.red = 150, .green = 40, .blue = 170, .alpha = 255},
        },
    };

    const auto scene = std::span<const veyra::DisplayCommand>{storage.data(), storage.size()};

    render_reference(scene, expected, kWidth, kHeight, rooms);

    auto stats = veyra::NogaFrameStats{};

    require(
        veyra::render_noga_frame(context, pipeline, scene, actual, stats) ==
            veyra::NogaRenderResult::complete,
        "fractional, translated, and empty strokes complete"
    );

    check_parity(context, pipeline, scene, expected, actual, "stroke-edge", 1);

    const std::array<veyra::DisplayCommand, 1> negative{
        veyra::StrokeRect{
            .bounds = {.width = 8.0F, .height = 8.0F},
            .thickness = -1.0F,
            .color = {.red = 255, .alpha = 255},
        },
    };

    require(
        veyra::render_noga_frame(
            context,
            pipeline,
            std::span<const veyra::DisplayCommand>(negative),
            actual,
            stats
        ) == veyra::NogaRenderResult::invalid_input,
        "negative thickness is rejected"
    );

    const std::array<veyra::DisplayCommand, 1> not_a_number{
        veyra::StrokeRect{
            .bounds = {.width = 8.0F, .height = 8.0F},
            .thickness = nan,
            .color = {.red = 255, .alpha = 255},
        },
    };

    require(
        veyra::render_noga_frame(
            context,
            pipeline,
            std::span<const veyra::DisplayCommand>(not_a_number),
            actual,
            stats
        ) == veyra::NogaRenderResult::invalid_input,
        "NaN thickness is rejected"
    );

    const std::array<veyra::DisplayCommand, 1> infinite{
        veyra::StrokeRect{
            .bounds = {.width = 8.0F, .height = 8.0F},
            .thickness = inf,
            .color = {.red = 255, .alpha = 255},
        },
    };

    require(
        veyra::render_noga_frame(
            context,
            pipeline,
            std::span<const veyra::DisplayCommand>(infinite),
            actual,
            stats
        ) == veyra::NogaRenderResult::invalid_input,
        "infinite thickness is rejected"
    );
}

void check_translated_bounds(
    veyra::NogaContext& context,
    veyra::NogaFramePipeline& pipeline,
    std::span<veyra::SoftwarePixel> scratch,
    SoftwareRooms& rooms
) {
    auto stats = veyra::NogaFrameStats{};
    const auto nan = std::numeric_limits<float>::quiet_NaN();

    const std::array<veyra::DisplayCommand, 2> nan_offset{
        veyra::PushTranslation{.offset = {.x = nan, .y = 0.0F}},
        veyra::FillRect{
            .bounds = {.width = 8.0F, .height = 8.0F},
            .color = {.red = 255, .alpha = 255},
        },
    };

    require(
        veyra::render_noga_frame(
            context,
            pipeline,
            std::span<const veyra::DisplayCommand>(nan_offset),
            scratch,
            stats
        ) == veyra::NogaRenderResult::invalid_input,
        "NaN translation offsets are rejected"
    );

    const auto huge = std::numeric_limits<float>::max();

    const std::array<veyra::DisplayCommand, 2> overflow{
        veyra::PushTranslation{.offset = {.x = huge, .y = 0.0F}},
        veyra::FillRect{
            .bounds = {.x = huge, .width = 8.0F, .height = 8.0F},
            .color = {.red = 255, .alpha = 255},
        },
    };

    require(
        veyra::render_noga_frame(
            context,
            pipeline,
            std::span<const veyra::DisplayCommand>(overflow),
            scratch,
            stats
        ) == veyra::NogaRenderResult::invalid_input,
        "overflowing translations are rejected"
    );

    auto expected = std::array<veyra::SoftwarePixel, kWidth * kHeight>{};
    auto actual = std::array<veyra::SoftwarePixel, kWidth * kHeight>{};

    const std::array<veyra::DisplayCommand, 3> shifted{
        veyra::PushTranslation{.offset = {.x = 4.0F, .y = 4.0F}},
        veyra::FillRect{
            .bounds = {.width = 8.0F, .height = 8.0F},
            .color = {.red = 255, .alpha = 255},
        },
        veyra::PopTranslation{},
    };

    const auto shifted_span =
        std::span<const veyra::DisplayCommand>{shifted.data(), shifted.size()};

    render_reference(shifted_span, expected, kWidth, kHeight, rooms);

    require(
        veyra::render_noga_frame(context, pipeline, shifted_span, actual, stats) ==
            veyra::NogaRenderResult::complete,
        "finite translations render"
    );

    check_parity(context, pipeline, shifted_span, expected, actual, "translated", 0);
}

void check_frame_boundaries(
    veyra::NogaContext& context,
    veyra::NogaFramePipeline& pipeline,
    std::span<veyra::SoftwarePixel> scratch
) {
    const std::array<veyra::DisplayCommand, 0> empty{};

    auto stats = veyra::NogaFrameStats{};

    require(
        veyra::render_noga_frame(
            context,
            pipeline,
            std::span<const veyra::DisplayCommand>{empty.data(), empty.size()},
            scratch,
            stats
        ) == veyra::NogaRenderResult::complete,
        "empty scenes clear instead of failing"
    );
    require(stats.submitted == 0, "empty scenes submit nothing");
    require(stats.draws == 0, "empty scenes draw nothing");

    for (const auto& pixel : scratch) {
        require(
            pixel.red == 0 && pixel.green == 0 && pixel.blue == 0 && pixel.alpha == 255,
            "cleared pixels match the clear color"
        );
    }

    const std::array<veyra::DisplayCommand, 1> unbalanced{
        veyra::PushTranslation{.offset = {.x = 1.0F}},
    };

    require(
        veyra::render_noga_frame(context, pipeline, unbalanced, scratch, stats) ==
            veyra::NogaRenderResult::invalid_input,
        "unbalanced translation is rejected"
    );

    const std::array<veyra::DisplayCommand, 1> popped{
        veyra::PopTranslation{},
    };

    require(
        veyra::render_noga_frame(context, pipeline, popped, scratch, stats) ==
            veyra::NogaRenderResult::invalid_input,
        "popping an empty translation stack is rejected"
    );

    auto undersized = std::span<veyra::SoftwarePixel>{scratch.data(), scratch.size() - 1};

    const std::array<veyra::DisplayCommand, 1> sized{
        veyra::FillRect{
            .bounds = {.width = 8.0F, .height = 8.0F},
            .color = {.red = 200, .alpha = 255},
        },
    };

    const auto scene = std::span<const veyra::DisplayCommand>{sized.data(), sized.size()};

    require(
        veyra::render_noga_frame(context, pipeline, scene, undersized, stats) ==
            veyra::NogaRenderResult::invalid_input,
        "output extents must match the target"
    );
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

    check_rejections(
        context,
        pipeline,
        std::span<veyra::SoftwarePixel>{actual.data(), kWidth * kHeight}
    );

    check_stroke_parity(
        context,
        pipeline,
        std::span<veyra::SoftwarePixel>{expected.data(), kWidth * kHeight},
        std::span<veyra::SoftwarePixel>{actual.data(), kWidth * kHeight},
        rooms
    );

    check_stroke_edges(
        context,
        pipeline,
        std::span<veyra::SoftwarePixel>{expected.data(), kWidth * kHeight},
        std::span<veyra::SoftwarePixel>{actual.data(), kWidth * kHeight},
        rooms
    );

    check_frame_boundaries(
        context,
        pipeline,
        std::span<veyra::SoftwarePixel>{actual.data(), kWidth * kHeight}
    );

    check_translated_bounds(
        context,
        pipeline,
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
