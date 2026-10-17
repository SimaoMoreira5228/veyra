#include "scroll_surface_helpers.hpp"
#include "veyra/render/scroll_shift.hpp"

#include "veyra/render/damage_tracker.hpp"
#include "veyra/render/software_renderer.hpp"

#include <array>
#include <cstdint>
#include <iostream>
#include <limits>
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

bool inside_rect(const veyra::ScrollShiftRect& rect, std::size_t x, std::size_t y) {
    return x >= rect.left && x < rect.left + rect.width && y >= rect.top &&
        y < rect.top + rect.height;
}

veyra::SoftwarePixel content_pixel(int content_x, int content_y) {
    const auto red = static_cast<std::uint8_t>((content_x * 31 + content_y * 17 + 11) & 0xFF);
    const auto green = static_cast<std::uint8_t>((content_x * 13 + content_y * 29 + 7) & 0xFF);
    const auto blue = static_cast<std::uint8_t>((content_x * 7 + content_y * 5 + 3) & 0xFF);

    return {
        .red = red,
        .green = green,
        .blue = blue,
        .alpha = 255,
    };
}

void paint_content(
    std::span<veyra::SoftwarePixel> pixels,
    std::size_t width,
    std::size_t height,
    std::size_t stride,
    int offset_x,
    int offset_y
) {
    for (std::size_t y = 0; y < height; ++y) {
        for (std::size_t x = 0; x < width; ++x) {
            pixels[y * stride + x] =
                content_pixel(static_cast<int>(x) + offset_x, static_cast<int>(y) + offset_y);
        }
    }
}

void repaint_strip(
    std::span<veyra::SoftwarePixel> pixels,
    std::size_t stride,
    const veyra::ScrollShiftRect& strip,
    int offset_x,
    int offset_y
) {
    for (std::size_t y = 0; y < strip.height; ++y) {
        for (std::size_t x = 0; x < strip.width; ++x) {
            const auto pixel_x = strip.left + x;
            const auto pixel_y = strip.top + y;
            pixels[pixel_y * stride + pixel_x] = content_pixel(
                static_cast<int>(pixel_x) + offset_x,
                static_cast<int>(pixel_y) + offset_y
            );
        }
    }
}

void check_zero_delta_is_no_movement() {
    veyra::ScrollShiftPlan plan{
        .has_blit = true,
        .strip_count = 7,
    };
    const auto result =
        veyra::resolve_scroll_shift({.width = 10, .height = 8, .delta_x = 0, .delta_y = 0}, plan);

    require(result == veyra::ScrollShiftResult::no_movement, "zero delta reports no movement");
    require(!plan.has_blit, "zero delta emits no blit");
    require(plan.strip_count == 0, "zero delta emits no strips");
    require(!plan.full_damage_x && !plan.full_damage_y, "zero delta damages nothing");
}

void check_horizontal_geometry() {
    veyra::ScrollShiftPlan plan{};
    const auto result =
        veyra::resolve_scroll_shift({.width = 10, .height = 8, .delta_x = 3, .delta_y = 0}, plan);

    require(result == veyra::ScrollShiftResult::complete, "horizontal shift resolves");
    require(plan.has_blit, "partial horizontal shift keeps a blit");
    require(
        plan.source.left == 0 && plan.source.top == 0 && plan.source.width == 7 &&
            plan.source.height == 8,
        "rightward shift reads from the left edge"
    );
    require(
        plan.destination.left == 3 && plan.destination.top == 0 && plan.destination.width == 7 &&
            plan.destination.height == 8,
        "rightward shift lands three pixels right"
    );
    require(plan.strip_count == 1, "one axis shift exposes one strip");
    require(
        plan.strips[0].left == 0 && plan.strips[0].top == 0 && plan.strips[0].width == 3 &&
            plan.strips[0].height == 8,
        "rightward shift exposes the left band"
    );

    veyra::ScrollShiftPlan left_plan{};
    const auto left_result = veyra::resolve_scroll_shift(
        {.width = 10, .height = 8, .delta_x = -3, .delta_y = 0},
        left_plan
    );

    require(left_result == veyra::ScrollShiftResult::complete, "negative shift resolves");
    require(
        left_plan.source.left == 3 && left_plan.destination.left == 0 &&
            left_plan.source.width == 7 && left_plan.destination.width == 7,
        "leftward shift reads from the right and lands at zero"
    );
    require(
        left_plan.strips[0].left == 7 && left_plan.strips[0].width == 3 &&
            left_plan.strips[0].height == 8,
        "leftward shift exposes the right band"
    );
}

void check_vertical_geometry() {
    veyra::ScrollShiftPlan plan{};
    const auto result =
        veyra::resolve_scroll_shift({.width = 10, .height = 8, .delta_x = 0, .delta_y = 2}, plan);

    require(result == veyra::ScrollShiftResult::complete, "vertical shift resolves");
    require(
        plan.source.top == 0 && plan.source.height == 6 && plan.destination.top == 2 &&
            plan.destination.height == 6,
        "downward shift reads from the top and lands two pixels down"
    );
    require(
        plan.strip_count == 1 && plan.strips[0].left == 0 && plan.strips[0].top == 0 &&
            plan.strips[0].width == 10 && plan.strips[0].height == 2,
        "downward shift exposes the full-width top band"
    );
}

void check_diagonal_geometry() {
    veyra::ScrollShiftPlan plan{};
    const auto result =
        veyra::resolve_scroll_shift({.width = 10, .height = 8, .delta_x = 2, .delta_y = 3}, plan);

    require(result == veyra::ScrollShiftResult::complete, "diagonal shift resolves");
    require(
        plan.source.left == 0 && plan.source.top == 0 && plan.source.width == 8 &&
            plan.source.height == 5,
        "diagonal shift reads the surviving corner"
    );
    require(
        plan.destination.left == 2 && plan.destination.top == 3 && plan.destination.width == 8 &&
            plan.destination.height == 5,
        "diagonal shift lands offset right and down"
    );
    require(plan.strip_count == 2, "diagonal shift exposes two strips");
    require(
        plan.strips[0].left == 0 && plan.strips[0].top == 0 && plan.strips[0].width == 10 &&
            plan.strips[0].height == 3,
        "horizontal strip spans the full width"
    );
    require(
        plan.strips[1].left == 0 && plan.strips[1].top == 3 && plan.strips[1].width == 2 &&
            plan.strips[1].height == 5,
        "vertical strip covers only the surviving rows"
    );
}

void check_large_delta_is_full_damage() {
    veyra::ScrollShiftPlan plan{};
    const auto result =
        veyra::resolve_scroll_shift({.width = 10, .height = 8, .delta_x = 10, .delta_y = 1}, plan);

    require(result == veyra::ScrollShiftResult::complete, "overscroll resolves");
    require(!plan.has_blit, "full-extent delta emits no blit");
    require(plan.full_damage_x, "full-extent axis reports full damage");
    require(!plan.full_damage_y, "surviving axis reports no full damage");
    require(
        plan.strip_count == 1 && plan.strips[0].left == 0 && plan.strips[0].top == 0 &&
            plan.strips[0].width == 10 && plan.strips[0].height == 8,
        "overscroll damages the whole area through one strip"
    );

    veyra::ScrollShiftPlan tall_plan{};
    const auto tall_result = veyra::resolve_scroll_shift(
        {.width = 10, .height = 8, .delta_x = 0, .delta_y = -9},
        tall_plan
    );

    require(tall_result == veyra::ScrollShiftResult::complete, "vertical overscroll resolves");
    require(!tall_plan.has_blit, "vertical overscroll emits no blit");
    require(tall_plan.full_damage_y, "overscrolled vertical axis reports full damage");
}

void check_invalid_and_clipped_requests() {
    veyra::ScrollShiftPlan plan{};
    const auto empty_result =
        veyra::resolve_scroll_shift({.width = 0, .height = 8, .delta_x = 1, .delta_y = 0}, plan);

    require(empty_result == veyra::ScrollShiftResult::invalid_input, "zero width is rejected");

    veyra::ScrollShiftPlan clipped{};
    const auto clipped_result = veyra::resolve_scroll_shift(
        {
            .width = 10,
            .height = 8,
            .delta_x = 2,
            .delta_y = 1,
            .has_clip = true,
            .clip = {.left = 2, .top = 1, .width = 5, .height = 4},
        },
        clipped
    );

    require(clipped_result == veyra::ScrollShiftResult::complete, "clipped shift resolves");
    require(
        clipped.source.left == 2 && clipped.source.top == 1 && clipped.source.width == 3 &&
            clipped.source.height == 3,
        "clipped shift reads inside the clip"
    );
    require(
        clipped.destination.left == 4 && clipped.destination.top == 2,
        "clipped shift lands inside the clip"
    );
    require(
        clipped.strip_count == 2 && clipped.strips[0].left == 2 && clipped.strips[0].top == 1 &&
            clipped.strips[0].width == 5 && clipped.strips[0].height == 1 &&
            clipped.strips[1].left == 2 && clipped.strips[1].top == 2 &&
            clipped.strips[1].width == 2 && clipped.strips[1].height == 3,
        "clipped strips stay inside the clip"
    );

    veyra::ScrollShiftPlan outside{};
    const auto outside_result = veyra::resolve_scroll_shift(
        {
            .width = 10,
            .height = 8,
            .delta_x = 2,
            .delta_y = 1,
            .has_clip = true,
            .clip = {.left = 40, .top = 0, .width = 5, .height = 4},
        },
        outside
    );

    require(
        outside_result == veyra::ScrollShiftResult::no_movement,
        "clip outside the surface is a no-op"
    );
}

void check_tiling_covers_every_pixel_once(
    std::size_t width,
    std::size_t height,
    int delta_x,
    int delta_y
) {
    veyra::ScrollShiftPlan plan{};
    const auto result = veyra::resolve_scroll_shift(
        {.width = width, .height = height, .delta_x = delta_x, .delta_y = delta_y},
        plan
    );

    require(result == veyra::ScrollShiftResult::complete, "tiling case resolves");

    for (std::size_t y = 0; y < height; ++y) {
        for (std::size_t x = 0; x < width; ++x) {
            auto coverage = 0;

            if (plan.has_blit && inside_rect(plan.destination, x, y)) {
                ++coverage;
            }

            for (std::size_t index = 0; index < plan.strip_count; ++index) {
                if (inside_rect(plan.strips[index], x, y)) {
                    ++coverage;
                }
            }

            require(coverage == 1, "every pixel is blitted or exposed exactly once");
        }
    }

    if (plan.has_blit) {
        require(
            plan.source.width == plan.destination.width &&
                plan.source.height == plan.destination.height,
            "blit source and destination match"
        );
        require(
            inside_rect({.width = width, .height = height}, plan.source.left, plan.source.top) &&
                inside_rect(
                    {.width = width, .height = height},
                    plan.source.left + plan.source.width - 1,
                    plan.source.top + plan.source.height - 1
                ),
            "blit source stays inside the surface"
        );
    }
}

void check_tiling_sweep() {
    constexpr std::size_t widths[4]{10, 7, 1, 5};
    constexpr std::size_t heights[4]{8, 5, 1, 6};

    for (const auto width : widths) {
        for (const auto height : heights) {
            const auto wide = static_cast<int>(width);
            const auto tall = static_cast<int>(height);
            const int deltas_x[7]{0, 1, -1, wide - 1, -wide + 1, wide, -wide - 2};
            const int deltas_y[7]{0, 2, -2, tall - 1, -tall + 1, tall + 3, -tall};

            for (const auto delta_x : deltas_x) {
                for (const auto delta_y : deltas_y) {
                    if (delta_x == 0 && delta_y == 0) {
                        continue;
                    }

                    check_tiling_covers_every_pixel_once(width, height, delta_x, delta_y);
                }
            }
        }
    }
}

void check_vertical_overlap_copies_bottom_up() {
    constexpr auto width = std::size_t{3};
    constexpr auto height = std::size_t{4};
    std::array<veyra::SoftwarePixel, width * height> pixels{};

    for (std::size_t y = 0; y < height; ++y) {
        for (std::size_t x = 0; x < width; ++x) {
            pixels[y * width + x] = {
                .red = static_cast<std::uint8_t>(y),
                .green = static_cast<std::uint8_t>(x),
            };
        }
    }

    const auto moved = veyra::shift_surface_pixels(
        pixels,
        width,
        width,
        height,
        {.width = width, .height = 3},
        {.top = 1, .width = width, .height = 3}
    );

    require(moved, "overlapping downward shift succeeds");
    require(pixels[0].red == 0, "top row keeps its pixels");

    for (std::size_t y = 1; y < height; ++y) {
        for (std::size_t x = 0; x < width; ++x) {
            require(
                pixels[y * width + x].red == static_cast<std::uint8_t>(y - 1),
                "downward shift copies each row before overwriting it"
            );
            require(
                pixels[y * width + x].green == static_cast<std::uint8_t>(x),
                "downward shift preserves columns"
            );
        }
    }
}

void check_vertical_overlap_copies_top_down() {
    constexpr auto width = std::size_t{3};
    constexpr auto height = std::size_t{4};
    std::array<veyra::SoftwarePixel, width * height> pixels{};

    for (std::size_t y = 0; y < height; ++y) {
        for (std::size_t x = 0; x < width; ++x) {
            pixels[y * width + x] = {
                .red = static_cast<std::uint8_t>(y),
                .green = static_cast<std::uint8_t>(x),
            };
        }
    }

    const auto moved = veyra::shift_surface_pixels(
        pixels,
        width,
        width,
        height,
        {.top = 1, .width = width, .height = 3},
        {.width = width, .height = 3}
    );

    require(moved, "overlapping upward shift succeeds");

    for (std::size_t y = 0; y + 1 < height; ++y) {
        for (std::size_t x = 0; x < width; ++x) {
            require(
                pixels[y * width + x].red == static_cast<std::uint8_t>(y + 1),
                "upward shift pulls each row down before overwriting it"
            );
        }
    }

    require(pixels[3 * width].red == 3, "bottom row keeps its pixels");
}

void check_horizontal_overlap_and_rejections() {
    constexpr auto width = std::size_t{4};
    constexpr auto height = std::size_t{2};
    std::array<veyra::SoftwarePixel, width * height> pixels{};

    for (std::size_t y = 0; y < height; ++y) {
        for (std::size_t x = 0; x < width; ++x) {
            pixels[y * width + x] = {
                .red = static_cast<std::uint8_t>(x),
            };
        }
    }

    const auto before = pixels;
    const auto moved = veyra::shift_surface_pixels(
        pixels,
        width,
        width,
        height,
        {.width = 3, .height = height},
        {.left = 1, .width = 3, .height = height}
    );

    require(moved, "overlapping horizontal shift succeeds");

    for (std::size_t y = 0; y < height; ++y) {
        require(pixels[y * width].red == 0, "first column keeps its pixels");
        require(
            pixels[y * width + 1].red == 0 && pixels[y * width + 2].red == 1 &&
                pixels[y * width + 3].red == 2,
            "rightward shift moves each column before overwriting it"
        );
    }

    const auto same = veyra::shift_surface_pixels(
        pixels,
        width,
        width,
        height,
        {.left = 1, .width = 2, .height = 1},
        {.left = 1, .width = 2, .height = 1}
    );

    require(same, "self copy succeeds");

    const auto snapshot = pixels;
    const auto mismatched = veyra::shift_surface_pixels(
        pixels,
        width,
        width,
        height,
        {.width = 2, .height = 1},
        {.width = 3, .height = 1}
    );

    require(!mismatched, "mismatched rect sizes are rejected");
    require(buffers_equal(pixels, snapshot), "rejected shift leaves pixels untouched");

    const auto outside = veyra::shift_surface_pixels(
        pixels,
        width,
        width,
        height,
        {.left = 3, .width = 2, .height = 1},
        {.width = 2, .height = 1}
    );

    require(!outside, "out-of-bounds source is rejected");
    require(buffers_equal(pixels, snapshot), "out-of-bounds shift leaves pixels untouched");

    const auto empty = veyra::shift_surface_pixels(
        pixels,
        width,
        width,
        height,
        {.width = 0, .height = 1},
        {.width = 0, .height = 1}
    );

    require(!empty, "empty rects are rejected");
    require(before[0].red == 0, "pattern setup starts at zero");
}

void check_blit_matches_full_redraw(int delta_x, int delta_y) {
    constexpr auto width = std::size_t{12};
    constexpr auto height = std::size_t{10};
    constexpr auto stride = std::size_t{12};
    constexpr auto old_x = 15;
    constexpr auto old_y = 14;
    const auto new_x = old_x - delta_x;
    const auto new_y = old_y - delta_y;

    std::array<veyra::SoftwarePixel, width * height> frame_a{};
    std::array<veyra::SoftwarePixel, width * height> frame_b{};
    std::array<veyra::SoftwarePixel, width * height> frame_c{};
    paint_content(frame_a, width, height, stride, old_x, old_y);
    frame_b = frame_a;
    paint_content(frame_c, width, height, stride, new_x, new_y);

    veyra::ScrollShiftPlan plan{};
    const auto result = veyra::resolve_scroll_shift(
        {.width = width, .height = height, .delta_x = delta_x, .delta_y = delta_y},
        plan
    );

    require(result == veyra::ScrollShiftResult::complete, "redraw case resolves");

    if (plan.has_blit) {
        const auto moved = veyra::shift_surface_pixels(
            frame_b,
            stride,
            width,
            height,
            plan.source,
            plan.destination
        );

        require(moved, "redraw case shifts surviving pixels");
    }

    for (std::size_t index = 0; index < plan.strip_count; ++index) {
        repaint_strip(frame_b, stride, plan.strips[index], new_x, new_y);
    }

    for (std::size_t index = 0; index < frame_b.size(); ++index) {
        require(
            pixel_equal(frame_b[index], frame_c[index]),
            "blit plus strip repaint matches a full redraw"
        );
    }
}

void check_blit_matches_full_redraw_sweep() {
    check_blit_matches_full_redraw(3, 0);
    check_blit_matches_full_redraw(-3, 0);
    check_blit_matches_full_redraw(0, 2);
    check_blit_matches_full_redraw(0, -2);
    check_blit_matches_full_redraw(2, 3);
    check_blit_matches_full_redraw(-2, -3);
    check_blit_matches_full_redraw(12, 0);
    check_blit_matches_full_redraw(0, 10);
    check_blit_matches_full_redraw(15, -14);
}

void check_stale_generation_refuses_blit_and_keeps_pixels() {
    constexpr auto width = std::size_t{6};
    constexpr auto height = std::size_t{5};
    std::array<veyra::SoftwarePixel, width * height> pixels{};
    paint_content(pixels, width, height, width, 2, 3);

    const auto snapshot = pixels;

    veyra::ScrollShiftPlan plan{
        .has_blit = true,
        .strip_count = 7,
    };
    const auto stale = veyra::resolve_scroll_shift(
        {
            .width = width,
            .height = height,
            .delta_x = 0,
            .delta_y = 2,
            .current_generation = 8,
            .retained_generation = 7,
        },
        plan
    );

    require(stale == veyra::ScrollShiftResult::stale_generation, "a stale layer is refused");
    require(!plan.has_blit, "a stale resolve emits no blit");
    require(plan.strip_count == 0, "a stale resolve emits no strips");
    require(!plan.full_damage_x && !plan.full_damage_y, "a stale resolve claims no damage");
    require(buffers_equal(pixels, snapshot), "a refused resolve leaves pixels untouched");

    veyra::ScrollShiftPlan fresh_plan{};
    const auto fresh = veyra::resolve_scroll_shift(
        {
            .width = width,
            .height = height,
            .delta_x = 0,
            .delta_y = 2,
            .current_generation = 8,
            .retained_generation = 8,
        },
        fresh_plan
    );

    require(fresh == veyra::ScrollShiftResult::complete, "a matching generation resolves");
    require(fresh_plan.has_blit, "a fresh resolve keeps its blit");

    veyra::ScrollShiftPlan broken_plan{};
    const auto broken = veyra::resolve_scroll_shift(
        {
            .width = 0,
            .height = height,
            .current_generation = 8,
            .retained_generation = 7,
        },
        broken_plan
    );

    require(
        broken == veyra::ScrollShiftResult::invalid_input,
        "bad sizes stay invalid even when stale"
    );
}

void check_stale_generation_wins_over_no_movement() {
    veyra::ScrollShiftPlan plan{};
    const auto result = veyra::resolve_scroll_shift(
        {
            .width = 10,
            .height = 8,
            .delta_x = 0,
            .delta_y = 0,
            .current_generation = 3,
            .retained_generation = 2,
        },
        plan
    );

    require(result == veyra::ScrollShiftResult::stale_generation, "stale wins over no movement");
    require(!plan.has_blit && plan.strip_count == 0, "a stale still frame clears the plan");
}

void check_shift_generation_comparison_survives_wrap() {
    constexpr auto before_wrap = std::numeric_limits<std::uint64_t>::max();

    veyra::ScrollShiftPlan stale_plan{};
    const auto stale = veyra::resolve_scroll_shift(
        {
            .width = 10,
            .height = 8,
            .delta_x = 2,
            .delta_y = 0,
            .current_generation = 0,
            .retained_generation = before_wrap,
        },
        stale_plan
    );

    require(stale == veyra::ScrollShiftResult::stale_generation, "a wrapped bump reads as stale");

    veyra::ScrollShiftPlan fresh_plan{};
    const auto fresh = veyra::resolve_scroll_shift(
        {
            .width = 10,
            .height = 8,
            .delta_x = 2,
            .delta_y = 0,
            .current_generation = before_wrap,
            .retained_generation = before_wrap,
        },
        fresh_plan
    );

    require(fresh == veyra::ScrollShiftResult::complete, "the pre-wrap value still matches");
    require(fresh_plan.has_blit, "a pre-wrap match keeps its blit");
}

void check_strided_shift_preserves_row_padding() {
    constexpr auto width = std::size_t{4};
    constexpr auto height = std::size_t{3};
    constexpr auto stride = std::size_t{6};
    std::array<veyra::SoftwarePixel, stride * height> pixels{};
    paint_content(pixels, width, height, stride, 0, 0);

    const auto before = pixels;
    const auto moved = veyra::shift_surface_pixels(
        pixels,
        stride,
        width,
        height,
        {.width = 3, .height = height},
        {.left = 1, .width = 3, .height = height}
    );

    require(moved, "strided rightward shift succeeds");

    for (std::size_t y = 0; y < height; ++y) {
        require(
            pixel_equal(pixels[y * stride], before[y * stride]),
            "strided shift leaves the uncovered column alone"
        );

        for (std::size_t x = 1; x <= 3; ++x) {
            require(
                pixel_equal(pixels[y * stride + x], before[y * stride + x - 1]),
                "strided shift moves each row through its own stride"
            );
        }

        for (std::size_t x = width; x < stride; ++x) {
            require(
                pixel_equal(pixels[y * stride + x], before[y * stride + x]),
                "strided shift never touches row padding"
            );
        }
    }
}

void check_strips_drive_damage_render() {
    constexpr auto width = std::size_t{8};
    constexpr auto height = std::size_t{6};
    std::array<veyra::SoftwarePixel, width * height> full_pixels{};
    std::array<veyra::SoftwarePixel, width * height> damaged_pixels{};
    const std::array<veyra::DisplayCommand, 1> commands{
        veyra::FillRect{
            .bounds = {.width = static_cast<float>(width), .height = static_cast<float>(height)},
            .color = {.red = 90, .green = 40, .blue = 200},
        },
    };
    const auto full_surface = veyra::SoftwareSurface{
        .width = width,
        .height = height,
        .stride = width,
        .pixels = full_pixels,
    };
    const auto damaged_surface = veyra::SoftwareSurface{
        .width = width,
        .height = height,
        .stride = width,
        .pixels = damaged_pixels,
    };

    require(
        veyra::render_software(commands, full_surface, {}) == veyra::SoftwareRenderResult::complete,
        "reference frame renders"
    );

    veyra::ScrollShiftPlan plan{};
    const auto shift_result = veyra::resolve_scroll_shift(
        {.width = width, .height = height, .delta_x = 2, .delta_y = 1},
        plan
    );

    require(shift_result == veyra::ScrollShiftResult::complete, "damage case resolves");
    require(plan.strip_count == 2, "damage case exposes two strips");

    std::array<veyra::LayoutRect, 2> storage{};
    veyra::DamageTracker tracker(width, height, storage);
    require(tracker.valid(), "damage tracker accepts caller storage");

    for (std::size_t index = 0; index < plan.strip_count; ++index) {
        const auto strip = veyra::LayoutRect{
            .x = static_cast<float>(plan.strips[index].left),
            .y = static_cast<float>(plan.strips[index].top),
            .width = static_cast<float>(plan.strips[index].width),
            .height = static_cast<float>(plan.strips[index].height),
        };

        require(tracker.add(strip), "scroll strip feeds the damage tracker");
    }

    require(tracker.regions().size() == 2, "disjoint strips stay separate regions");
    require(
        veyra::render_software(commands, damaged_surface, {}, tracker.regions()) ==
            veyra::SoftwareRenderResult::complete,
        "strip damage replays through the renderer"
    );

    for (std::size_t y = 0; y < height; ++y) {
        for (std::size_t x = 0; x < width; ++x) {
            const auto index = y * width + x;

            if (inside_rect(plan.strips[0], x, y) || inside_rect(plan.strips[1], x, y)) {
                require(
                    pixel_equal(damaged_pixels[index], full_pixels[index]),
                    "damaged strips match the full render"
                );
            } else {
                require(
                    pixel_equal(damaged_pixels[index], veyra::SoftwarePixel{}),
                    "pixels outside the strips stay untouched"
                );
            }
        }
    }
}

}

int main() {
    try {
        check_zero_delta_is_no_movement();
        check_horizontal_geometry();
        check_vertical_geometry();
        check_diagonal_geometry();
        check_large_delta_is_full_damage();
        check_invalid_and_clipped_requests();
        check_tiling_sweep();
        check_vertical_overlap_copies_bottom_up();
        check_vertical_overlap_copies_top_down();
        check_horizontal_overlap_and_rejections();
        check_blit_matches_full_redraw_sweep();
        check_stale_generation_refuses_blit_and_keeps_pixels();
        check_stale_generation_wins_over_no_movement();
        check_shift_generation_comparison_survives_wrap();
        check_strided_shift_preserves_row_padding();
        check_strips_drive_damage_render();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
