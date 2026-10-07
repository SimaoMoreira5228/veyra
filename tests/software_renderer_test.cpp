#include "veyra/render/software_renderer.hpp"

#include <array>
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

bool pixel_is(
    const veyra::SoftwarePixel& pixel,
    std::uint8_t red,
    std::uint8_t green,
    std::uint8_t blue,
    std::uint8_t alpha = 255
) {
    return pixel.red == red && pixel.green == green && pixel.blue == blue && pixel.alpha == alpha;
}

void check_fills_and_nested_clips() {
    constexpr std::size_t width = 5;
    constexpr std::size_t height = 4;
    constexpr std::size_t stride = 7;
    const auto initial = veyra::SoftwarePixel{
        .red = 1,
        .green = 2,
        .blue = 3,
        .alpha = 4,
    };
    std::array<veyra::SoftwarePixel, stride * height> pixels;
    pixels.fill(initial);
    const std::array<veyra::DisplayCommand, 7> commands{
        veyra::FillRect{
            .bounds = {.x = -1.0F, .y = -1.0F, .width = 7.0F, .height = 6.0F},
            .color = {.red = 10, .green = 20, .blue = 30},
        },
        veyra::PushClip{
            .bounds = {.x = 1.0F, .y = 1.0F, .width = 3.0F, .height = 2.0F},
        },
        veyra::FillRect{
            .bounds = {.x = 0.0F, .y = 0.0F, .width = 5.0F, .height = 4.0F},
            .color = {.red = 40, .green = 50, .blue = 60},
        },
        veyra::PushClip{
            .bounds = {.x = 2.0F, .y = 0.0F, .width = 2.0F, .height = 3.0F},
        },
        veyra::FillRect{
            .bounds = {.x = 0.0F, .y = 0.0F, .width = 5.0F, .height = 4.0F},
            .color = {.red = 70, .green = 80, .blue = 90},
        },
        veyra::PopClip{},
        veyra::PopClip{},
    };
    std::array<veyra::SoftwareClipRect, 2> clips{};
    const auto surface = veyra::SoftwareSurface{
        .width = width,
        .height = height,
        .stride = stride,
        .pixels = pixels,
    };
    const auto workspace = veyra::SoftwareRenderWorkspace{.clips = clips};

    const auto result = veyra::render_software(commands, surface, workspace);
    require(result == veyra::SoftwareRenderResult::complete, "nested clip list renders");
    require(pixel_is(pixels[0], 10, 20, 30), "surface edge receives the first fill");
    require(pixel_is(pixels[1 * stride + 1], 40, 50, 60), "outer clip receives its fill");
    require(pixel_is(pixels[1 * stride + 2], 70, 80, 90), "nested clip intersects its parent");
    require(pixel_is(pixels[2 * stride + 3], 70, 80, 90), "nested clip covers its full bounds");
    require(pixel_is(pixels[2 * stride + 1], 40, 50, 60), "pop restores the parent clip");
    require(
        pixel_is(pixels[3 * stride], 10, 20, 30),
        "pixels outside clips preserve earlier fills"
    );
    require(pixel_is(pixels[5], 1, 2, 3, 4), "row padding remains untouched");
    require(pixel_is(pixels[stride + 6], 1, 2, 3, 4), "each row uses the supplied stride");
    require(pixel_is(pixels[0], 10, 20, 30, 255), "filled pixels are opaque RGBA");
}

void check_damage_regions() {
    constexpr std::size_t width = 4;
    constexpr std::size_t height = 3;
    std::array<veyra::SoftwarePixel, width * height> pixels;
    const auto initial = veyra::SoftwarePixel{
        .red = 1,
        .green = 2,
        .blue = 3,
    };
    pixels.fill(initial);
    const std::array<veyra::DisplayCommand, 1> commands{
        veyra::FillRect{
            .bounds = {.width = 4.0F, .height = 3.0F},
            .color = {.red = 10, .green = 20, .blue = 30},
        },
    };
    const std::array damage{
        veyra::LayoutRect{.x = 1.0F, .y = 1.0F, .width = 2.0F, .height = 1.0F},
    };
    const auto surface = veyra::SoftwareSurface{
        .width = width,
        .height = height,
        .stride = width,
        .pixels = pixels,
    };

    const auto result = veyra::render_software(commands, surface, {}, damage);
    require(result == veyra::SoftwareRenderResult::complete, "damage-limited rendering completes");
    require(pixel_is(pixels[width + 1], 10, 20, 30), "damage region is redrawn");
    require(pixel_is(pixels[width + 2], 10, 20, 30), "damage includes its final column");
    require(pixel_is(pixels[0], 1, 2, 3), "pixels outside damage remain unchanged");
    require(pixel_is(pixels[2 * width + 1], 1, 2, 3), "damage excludes its final row");

    const std::span<const veyra::LayoutRect> no_damage;
    require(
        veyra::render_software(commands, surface, {}, no_damage) ==
            veyra::SoftwareRenderResult::complete,
        "empty damage performs no drawing"
    );
    require(pixel_is(pixels[0], 1, 2, 3), "empty damage leaves pixels unchanged");
}

void check_damage_replay_matches_full_render() {
    constexpr auto width = std::size_t{4};
    constexpr auto height = std::size_t{2};
    const std::array<std::uint8_t, width * height> coverage{
        0,
        64,
        128,
        255,
        255,
        128,
        64,
        0,
    };
    const auto mask = veyra::SoftwareMask{
        .width = width,
        .height = height,
        .stride = width,
        .coverage = coverage,
    };
    const std::array<veyra::DisplayCommand, 4> commands{
        veyra::FillRect{
            .bounds = {.width = static_cast<float>(width), .height = static_cast<float>(height)},
            .color = {.red = 12, .green = 34, .blue = 56},
        },
        veyra::PushClip{
            .bounds = {.x = 1.0F, .width = 2.0F, .height = static_cast<float>(height)},
        },
        veyra::DrawMask{
            .mask = &mask,
            .color = {.red = 210, .green = 120, .blue = 30, .alpha = 180},
        },
        veyra::PopClip{},
    };
    const std::array damage_regions{
        veyra::LayoutRect{.width = 2.0F, .height = static_cast<float>(height)},
        veyra::LayoutRect{.x = 2.0F, .width = 2.0F, .height = static_cast<float>(height)},
    };
    std::array<veyra::SoftwarePixel, width * height> full_pixels{};
    std::array<veyra::SoftwarePixel, width * height> damaged_pixels{};
    std::array<veyra::SoftwareClipRect, 1> full_clips{};
    std::array<veyra::SoftwareClipRect, 1> damaged_clips{};
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

    const auto full_result = veyra::render_software(commands, full_surface, {.clips = full_clips});
    const auto damaged_result =
        veyra::render_software(commands, damaged_surface, {.clips = damaged_clips}, damage_regions);
    require(full_result == veyra::SoftwareRenderResult::complete, "full mask render completes");
    require(
        damaged_result == veyra::SoftwareRenderResult::complete,
        "disjoint damage mask render completes"
    );
    for (std::size_t index = 0; index < full_pixels.size(); ++index) {
        const auto& expected = full_pixels[index];
        require(
            pixel_is(
                damaged_pixels[index],
                expected.red,
                expected.green,
                expected.blue,
                expected.alpha
            ),
            "damage replay matches the full clipped mask render"
        );
    }
}

void check_fractional_bounds() {
    std::array<veyra::SoftwarePixel, 4> pixels{};
    const std::array<veyra::DisplayCommand, 1> commands{
        veyra::FillRect{
            .bounds = {.x = 0.5F, .y = 0.5F, .width = 1.0F, .height = 1.0F},
            .color = {.red = 10, .green = 20, .blue = 30},
        },
    };
    const auto surface = veyra::SoftwareSurface{
        .width = 2,
        .height = 2,
        .stride = 2,
        .pixels = pixels,
    };

    const auto result = veyra::render_software(commands, surface, {});
    require(result == veyra::SoftwareRenderResult::complete, "fractional rectangle renders");
    for (const auto& pixel : pixels) {
        require(pixel_is(pixel, 10, 20, 30), "fractional bounds cover touched pixels");
    }

    pixels.fill(veyra::SoftwarePixel{});
    const std::array<veyra::DisplayCommand, 1> zero_width{
        veyra::FillRect{
            .bounds = {.x = 0.5F, .width = 0.0F, .height = 2.0F},
            .color = {.red = 10, .green = 20, .blue = 30},
        },
    };
    const auto zero_width_result = veyra::render_software(zero_width, surface, {});
    require(zero_width_result == veyra::SoftwareRenderResult::complete, "empty fill is valid");
    for (const auto& pixel : pixels) {
        require(pixel_is(pixel, 0, 0, 0), "zero-width fill covers no pixels");
    }
}

void check_alpha_compositing() {
    std::array<veyra::SoftwarePixel, 4> pixels{
        veyra::SoftwarePixel{
            .red = 20,
            .green = 40,
            .blue = 60,
            .alpha = 128,
        },
        veyra::SoftwarePixel{
            .red = 11,
            .green = 22,
            .blue = 33,
            .alpha = 44,
        },
        veyra::SoftwarePixel{
            .red = 20,
            .green = 40,
            .blue = 60,
            .alpha = 255,
        },
        veyra::SoftwarePixel{
            .alpha = 0,
        },
    };
    const std::array<veyra::DisplayCommand, 4> commands{
        veyra::FillRect{
            .bounds = {.width = 1.0F, .height = 1.0F},
            .color = {.red = 200, .green = 100, .blue = 0, .alpha = 128},
        },
        veyra::FillRect{
            .bounds = {.x = 1.0F, .width = 1.0F, .height = 1.0F},
            .color = {.red = 255, .green = 0, .blue = 0, .alpha = 0},
        },
        veyra::FillRect{
            .bounds = {.x = 2.0F, .width = 1.0F, .height = 1.0F},
            .color = {.red = 200, .green = 100, .blue = 0, .alpha = 128},
        },
        veyra::FillRect{
            .bounds = {.x = 3.0F, .width = 1.0F, .height = 1.0F},
            .color = {.red = 200, .green = 100, .blue = 0, .alpha = 128},
        },
    };
    const auto surface = veyra::SoftwareSurface{
        .width = 4,
        .height = 1,
        .stride = 4,
        .pixels = pixels,
    };

    const auto result = veyra::render_software(commands, surface, {});
    require(result == veyra::SoftwareRenderResult::complete, "translucent fills render");
    require(pixel_is(pixels[0], 140, 80, 20, 192), "source-over blends RGBA channels");
    require(pixel_is(pixels[1], 11, 22, 33, 44), "fully transparent fill preserves destination");
    require(pixel_is(pixels[2], 110, 70, 30, 255), "source-over blends opaque destinations");
    require(pixel_is(pixels[3], 200, 100, 0, 128), "transparent destination takes source color");
}

void check_mask_coverage_and_clipping() {
    constexpr auto width = std::size_t{3};
    constexpr auto height = std::size_t{2};
    constexpr auto stride = std::size_t{4};
    std::array<veyra::SoftwarePixel, width * height> pixels;
    pixels.fill(veyra::SoftwarePixel{.red = 10, .green = 20, .blue = 30});
    const std::array<std::uint8_t, stride * height> coverage{
        0,
        128,
        255,
        17,
        255,
        0,
        128,
        23,
    };
    const auto mask = veyra::SoftwareMask{
        .width = 3,
        .height = 2,
        .stride = stride,
        .coverage = coverage,
    };
    const std::array<veyra::DisplayCommand, 1> commands{
        veyra::DrawMask{
            .x = -1,
            .mask = &mask,
            .color = {.red = 0, .green = 0, .blue = 255},
        },
    };
    const auto surface = veyra::SoftwareSurface{
        .width = width,
        .height = height,
        .stride = width,
        .pixels = pixels,
    };

    const auto result = veyra::render_software(commands, surface, {});
    require(result == veyra::SoftwareRenderResult::complete, "coverage mask renders");
    require(pixel_is(pixels[0], 5, 10, 143), "partial coverage blends the mask tint");
    require(pixel_is(pixels[1], 0, 0, 255), "full coverage paints the mask tint");
    require(pixel_is(pixels[2], 10, 20, 30), "mask bounds leave pixels outside the mask alone");
    require(pixel_is(pixels[width], 10, 20, 30), "zero coverage preserves the destination");
    require(pixel_is(pixels[width + 1], 5, 10, 143), "mask stride skips row padding");

    pixels.fill(veyra::SoftwarePixel{.red = 10, .green = 20, .blue = 30});
    const std::array<veyra::DisplayCommand, 3> clipped_commands{
        veyra::PushClip{
            .bounds = {.x = 1.0F, .width = 1.0F, .height = 2.0F},
        },
        commands[0],
        veyra::PopClip{},
    };
    std::array<veyra::SoftwareClipRect, 1> clips{};
    const auto result_with_clip =
        veyra::render_software(clipped_commands, surface, {.clips = clips});
    require(result_with_clip == veyra::SoftwareRenderResult::complete, "mask respects clips");
    require(pixel_is(pixels[0], 10, 20, 30), "clip skips mask pixels outside its bounds");
    require(pixel_is(pixels[1], 0, 0, 255), "clip retains covered mask pixels");
}

void check_stroke_rect_edges_and_clipping() {
    constexpr auto width = std::size_t{5};
    constexpr auto height = std::size_t{5};
    const auto background = veyra::SoftwarePixel{.red = 20, .green = 40, .blue = 60};
    std::array<veyra::SoftwarePixel, width * height> pixels;
    pixels.fill(background);
    const std::array<veyra::DisplayCommand, 1> commands{
        veyra::StrokeRect{
            .bounds = {.x = 0.25F, .y = 0.25F, .width = 4.5F, .height = 4.5F},
            .thickness = 1.0F,
            .color = {.red = 200, .green = 100, .blue = 0, .alpha = 128},
        },
    };
    const auto surface = veyra::SoftwareSurface{
        .width = width,
        .height = height,
        .stride = width,
        .pixels = pixels,
    };

    const auto result = veyra::render_software(commands, surface, {});
    require(result == veyra::SoftwareRenderResult::complete, "fractional border renders");
    require(pixel_is(pixels[0], 110, 70, 30), "border corner blends once");
    require(pixel_is(pixels[2], 110, 70, 30), "border top edge renders");
    require(pixel_is(pixels[width * 2 + 2], 20, 40, 60), "border leaves its interior alone");

    pixels.fill(background);
    const std::array<veyra::DisplayCommand, 3> clipped_commands{
        veyra::PushClip{
            .bounds = {.width = 1.0F, .height = static_cast<float>(height)},
        },
        commands[0],
        veyra::PopClip{},
    };
    std::array<veyra::SoftwareClipRect, 1> clips{};
    const auto clipped_result = veyra::render_software(clipped_commands, surface, {.clips = clips});
    require(clipped_result == veyra::SoftwareRenderResult::complete, "clipped border renders");
    for (std::size_t y = 0; y < height; ++y) {
        require(pixel_is(pixels[y * width], 110, 70, 30), "clip retains the border column");
        require(pixel_is(pixels[y * width + 1], 20, 40, 60), "clip skips the next column");
    }

    pixels.fill(background);
    const std::array<veyra::DisplayCommand, 1> zero_thickness{
        veyra::StrokeRect{
            .bounds = {.width = static_cast<float>(width), .height = static_cast<float>(height)},
            .thickness = 0.0F,
            .color = {.red = 255, .green = 0, .blue = 0},
        },
    };
    const auto empty_border_result = veyra::render_software(zero_thickness, surface, {});
    require(
        empty_border_result == veyra::SoftwareRenderResult::complete,
        "zero-thickness border is valid"
    );
    for (const auto& pixel : pixels) {
        require(pixel_is(pixel, 20, 40, 60), "zero-thickness border draws nothing");
    }
}

void check_image_alpha_clipping_and_stride() {
    constexpr auto width = std::size_t{3};
    constexpr auto height = std::size_t{2};
    constexpr auto image_stride = std::size_t{5};
    const std::array<veyra::SoftwarePixel, image_stride * height> image_pixels{
        veyra::SoftwarePixel{.red = 255, .alpha = 0},
        veyra::SoftwarePixel{.red = 255, .alpha = 128},
        veyra::SoftwarePixel{.green = 255},
        veyra::SoftwarePixel{.blue = 255},
        veyra::SoftwarePixel{.red = 255, .green = 255, .blue = 255},
        veyra::SoftwarePixel{.red = 255, .green = 255},
        veyra::SoftwarePixel{.red = 255, .blue = 255, .alpha = 0},
        veyra::SoftwarePixel{.red = 255, .green = 255, .blue = 255},
        veyra::SoftwarePixel{.blue = 255, .alpha = 128},
        veyra::SoftwarePixel{.red = 255, .green = 255, .blue = 255},
    };
    const auto image = veyra::SoftwareImage{
        .width = 4,
        .height = height,
        .stride = image_stride,
        .pixels = image_pixels,
    };
    const std::array<veyra::DisplayCommand, 1> commands{
        veyra::DrawImage{
            .source =
                {
                    .width = 4.0F,
                    .height = static_cast<float>(height),
                },
            .destination =
                {
                    .x = -1.0F,
                    .width = 4.0F,
                    .height = static_cast<float>(height),
                },
            .image = &image,
        },
    };
    const auto background = veyra::SoftwarePixel{.red = 10, .green = 20, .blue = 30};
    std::array<veyra::SoftwarePixel, width * height> pixels;
    pixels.fill(background);
    const auto surface = veyra::SoftwareSurface{
        .width = width,
        .height = height,
        .stride = width,
        .pixels = pixels,
    };

    const auto result = veyra::render_software(commands, surface, {});
    require(result == veyra::SoftwareRenderResult::complete, "RGBA image renders");
    require(pixel_is(pixels[0], 133, 10, 15), "image alpha blends with the destination");
    require(pixel_is(pixels[1], 0, 255, 0), "opaque image pixels replace the destination");
    require(pixel_is(pixels[2], 0, 0, 255), "cropped image edges use source coordinates");
    require(
        pixel_is(pixels[width], 10, 20, 30),
        "transparent image pixels preserve the destination"
    );
    require(pixel_is(pixels[width + 1], 255, 255, 255), "image stride skips row padding");
    require(pixel_is(pixels[width + 2], 5, 10, 143), "partial image alpha is composited");

    pixels.fill(background);
    const std::array<veyra::DisplayCommand, 3> clipped_commands{
        veyra::PushClip{
            .bounds = {.x = 1.0F, .width = 1.0F, .height = static_cast<float>(height)},
        },
        commands[0],
        veyra::PopClip{},
    };
    std::array<veyra::SoftwareClipRect, 1> clips{};
    const auto clipped_result = veyra::render_software(clipped_commands, surface, {.clips = clips});
    require(clipped_result == veyra::SoftwareRenderResult::complete, "clipped image renders");
    require(pixel_is(pixels[0], 10, 20, 30), "clip skips image pixels outside its bounds");
    require(pixel_is(pixels[1], 0, 255, 0), "clip retains the image pixel inside its bounds");
    require(pixel_is(pixels[2], 10, 20, 30), "clip stops image drawing after its bounds");
}

void check_overlapping_damage_is_rejected() {
    std::array<veyra::SoftwarePixel, 3> pixels{
        veyra::SoftwarePixel{.red = 1, .green = 2, .blue = 3},
        veyra::SoftwarePixel{.red = 4, .green = 5, .blue = 6},
        veyra::SoftwarePixel{.red = 7, .green = 8, .blue = 9},
    };
    const auto before = pixels;
    const std::array<veyra::DisplayCommand, 1> commands{
        veyra::FillRect{
            .bounds = {.width = 3.0F, .height = 1.0F},
            .color = {.red = 100, .green = 110, .blue = 120, .alpha = 128},
        },
    };
    const std::array overlapping_damage{
        veyra::LayoutRect{.x = 0.25F, .y = 0.25F, .width = 0.5F, .height = 0.5F},
        veyra::LayoutRect{.x = 0.75F, .y = 0.25F, .width = 0.5F, .height = 0.5F},
    };
    const auto surface = veyra::SoftwareSurface{
        .width = 3,
        .height = 1,
        .stride = 3,
        .pixels = pixels,
    };

    const auto result = veyra::render_software(commands, surface, {}, overlapping_damage);
    require(result == veyra::SoftwareRenderResult::invalid_input, "overlapping damage is rejected");
    for (std::size_t index = 0; index < pixels.size(); ++index) {
        require(
            pixel_is(
                pixels[index],
                before[index].red,
                before[index].green,
                before[index].blue,
                before[index].alpha
            ),
            "overlapping damage rejection leaves the surface unchanged"
        );
    }
}

void check_invalid_input_is_transactional() {
    constexpr std::size_t width = 3;
    constexpr std::size_t height = 2;
    const auto initial = veyra::SoftwarePixel{
        .red = 5,
        .green = 6,
        .blue = 7,
    };
    std::array<veyra::SoftwarePixel, width * height> pixels;
    pixels.fill(initial);
    const auto surface = veyra::SoftwareSurface{
        .width = width,
        .height = height,
        .stride = width,
        .pixels = pixels,
    };
    const std::array<veyra::DisplayCommand, 1> valid_fill{
        veyra::FillRect{
            .bounds = {.width = 3.0F, .height = 2.0F},
            .color = {.red = 10, .green = 20, .blue = 30},
        },
    };
    const std::array<veyra::DisplayCommand, 2> unmatched_pop{
        veyra::PopClip{},
        valid_fill[0],
    };
    const std::array<veyra::DisplayCommand, 2> unclosed_push{
        veyra::PushClip{
            .bounds = {.width = 3.0F, .height = 2.0F},
        },
        valid_fill[0],
    };
    const std::array<veyra::DisplayCommand, 3> clipped_fill{
        veyra::PushClip{
            .bounds = {.width = 3.0F, .height = 2.0F},
        },
        valid_fill[0],
        veyra::PopClip{},
    };
    const std::array<veyra::DisplayCommand, 1> invalid_bounds{
        veyra::FillRect{
            .bounds = {.width = -1.0F, .height = 2.0F},
            .color = {.red = 10, .green = 20, .blue = 30},
        },
    };
    const std::array<veyra::DisplayCommand, 1> invalid_stroke{
        veyra::StrokeRect{
            .bounds = {.width = 3.0F, .height = 2.0F},
            .thickness = -1.0F,
        },
    };
    const std::array<veyra::DisplayCommand, 1> invalid_mask{
        veyra::DrawMask{
            .mask = nullptr,
        },
    };
    const auto invalid_mask_storage = veyra::SoftwareMask{
        .width = 2,
        .height = 2,
        .stride = 1,
        .coverage = {},
    };
    const std::array<veyra::DisplayCommand, 1> short_mask{
        veyra::DrawMask{
            .mask = &invalid_mask_storage,
        },
    };
    const std::array<veyra::DisplayCommand, 1> invalid_image{
        veyra::DrawImage{},
    };
    const std::array<veyra::SoftwarePixel, 1> invalid_image_pixels{};
    const auto invalid_image_storage = veyra::SoftwareImage{
        .width = 2,
        .height = 1,
        .stride = 1,
        .pixels = invalid_image_pixels,
    };
    const std::array<veyra::DisplayCommand, 1> short_image{
        veyra::DrawImage{
            .image = &invalid_image_storage,
        },
    };
    std::array<veyra::SoftwareClipRect, 1> clips{};
    const auto workspace = veyra::SoftwareRenderWorkspace{.clips = clips};
    const auto untouched = [&pixels] {
        for (const auto& pixel : pixels) {
            if (!pixel_is(pixel, 5, 6, 7)) {
                return false;
            }
        }
        return true;
    };

    require(
        veyra::render_software(unmatched_pop, surface, workspace) ==
            veyra::SoftwareRenderResult::invalid_input,
        "unmatched clip pop is rejected"
    );
    require(untouched(), "invalid clip stack leaves the surface untouched");
    require(
        veyra::render_software(unclosed_push, surface, workspace) ==
            veyra::SoftwareRenderResult::invalid_input,
        "unclosed clip push is rejected"
    );
    require(untouched(), "unclosed clip stack leaves the surface untouched");
    require(
        veyra::render_software(invalid_bounds, surface, workspace) ==
            veyra::SoftwareRenderResult::invalid_input,
        "negative rectangle extent is rejected"
    );
    require(untouched(), "invalid geometry leaves the surface untouched");
    require(
        veyra::render_software(invalid_stroke, surface, workspace) ==
            veyra::SoftwareRenderResult::invalid_input,
        "negative border thickness is rejected"
    );
    require(untouched(), "invalid border leaves the surface untouched");
    require(
        veyra::render_software(invalid_mask, surface, workspace) ==
            veyra::SoftwareRenderResult::invalid_input,
        "null mask storage is rejected"
    );
    require(untouched(), "null mask leaves the surface untouched");
    require(
        veyra::render_software(short_mask, surface, workspace) ==
            veyra::SoftwareRenderResult::invalid_input,
        "short mask storage is rejected"
    );
    require(untouched(), "invalid mask storage leaves the surface untouched");
    require(
        veyra::render_software(invalid_image, surface, workspace) ==
            veyra::SoftwareRenderResult::invalid_input,
        "null image storage is rejected"
    );
    require(untouched(), "null image leaves the surface untouched");
    require(
        veyra::render_software(short_image, surface, workspace) ==
            veyra::SoftwareRenderResult::invalid_input,
        "short image storage is rejected"
    );
    require(untouched(), "invalid image storage leaves the surface untouched");
    require(
        veyra::render_software(clipped_fill, surface, {}) ==
            veyra::SoftwareRenderResult::insufficient_workspace,
        "short clip scratch is reported before rendering"
    );
    require(untouched(), "short clip scratch leaves the surface untouched");

    const std::array invalid_damage{
        veyra::LayoutRect{.x = 0.0F, .y = 0.0F, .width = 3.0F, .height = -1.0F},
    };
    require(
        veyra::render_software(valid_fill, surface, workspace, invalid_damage) ==
            veyra::SoftwareRenderResult::invalid_input,
        "negative damage extent is rejected"
    );
    require(untouched(), "invalid damage leaves the surface untouched");

    const auto short_surface = veyra::SoftwareSurface{
        .width = width,
        .height = height,
        .stride = width,
        .pixels = std::span<veyra::SoftwarePixel>(pixels).first(pixels.size() - 1),
    };
    require(
        veyra::render_software(valid_fill, short_surface, workspace) ==
            veyra::SoftwareRenderResult::invalid_surface,
        "short pixel storage is rejected"
    );
    require(untouched(), "invalid surface storage leaves the available pixels untouched");
}

}

int main() {
    try {
        check_fills_and_nested_clips();
        check_damage_regions();
        check_damage_replay_matches_full_render();
        check_fractional_bounds();
        check_alpha_compositing();
        check_stroke_rect_edges_and_clipping();
        check_mask_coverage_and_clipping();
        check_image_alpha_clipping_and_stride();
        check_overlapping_damage_is_rejected();
        check_invalid_input_is_transactional();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
