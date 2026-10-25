#include "allocation_counter.hpp"
#include "veyra/render/damage_tracker.hpp"
#include "veyra/render/noga_frame.hpp"
#include "veyra/render/noga_rect.hpp"
#include "veyra/render/software_renderer.hpp"

#include "noga_parity_helpers.hpp"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <new>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

using veyra::noga_parity::render_reference;
using veyra::noga_parity::require;
using veyra::noga_parity::SoftwareRooms;

constexpr std::size_t kWidth = 64;
constexpr std::size_t kHeight = 48;
constexpr std::size_t kPixels = kWidth * kHeight;

void check_pixels_near(
    std::span<const veyra::SoftwarePixel> expected,
    std::span<const veyra::SoftwarePixel> actual,
    std::uint64_t tolerance,
    std::string_view stage
) {
    require(expected.size() == actual.size(), "damaged parity compares equal extents");

    auto max_error = 0;

    for (auto i = std::size_t{}; i < expected.size(); ++i) {
        const auto& want = expected[i];
        const auto& got = actual[i];
        const auto channels = std::array<std::uint8_t, 4>{
            want.red,
            want.green,
            want.blue,
            want.alpha,
        };
        const auto found = std::array<std::uint8_t, 4>{
            got.red,
            got.green,
            got.blue,
            got.alpha,
        };

        for (auto channel = std::size_t{}; channel < 4; ++channel) {
            const auto difference =
                static_cast<int>(channels[channel]) - static_cast<int>(found[channel]);
            const auto distance = difference >= 0 ? difference : -difference;

            max_error = max_error >= distance ? max_error : distance;

            if (distance > static_cast<int>(tolerance)) {
                std::cout << "noga damaged mismatch stage=" << stage << " pixel=" << i
                          << " channel=" << channel
                          << " want=" << static_cast<int>(channels[channel])
                          << " got=" << static_cast<int>(found[channel]) << '\n';

                throw std::runtime_error("damaged pixels exceed the tolerance bar");
            }
        }
    }

    std::cout << "noga damaged stage=" << stage << " max_abs_error=" << max_error << '\n';
}

void prime_target(
    veyra::NogaContext& context,
    veyra::NogaFramePipeline& pipeline,
    std::span<const veyra::DisplayCommand> prime,
    std::span<veyra::SoftwarePixel> scratch
) {
    auto stats = veyra::NogaFrameStats{};

    require(
        veyra::render_noga_frame(context, pipeline, prime, scratch, stats) ==
            veyra::NogaRenderResult::complete,
        "priming full render completes"
    );
}

void check_damaged_matches_full(
    veyra::NogaContext& context,
    veyra::NogaFramePipeline& pipeline,
    std::span<const veyra::DisplayCommand> prime,
    std::span<const veyra::DisplayCommand> scene,
    std::span<const veyra::LayoutRect> damage,
    std::span<veyra::SoftwarePixel> expected,
    std::span<veyra::SoftwarePixel> actual,
    std::string_view stage,
    veyra::NogaFrameStats& stats
) {
    prime_target(context, pipeline, prime, actual);

    require(
        veyra::render_noga_frame_damaged(context, pipeline, scene, damage, actual, stats) ==
            veyra::NogaRenderResult::complete,
        "damaged replay completes"
    );

    auto full_stats = veyra::NogaFrameStats{};

    require(
        veyra::render_noga_frame(context, pipeline, scene, expected, full_stats) ==
            veyra::NogaRenderResult::complete,
        "full replay completes"
    );

    check_pixels_near(expected, actual, 0, stage);
}

void check_mixed_tiled_coverage(
    veyra::NogaContext& context,
    veyra::NogaFramePipeline& pipeline,
    std::span<veyra::SoftwarePixel> expected,
    std::span<veyra::SoftwarePixel> actual
) {
    auto image_pixels = std::array<veyra::SoftwarePixel, 16>{};

    for (auto& pixel : image_pixels) {
        pixel = veyra::SoftwarePixel{.red = 200, .green = 120, .blue = 40, .alpha = 255};
    }

    const auto tile = veyra::SoftwareImage{
        .width = 4,
        .height = 4,
        .stride = 4,
        .pixels = image_pixels,
        .generation = 11,
    };

    auto mask_coverage = std::array<std::uint8_t, 16>{
        255,
        200,
        150,
        100,
        200,
        255,
        200,
        150,
        150,
        200,
        255,
        200,
        100,
        150,
        200,
        255,
    };

    const auto mask = veyra::SoftwareMask{
        .width = 4,
        .height = 4,
        .stride = 4,
        .coverage = mask_coverage,
        .generation = 12,
    };

    const std::array<veyra::SoftwarePathSegment, 3> triangle_segments{
        veyra::SoftwareLineTo{.end = {.x = 52.0F, .y = 40.0F}},
        veyra::SoftwareLineTo{.end = {.x = 44.0F, .y = 40.0F}},
        veyra::SoftwareLineTo{.end = {.x = 48.0F, .y = 32.0F}},
    };
    const std::array<veyra::SoftwarePathContour, 1> triangle_contours{
        veyra::SoftwarePathContour{
            .start = {.x = 48.0F, .y = 32.0F},
            .segments = triangle_segments,
        },
    };

    const std::array<veyra::DisplayCommand, 9> scene{
        veyra::FillRect{
            .bounds = {.width = static_cast<float>(kWidth), .height = static_cast<float>(kHeight)},
            .color = {.red = 16, .green = 24, .blue = 32, .alpha = 255},
        },
        veyra::FillRect{
            .bounds = {.x = 4.0F, .y = 8.0F, .width = 56.0F, .height = 8.0F},
            .color = {.red = 220, .green = 40, .blue = 30, .alpha = 128},
        },
        veyra::FillRoundedRect{
            .bounds = {.x = 8.0F, .y = 20.0F, .width = 24.0F, .height = 12.0F},
            .radius = 5.0F,
            .color = {.red = 30, .green = 120, .blue = 220, .alpha = 150},
        },
        veyra::StrokeLine{
            .start = {.x = 2.0F, .y = 2.0F},
            .end = {.x = 62.0F, .y = 46.0F},
            .thickness = 2.0F,
            .color = {.red = 240, .green = 200, .blue = 40, .alpha = 160},
        },
        veyra::StrokeRect{
            .bounds = {.x = 36.0F, .y = 20.0F, .width = 20.0F, .height = 12.0F},
            .thickness = 2.0F,
            .color = {.red = 30, .green = 140, .blue = 60, .alpha = 255},
        },
        veyra::FillPath{
            .contours = triangle_contours,
            .color = {.red = 150, .green = 40, .blue = 170, .alpha = 200},
            .fill_rule = veyra::SoftwarePathFillRule::even_odd,
        },
        veyra::DrawImage{
            .source = {.width = 4.0F, .height = 4.0F},
            .destination = {.x = 8.0F, .y = 34.0F, .width = 12.0F, .height = 12.0F},
            .image = &tile,
            .sampling = veyra::SoftwareImageSampling::nearest,
            .opacity = 255,
        },
        veyra::DrawMask{
            .x = 40.0F,
            .y = 34.0F,
            .mask = &mask,
            .color = {.red = 230, .green = 232, .blue = 240, .alpha = 255},
        },
        veyra::FillRect{
            .bounds = {.x = 4.0F, .y = 40.0F, .width = 56.0F, .height = 4.0F},
            .color = {.red = 90, .green = 90, .blue = 96, .alpha = 255},
        },
    };

    const std::array<veyra::DisplayCommand, 1> prime{
        veyra::FillRect{
            .bounds = {.width = static_cast<float>(kWidth), .height = static_cast<float>(kHeight)},
            .color = {.red = 200, .green = 40, .blue = 180, .alpha = 255},
        },
    };

    const std::array<veyra::LayoutRect, 2> halves{
        veyra::LayoutRect{.width = 32.0F, .height = static_cast<float>(kHeight)},
        veyra::LayoutRect{.x = 32.0F, .width = 32.0F, .height = static_cast<float>(kHeight)},
    };

    auto stats = veyra::NogaFrameStats{};

    check_damaged_matches_full(
        context,
        pipeline,
        std::span<const veyra::DisplayCommand>{prime.data(), prime.size()},
        std::span<const veyra::DisplayCommand>{scene.data(), scene.size()},
        std::span<const veyra::LayoutRect>{halves.data(), halves.size()},
        expected,
        actual,
        "mixed-tiled",
        stats
    );

    require(stats.damage_regions == 2, "two halves replay");
    require(
        stats.damage_pixels == static_cast<std::uint64_t>(kWidth) * kHeight,
        "tiled pixels cover the surface"
    );
}

void check_chat_row_targeted(
    veyra::NogaContext& context,
    veyra::NogaFramePipeline& pipeline,
    std::span<veyra::SoftwarePixel> expected,
    std::span<veyra::SoftwarePixel> actual
) {
    const auto row = [](float y, veyra::DisplayColor color) {
        return veyra::FillRect{
            .bounds = {.x = 4.0F, .y = y, .width = 56.0F, .height = 6.0F},
            .color = color,
        };
    };

    const auto gray = veyra::DisplayColor{.red = 70, .green = 74, .blue = 80, .alpha = 255};
    const auto teal = veyra::DisplayColor{.red = 30, .green = 140, .blue = 140, .alpha = 255};
    const auto rust = veyra::DisplayColor{.red = 200, .green = 90, .blue = 40, .alpha = 255};

    const std::array<veyra::DisplayCommand, 7> before{
        veyra::FillRect{
            .bounds = {.width = static_cast<float>(kWidth), .height = static_cast<float>(kHeight)},
            .color = {.red = 16, .green = 24, .blue = 32, .alpha = 255},
        },
        row(4.0F, gray),
        row(12.0F, gray),
        row(20.0F, gray),
        row(28.0F, gray),
        row(36.0F, gray),
        veyra::FillRect{
            .bounds = {.x = 4.0F, .y = 44.0F, .width = 56.0F, .height = 2.0F},
            .color = {.red = 90, .green = 90, .blue = 96, .alpha = 255},
        },
    };

    const std::array<veyra::DisplayCommand, 8> after{
        veyra::FillRect{
            .bounds = {.width = static_cast<float>(kWidth), .height = static_cast<float>(kHeight)},
            .color = {.red = 16, .green = 24, .blue = 32, .alpha = 255},
        },
        row(4.0F, gray),
        row(12.0F, teal),
        row(20.0F, gray),
        row(28.0F, rust),
        row(36.0F, gray),
        veyra::FillRect{
            .bounds = {.x = 8.0F, .y = 13.0F, .width = 20.0F, .height = 4.0F},
            .color = {.red = 240, .green = 200, .blue = 40, .alpha = 120},
        },
        veyra::FillRect{
            .bounds = {.x = 4.0F, .y = 44.0F, .width = 56.0F, .height = 2.0F},
            .color = {.red = 90, .green = 90, .blue = 96, .alpha = 255},
        },
    };

    auto storage = std::array<veyra::LayoutRect, 8>{};
    auto tracker = veyra::DamageTracker(kWidth, kHeight, storage);

    require(tracker.valid(), "row damage tracker is valid");
    require(
        tracker.add({.x = 4.0F, .y = 12.0F, .width = 56.0F, .height = 6.0F}),
        "changed row adds"
    );
    require(
        tracker.add({.x = 4.0F, .y = 28.0F, .width = 56.0F, .height = 6.0F}),
        "second changed row adds"
    );
    require(!tracker.full_surface(), "two rows stay partial");

    prime_target(
        context,
        pipeline,
        std::span<const veyra::DisplayCommand>{before.data(), before.size()},
        actual
    );

    auto stats = veyra::NogaFrameStats{};

    require(
        veyra::render_noga_frame_damaged(
            context,
            pipeline,
            std::span<const veyra::DisplayCommand>{after.data(), after.size()},
            tracker,
            actual,
            stats
        ) == veyra::NogaRenderResult::complete,
        "row-targeted replay completes"
    );

    auto full_stats = veyra::NogaFrameStats{};

    require(
        veyra::render_noga_frame(
            context,
            pipeline,
            std::span<const veyra::DisplayCommand>{after.data(), after.size()},
            expected,
            full_stats
        ) == veyra::NogaRenderResult::complete,
        "full replay completes"
    );

    check_pixels_near(expected, actual, 0, "chat-rows");

    std::cout << "noga damaged regions=" << stats.damage_regions
              << " pixels=" << stats.damage_pixels << " skipped=" << stats.damage_skipped
              << " submitted=" << stats.submitted << '\n';

    require(stats.damage_regions == 2, "two rows replay");
    require(stats.damage_pixels == 56 * 6 * 2, "row pixels counted");
    require(stats.damage_skipped == 11, "each region skips every draw outside it");
}

void check_tracker_full_fallback(
    veyra::NogaContext& context,
    veyra::NogaFramePipeline& pipeline,
    std::span<veyra::SoftwarePixel> expected,
    std::span<veyra::SoftwarePixel> actual
) {
    const std::array<veyra::DisplayCommand, 3> scene{
        veyra::FillRect{
            .bounds = {.width = static_cast<float>(kWidth), .height = static_cast<float>(kHeight)},
            .color = {.red = 16, .green = 24, .blue = 32, .alpha = 255},
        },
        veyra::FillRect{
            .bounds = {.x = 8.0F, .y = 8.0F, .width = 48.0F, .height = 32.0F},
            .color = {.red = 220, .green = 40, .blue = 30, .alpha = 128},
        },
        veyra::FillRect{
            .bounds = {.x = 8.0F, .y = 8.0F, .width = 48.0F, .height = 32.0F},
            .color = {.red = 30, .green = 120, .blue = 220, .alpha = 128},
        },
    };

    auto storage = std::array<veyra::LayoutRect, 4>{};
    auto tracker = veyra::DamageTracker(kWidth, kHeight, storage);

    require(tracker.mark_full(), "explicit full damage records");

    auto damaged_stats = veyra::NogaFrameStats{};
    auto full_stats = veyra::NogaFrameStats{};

    require(
        veyra::render_noga_frame_damaged(
            context,
            pipeline,
            std::span<const veyra::DisplayCommand>{scene.data(), scene.size()},
            tracker,
            actual,
            damaged_stats
        ) == veyra::NogaRenderResult::complete,
        "full-surface tracker completes"
    );
    require(
        veyra::render_noga_frame(
            context,
            pipeline,
            std::span<const veyra::DisplayCommand>{scene.data(), scene.size()},
            expected,
            full_stats
        ) == veyra::NogaRenderResult::complete,
        "full entry completes"
    );

    check_pixels_near(expected, actual, 0, "tracker-full");

    require(damaged_stats.submitted == full_stats.submitted, "fallback submits the full list");
    require(damaged_stats.damage_regions == 0, "fallback takes the full path");

    const std::span<veyra::LayoutRect> no_storage;
    auto invalid = veyra::DamageTracker(kWidth, kHeight, no_storage);

    require(!invalid.valid(), "empty workspace is invalid");

    require(
        veyra::render_noga_frame_damaged(
            context,
            pipeline,
            std::span<const veyra::DisplayCommand>{scene.data(), scene.size()},
            invalid,
            actual,
            damaged_stats
        ) == veyra::NogaRenderResult::complete,
        "invalid tracker falls back to full"
    );

    check_pixels_near(expected, actual, 0, "invalid-tracker");
}

void check_noop_frames(
    veyra::NogaContext& context,
    veyra::NogaFramePipeline& pipeline,
    std::span<veyra::SoftwarePixel> actual
) {
    const std::array<veyra::DisplayCommand, 1> scene{
        veyra::FillRect{
            .bounds = {.width = static_cast<float>(kWidth), .height = static_cast<float>(kHeight)},
            .color = {.red = 16, .green = 24, .blue = 32, .alpha = 255},
        },
    };

    const auto sentinel = veyra::SoftwarePixel{.red = 7, .green = 8, .blue = 9, .alpha = 10};

    for (auto& pixel : actual) {
        pixel = sentinel;
    }

    auto storage = std::array<veyra::LayoutRect, 4>{};
    auto tracker = veyra::DamageTracker(kWidth, kHeight, storage);

    require(tracker.valid(), "empty tracker is valid");
    require(tracker.regions().empty(), "fresh tracker holds no regions");

    auto stats = veyra::NogaFrameStats{
        .submitted = 99,
        .draws = 99,
        .damage_regions = 99,
        .damage_pixels = 99,
        .damage_skipped = 99,
    };

    require(
        veyra::render_noga_frame_damaged(
            context,
            pipeline,
            std::span<const veyra::DisplayCommand>{scene.data(), scene.size()},
            tracker,
            actual,
            stats
        ) == veyra::NogaRenderResult::unchanged,
        "empty regions skip the frame"
    );

    for (const auto& pixel : actual) {
        require(
            pixel.red == 7 && pixel.green == 8 && pixel.blue == 9 && pixel.alpha == 10,
            "no-op frame leaves the retained buffer alone"
        );
    }

    require(
        stats.submitted == 0 && stats.draws == 0 && stats.damage_regions == 0 &&
            stats.damage_pixels == 0 && stats.damage_skipped == 0,
        "no-op frame reports zero stats"
    );

    require(
        veyra::render_noga_frame_damaged(
            context,
            pipeline,
            std::span<const veyra::DisplayCommand>{scene.data(), scene.size()},
            std::span<const veyra::LayoutRect>{},
            actual,
            stats
        ) == veyra::NogaRenderResult::unchanged,
        "empty span skips the frame"
    );

    const std::array<veyra::LayoutRect, 2> outside{
        veyra::LayoutRect{.x = 200.0F, .y = 200.0F, .width = 8.0F, .height = 8.0F},
        veyra::LayoutRect{.x = -32.0F, .y = -16.0F, .width = 8.0F, .height = 8.0F},
    };

    require(
        veyra::render_noga_frame_damaged(
            context,
            pipeline,
            std::span<const veyra::DisplayCommand>{scene.data(), scene.size()},
            std::span<const veyra::LayoutRect>{outside.data(), outside.size()},
            actual,
            stats
        ) == veyra::NogaRenderResult::unchanged,
        "fully outside regions skip the frame"
    );

    for (const auto& pixel : actual) {
        require(pixel.red == 7, "outside-only damage leaves the buffer alone");
    }
}

void check_out_of_bounds_clipped(
    veyra::NogaContext& context,
    veyra::NogaFramePipeline& pipeline,
    std::span<veyra::SoftwarePixel> expected,
    std::span<veyra::SoftwarePixel> actual
) {
    const std::array<veyra::DisplayCommand, 2> scene{
        veyra::FillRect{
            .bounds = {.width = static_cast<float>(kWidth), .height = static_cast<float>(kHeight)},
            .color = {.red = 16, .green = 24, .blue = 32, .alpha = 255},
        },
        veyra::FillRect{
            .bounds = {.x = -8.0F, .y = -8.0F, .width = 32.0F, .height = 32.0F},
            .color = {.red = 220, .green = 40, .blue = 30, .alpha = 255},
        },
    };

    const std::array<veyra::LayoutRect, 2> damage{
        veyra::LayoutRect{.x = -8.0F, .y = -8.0F, .width = 40.0F, .height = 40.0F},
        veyra::LayoutRect{
            .x = 32.0F,
            .y = 32.0F,
            .width = static_cast<float>(kWidth),
            .height = static_cast<float>(kHeight)
        },
    };

    auto stats = veyra::NogaFrameStats{};

    check_damaged_matches_full(
        context,
        pipeline,
        std::span<const veyra::DisplayCommand>{scene.data(), scene.size()},
        std::span<const veyra::DisplayCommand>{scene.data(), scene.size()},
        std::span<const veyra::LayoutRect>{damage.data(), damage.size()},
        expected,
        actual,
        "out-of-bounds",
        stats
    );

    require(stats.damage_regions == 2, "clipped regions still replay");
    require(stats.damage_pixels == 32 * 32 + 32 * 16, "pixels clip to the surface");
}

void check_ordering_scene_damaged(
    veyra::NogaContext& context,
    veyra::NogaFramePipeline& pipeline,
    std::span<const veyra::DisplayCommand> scene,
    std::span<veyra::SoftwarePixel> expected,
    std::span<veyra::SoftwarePixel> actual,
    SoftwareRooms& rooms,
    std::string_view stage,
    std::uint64_t tolerance
) {
    const std::array<veyra::DisplayCommand, 1> prime{
        veyra::FillRect{
            .bounds = {.width = static_cast<float>(kWidth), .height = static_cast<float>(kHeight)},
            .color = {.red = 200, .green = 40, .blue = 180, .alpha = 255},
        },
    };

    const std::array<veyra::LayoutRect, 2> halves{
        veyra::LayoutRect{.width = 32.0F, .height = static_cast<float>(kHeight)},
        veyra::LayoutRect{.x = 32.0F, .width = 32.0F, .height = static_cast<float>(kHeight)},
    };

    render_reference(scene, expected, kWidth, kHeight, rooms);
    prime_target(
        context,
        pipeline,
        std::span<const veyra::DisplayCommand>{prime.data(), prime.size()},
        actual
    );

    auto stats = veyra::NogaFrameStats{};

    require(
        veyra::render_noga_frame_damaged(
            context,
            pipeline,
            scene,
            std::span<const veyra::LayoutRect>{halves.data(), halves.size()},
            actual,
            stats
        ) == veyra::NogaRenderResult::complete,
        "damaged ordering replay completes"
    );

    check_pixels_near(expected, actual, tolerance, stage);
}

void check_ordering_through_damaged(
    veyra::NogaContext& context,
    veyra::NogaFramePipeline& pipeline,
    std::span<veyra::SoftwarePixel> expected,
    std::span<veyra::SoftwarePixel> actual,
    SoftwareRooms& rooms
) {
    const std::array<veyra::DisplayCommand, 4> stacked{
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

    check_ordering_scene_damaged(
        context,
        pipeline,
        std::span<const veyra::DisplayCommand>{stacked.data(), stacked.size()},
        expected,
        actual,
        rooms,
        "ordering-stacked",
        1
    );

    const std::array<veyra::DisplayCommand, 3> red_over_blue{
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

    check_ordering_scene_damaged(
        context,
        pipeline,
        std::span<const veyra::DisplayCommand>{red_over_blue.data(), red_over_blue.size()},
        expected,
        actual,
        rooms,
        "ordering-opaque-forward",
        0
    );

    const std::array<veyra::DisplayCommand, 3> blue_over_red{
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

    check_ordering_scene_damaged(
        context,
        pipeline,
        std::span<const veyra::DisplayCommand>{blue_over_red.data(), blue_over_red.size()},
        expected,
        actual,
        rooms,
        "ordering-opaque-backward",
        0
    );

    const std::array<veyra::DisplayCommand, 4> mixed{
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

    check_ordering_scene_damaged(
        context,
        pipeline,
        std::span<const veyra::DisplayCommand>{mixed.data(), mixed.size()},
        expected,
        actual,
        rooms,
        "ordering-mixed",
        1
    );
}

void check_damage_rejections(
    veyra::NogaContext& context,
    veyra::NogaFramePipeline& pipeline,
    std::span<veyra::SoftwarePixel> scratch
) {
    const std::array<veyra::DisplayCommand, 1> scene{
        veyra::FillRect{
            .bounds = {.width = 8.0F, .height = 8.0F},
            .color = {.red = 200, .alpha = 255},
        },
    };

    auto stats = veyra::NogaFrameStats{};
    const auto nan = std::numeric_limits<float>::quiet_NaN();

    const std::array<veyra::LayoutRect, 1> indefinite{
        veyra::LayoutRect{.x = nan, .width = 8.0F, .height = 8.0F},
    };

    require(
        veyra::render_noga_frame_damaged(
            context,
            pipeline,
            std::span<const veyra::DisplayCommand>{scene.data(), scene.size()},
            std::span<const veyra::LayoutRect>{indefinite.data(), indefinite.size()},
            scratch,
            stats
        ) == veyra::NogaRenderResult::invalid_input,
        "non-finite damage is rejected"
    );

    const std::array<veyra::LayoutRect, 2> overlapping{
        veyra::LayoutRect{.width = 16.0F, .height = 16.0F},
        veyra::LayoutRect{.x = 8.0F, .width = 16.0F, .height = 16.0F},
    };

    require(
        veyra::render_noga_frame_damaged(
            context,
            pipeline,
            std::span<const veyra::DisplayCommand>{scene.data(), scene.size()},
            std::span<const veyra::LayoutRect>{overlapping.data(), overlapping.size()},
            scratch,
            stats
        ) == veyra::NogaRenderResult::invalid_input,
        "overlapping damage is rejected"
    );

    const std::array<veyra::DisplayCommand, 1> unbalanced{
        veyra::PushTranslation{.offset = {.x = 1.0F}},
    };

    const std::array<veyra::LayoutRect, 1> whole{
        veyra::LayoutRect{
            .width = static_cast<float>(kWidth),
            .height = static_cast<float>(kHeight)
        },
    };

    require(
        veyra::render_noga_frame_damaged(
            context,
            pipeline,
            std::span<const veyra::DisplayCommand>{unbalanced.data(), unbalanced.size()},
            std::span<const veyra::LayoutRect>{whole.data(), whole.size()},
            scratch,
            stats
        ) == veyra::NogaRenderResult::invalid_input,
        "unbalanced commands are rejected on the damaged path"
    );
}

void check_steady_damaged_allocates_nothing(
    veyra::NogaContext& context,
    veyra::NogaFramePipeline& pipeline,
    std::span<veyra::SoftwarePixel> actual
) {
    const std::array<veyra::DisplayCommand, 4> scene{
        veyra::FillRect{
            .bounds = {.width = static_cast<float>(kWidth), .height = static_cast<float>(kHeight)},
            .color = {.red = 16, .green = 24, .blue = 32, .alpha = 255},
        },
        veyra::FillRect{
            .bounds = {.x = 8.0F, .y = 8.0F, .width = 48.0F, .height = 12.0F},
            .color = {.red = 220, .green = 40, .blue = 30, .alpha = 255},
        },
        veyra::FillRect{
            .bounds = {.x = 8.0F, .y = 28.0F, .width = 48.0F, .height = 12.0F},
            .color = {.red = 30, .green = 120, .blue = 220, .alpha = 255},
        },
        veyra::StrokeLine{
            .start = {.x = 4.0F, .y = 4.0F},
            .end = {.x = 60.0F, .y = 44.0F},
            .thickness = 2.0F,
            .color = {.red = 240, .green = 200, .blue = 40, .alpha = 255},
        },
    };

    const std::array<veyra::LayoutRect, 1> damage{
        veyra::LayoutRect{.x = 8.0F, .y = 8.0F, .width = 48.0F, .height = 12.0F},
    };

    const auto commands = std::span<const veyra::DisplayCommand>{scene.data(), scene.size()};
    const auto regions = std::span<const veyra::LayoutRect>{damage.data(), damage.size()};

    prime_target(context, pipeline, commands, actual);

    auto stats = veyra::NogaFrameStats{};

    require(
        veyra::render_noga_frame_damaged(context, pipeline, commands, regions, actual, stats) ==
            veyra::NogaRenderResult::complete,
        "steady damaged window opens clean"
    );

    g_allocation_count.store(0, std::memory_order_relaxed);
    g_counting.store(true, std::memory_order_relaxed);

    for (auto repeat = std::size_t{}; repeat < 60; ++repeat) {
        const auto result =
            veyra::render_noga_frame_damaged(context, pipeline, commands, regions, actual, stats);

        if (result != veyra::NogaRenderResult::complete) {
            g_counting.store(false, std::memory_order_relaxed);

            throw std::runtime_error("steady damaged repeats stay complete");
        }
    }

    g_counting.store(false, std::memory_order_relaxed);

    std::cout << "noga damaged repeats=60 host_allocations="
              << g_allocation_count.load(std::memory_order_relaxed) << '\n';

    require(
        g_allocation_count.load(std::memory_order_relaxed) == 0,
        "damaged frames allocate nothing"
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

    auto rooms = SoftwareRooms{};
    auto expected = std::array<veyra::SoftwarePixel, kPixels>{};
    auto actual = std::array<veyra::SoftwarePixel, kPixels>{};

    check_mixed_tiled_coverage(
        context,
        pipeline,
        std::span<veyra::SoftwarePixel>{expected.data(), kPixels},
        std::span<veyra::SoftwarePixel>{actual.data(), kPixels}
    );

    check_chat_row_targeted(
        context,
        pipeline,
        std::span<veyra::SoftwarePixel>{expected.data(), kPixels},
        std::span<veyra::SoftwarePixel>{actual.data(), kPixels}
    );

    check_tracker_full_fallback(
        context,
        pipeline,
        std::span<veyra::SoftwarePixel>{expected.data(), kPixels},
        std::span<veyra::SoftwarePixel>{actual.data(), kPixels}
    );

    check_noop_frames(context, pipeline, std::span<veyra::SoftwarePixel>{actual.data(), kPixels});

    check_out_of_bounds_clipped(
        context,
        pipeline,
        std::span<veyra::SoftwarePixel>{expected.data(), kPixels},
        std::span<veyra::SoftwarePixel>{actual.data(), kPixels}
    );

    check_ordering_through_damaged(
        context,
        pipeline,
        std::span<veyra::SoftwarePixel>{expected.data(), kPixels},
        std::span<veyra::SoftwarePixel>{actual.data(), kPixels},
        rooms
    );

    check_damage_rejections(
        context,
        pipeline,
        std::span<veyra::SoftwarePixel>{actual.data(), kPixels}
    );

    check_steady_damaged_allocates_nothing(
        context,
        pipeline,
        std::span<veyra::SoftwarePixel>{actual.data(), kPixels}
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
