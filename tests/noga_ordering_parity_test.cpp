#include "veyra/render/noga_frame.hpp"
#include "veyra/render/noga_rect.hpp"
#include "veyra/render/software_renderer.hpp"

#include "noga_parity_helpers.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

constexpr std::size_t kWidth = 64;
constexpr std::size_t kHeight = 48;
constexpr std::size_t kPixels = kWidth * kHeight;

using veyra::noga_parity::check_parity;
using veyra::noga_parity::render_reference;
using veyra::noga_parity::require;
using veyra::noga_parity::SoftwareRooms;

void check_stacked_order(
    veyra::NogaContext& context,
    veyra::NogaFramePipeline& pipeline,
    std::span<veyra::SoftwarePixel> expected,
    std::span<veyra::SoftwarePixel> actual,
    SoftwareRooms& rooms
) {
    const std::array<veyra::DisplayCommand, 4> storage{
        veyra::FillRect{
            .bounds = {.width = static_cast<float>(kWidth), .height = static_cast<float>(kHeight)},
            .color = {.red = 16, .green = 24, .blue = 32, .alpha = 255},
        },
        veyra::FillRect{
            .bounds = {.x = 8.0F, .y = 8.0F, .width = 40.0F, .height = 28.0F},
            .color = {.red = 220, .green = 40, .blue = 30, .alpha = 128},
        },
        veyra::FillRect{
            .bounds = {.x = 20.0F, .y = 14.0F, .width = 40.0F, .height = 28.0F},
            .color = {.red = 30, .green = 120, .blue = 220, .alpha = 128},
        },
        veyra::FillRect{
            .bounds = {.x = 14.0F, .y = 20.0F, .width = 40.0F, .height = 20.0F},
            .color = {.red = 240, .green = 200, .blue = 40, .alpha = 110},
        },
    };

    const auto scene = std::span<const veyra::DisplayCommand>{storage.data(), storage.size()};

    render_reference(scene, expected, kWidth, kHeight, rooms);

    auto stats = veyra::NogaFrameStats{};

    require(
        veyra::render_noga_frame(context, pipeline, scene, actual, stats) ==
            veyra::NogaRenderResult::complete,
        "stacked translucent draws complete"
    );
    require(stats.rect_draws == 1, "one command-order run draws every stacked rect");

    // NOTE: hardware blending evaluates source_alpha/255 in fp32 while the
    // software reference rounds integer weights, so doubly blended texels may
    // differ by one step. The deleted hand-written backend and the SDL GPU
    // backend prove the same scenes at the same tolerance; order is what this proves.
    check_parity(context, pipeline, scene, expected, actual, "stacked", 1);
}

void check_opaque_swap(
    veyra::NogaContext& context,
    veyra::NogaFramePipeline& pipeline,
    std::span<veyra::SoftwarePixel> expected,
    std::span<veyra::SoftwarePixel> actual,
    SoftwareRooms& rooms
) {
    const auto red_over_blue = std::array<veyra::DisplayCommand, 3>{
        veyra::FillRect{
            .bounds = {.width = static_cast<float>(kWidth), .height = static_cast<float>(kHeight)},
            .color = {.red = 16, .green = 24, .blue = 32, .alpha = 255},
        },
        veyra::FillRect{
            .bounds = {.x = 8.0F, .y = 8.0F, .width = 48.0F, .height = 32.0F},
            .color = {.red = 30, .green = 120, .blue = 220, .alpha = 255},
        },
        veyra::FillRect{
            .bounds = {.x = 8.0F, .y = 8.0F, .width = 48.0F, .height = 32.0F},
            .color = {.red = 220, .green = 40, .blue = 30, .alpha = 255},
        },
    };

    const auto blue_over_red = std::array<veyra::DisplayCommand, 3>{
        veyra::FillRect{
            .bounds = {.width = static_cast<float>(kWidth), .height = static_cast<float>(kHeight)},
            .color = {.red = 16, .green = 24, .blue = 32, .alpha = 255},
        },
        veyra::FillRect{
            .bounds = {.x = 8.0F, .y = 8.0F, .width = 48.0F, .height = 32.0F},
            .color = {.red = 220, .green = 40, .blue = 30, .alpha = 255},
        },
        veyra::FillRect{
            .bounds = {.x = 8.0F, .y = 8.0F, .width = 48.0F, .height = 32.0F},
            .color = {.red = 30, .green = 120, .blue = 220, .alpha = 255},
        },
    };

    const auto forward = std::span<const veyra::DisplayCommand>{
        red_over_blue.data(),
        red_over_blue.size(),
    };
    const auto backward = std::span<const veyra::DisplayCommand>{
        blue_over_red.data(),
        blue_over_red.size(),
    };

    render_reference(forward, expected, kWidth, kHeight, rooms);

    const auto forward_probe = expected[20 * kWidth + 16];

    require(
        forward_probe.red == 220 && forward_probe.blue == 30,
        "the software reference resolves the overlap red-over-blue"
    );

    check_parity(context, pipeline, forward, expected, actual, "opaque-forward", 0);

    render_reference(backward, expected, kWidth, kHeight, rooms);

    const auto backward_probe = expected[20 * kWidth + 16];

    require(
        backward_probe.red == 30 && backward_probe.blue == 220,
        "the software reference resolves the swapped overlap blue-over-red"
    );

    check_parity(context, pipeline, backward, expected, actual, "opaque-backward", 0);
}

void check_reversed_stack(
    veyra::NogaContext& context,
    veyra::NogaFramePipeline& pipeline,
    std::span<veyra::SoftwarePixel> expected,
    std::span<veyra::SoftwarePixel> actual,
    SoftwareRooms& rooms
) {
    const std::array<veyra::DisplayCommand, 3> storage{
        veyra::FillRect{
            .bounds = {.width = static_cast<float>(kWidth), .height = static_cast<float>(kHeight)},
            .color = {.red = 16, .green = 24, .blue = 32, .alpha = 255},
        },
        veyra::FillRect{
            .bounds = {.x = 8.0F, .y = 8.0F, .width = 48.0F, .height = 32.0F},
            .color = {.red = 30, .green = 120, .blue = 220, .alpha = 128},
        },
        veyra::FillRect{
            .bounds = {.x = 8.0F, .y = 8.0F, .width = 48.0F, .height = 32.0F},
            .color = {.red = 220, .green = 40, .blue = 30, .alpha = 128},
        },
    };

    const auto scene = std::span<const veyra::DisplayCommand>{storage.data(), storage.size()};

    render_reference(scene, expected, kWidth, kHeight, rooms);

    const auto probe = veyra::SoftwarePixel{
        .red = expected[20 * kWidth + 16].red,
        .green = expected[20 * kWidth + 16].green,
        .blue = expected[20 * kWidth + 16].blue,
        .alpha = 255,
    };

    require(
        probe.red > 100 && probe.blue < 120,
        "the software reference resolves the overlap red-over-blue"
    );

    check_parity(context, pipeline, scene, expected, actual, "reversed", 1);
}

void check_mixed_kind_order(
    veyra::NogaContext& context,
    veyra::NogaFramePipeline& pipeline,
    std::span<veyra::SoftwarePixel> expected,
    std::span<veyra::SoftwarePixel> actual,
    SoftwareRooms& rooms
) {
    const std::array<veyra::DisplayCommand, 4> storage{
        veyra::FillRect{
            .bounds = {.width = static_cast<float>(kWidth), .height = static_cast<float>(kHeight)},
            .color = {.red = 16, .green = 24, .blue = 32, .alpha = 255},
        },
        veyra::FillRoundedRect{
            .bounds = {.x = 6.0F, .y = 6.0F, .width = 52.0F, .height = 36.0F},
            .radius = 10.0F,
            .color = {.red = 220, .green = 40, .blue = 30, .alpha = 150},
        },
        veyra::FillRect{
            .bounds = {.x = 16.0F, .y = 12.0F, .width = 32.0F, .height = 24.0F},
            .color = {.red = 30, .green = 120, .blue = 220, .alpha = 150},
        },
        veyra::StrokeLine{
            .start = {.x = 4.0F, .y = 4.0F},
            .end = {.x = 60.0F, .y = 44.0F},
            .thickness = 3.0F,
            .color = {.red = 240, .green = 200, .blue = 40, .alpha = 160},
        },
    };

    const auto scene = std::span<const veyra::DisplayCommand>{storage.data(), storage.size()};

    render_reference(scene, expected, kWidth, kHeight, rooms);

    auto stats = veyra::NogaFrameStats{};

    require(
        veyra::render_noga_frame(context, pipeline, scene, actual, stats) ==
            veyra::NogaRenderResult::complete,
        "mixed-kind translucent draws complete"
    );
    require(stats.draws == 4, "every kind boundary flushes its own ordered run");

    check_parity(context, pipeline, scene, expected, actual, "mixed", 1);
}

int run() {
    auto context = veyra::NogaContext{};

    const auto context_result = veyra::create_noga_context(context, kWidth, kHeight);

    require(context_result == veyra::NogaContextResult::ready, "noga context comes up");

    auto pipeline = veyra::NogaFramePipeline{};

    const auto pipeline_result = veyra::create_noga_frame_pipeline(context, pipeline);

    require(pipeline_result == veyra::NogaRenderResult::complete, "frame pipeline comes up");

    auto rooms = SoftwareRooms{};
    auto expected = std::array<veyra::SoftwarePixel, kPixels>{};
    auto actual = std::array<veyra::SoftwarePixel, kPixels>{};

    check_stacked_order(
        context,
        pipeline,
        std::span<veyra::SoftwarePixel>{expected.data(), kPixels},
        std::span<veyra::SoftwarePixel>{actual.data(), kPixels},
        rooms
    );

    check_reversed_stack(
        context,
        pipeline,
        std::span<veyra::SoftwarePixel>{expected.data(), kPixels},
        std::span<veyra::SoftwarePixel>{actual.data(), kPixels},
        rooms
    );

    check_opaque_swap(
        context,
        pipeline,
        std::span<veyra::SoftwarePixel>{expected.data(), kPixels},
        std::span<veyra::SoftwarePixel>{actual.data(), kPixels},
        rooms
    );

    check_mixed_kind_order(
        context,
        pipeline,
        std::span<veyra::SoftwarePixel>{expected.data(), kPixels},
        std::span<veyra::SoftwarePixel>{actual.data(), kPixels},
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
