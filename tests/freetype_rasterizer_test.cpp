#include "veyra/text/rendering/freetype_rasterizer.hpp"

#include <array>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

void require(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

void check_grayscale_rows_and_bearings() {
    std::array<FT_Byte, 6> source{0, 1, 99, 2, 3, 99};
    const FT_Bitmap bitmap{
        .rows = 2,
        .width = 2,
        .pitch = 3,
        .buffer = source.data(),
        .num_grays = 4,
        .pixel_mode = FT_PIXEL_MODE_GRAY,
    };
    std::array<std::uint8_t, 6> output{};
    veyra::GlyphMask glyph;

    const auto result = veyra::copy_freetype_bitmap(
        bitmap,
        -2,
        11,
        veyra::GlyphRasterMode::grayscale,
        output,
        3,
        glyph
    );

    require(result == veyra::GlyphRasterizeResult::complete, "grayscale bitmap copies");
    require(glyph.mask.width == 2 && glyph.mask.height == 2, "bitmap dimensions are retained");
    require(glyph.mask.stride == 3, "caller output pitch is retained");
    require(glyph.bearing_x == -2 && glyph.bearing_y == 11, "bitmap bearings are retained");
    require(
        output[0] == 0 && output[1] == 85 && output[3] == 170 && output[4] == 255,
        "gray levels expand to 8-bit coverage and source padding is skipped"
    );
}

void check_monochrome_negative_pitch() {
    std::array<FT_Byte, 4> source{0x80, 0x80, 0x40, 0x00};
    const FT_Bitmap bitmap{
        .rows = 2,
        .width = 9,
        .pitch = -2,
        .buffer = source.data() + 2,
        .num_grays = 2,
        .pixel_mode = FT_PIXEL_MODE_MONO,
    };
    std::array<std::uint8_t, 18> output{};
    veyra::GlyphMask glyph;

    const auto result = veyra::copy_freetype_bitmap(
        bitmap,
        0,
        8,
        veyra::GlyphRasterMode::monochrome,
        output,
        9,
        glyph
    );

    require(result == veyra::GlyphRasterizeResult::complete, "monochrome bitmap copies");
    require(
        output[0] == 0 && output[1] == 255 && output[8] == 0 && output[9] == 255 &&
            output[17] == 255,
        "negative pitch rows and most-significant-first pixels are handled"
    );
}

void check_rejections_leave_outputs_unchanged() {
    std::array<FT_Byte, 4> source{0, 1, 2, 3};
    const FT_Bitmap bitmap{
        .rows = 2,
        .width = 2,
        .pitch = 2,
        .buffer = source.data(),
        .num_grays = 256,
        .pixel_mode = FT_PIXEL_MODE_GRAY,
    };
    std::array<std::uint8_t, 4> output{7, 7, 7, 7};
    const std::array<std::uint8_t, 1> existing_coverage{42};
    veyra::GlyphMask glyph{
        .mask =
            {
                .width = 1,
                .height = 1,
                .stride = 1,
                .coverage = existing_coverage,
                .generation = 0,
            },
        .bearing_x = 5,
        .bearing_y = 6,
    };

    const auto too_small = veyra::copy_freetype_bitmap(
        bitmap,
        0,
        0,
        veyra::GlyphRasterMode::grayscale,
        output,
        1,
        glyph
    );
    require(too_small == veyra::GlyphRasterizeResult::glyph_too_large, "short output is rejected");
    require(
        output[0] == 7 && output[1] == 7 && output[2] == 7 && output[3] == 7,
        "rejected bitmap leaves output bytes unchanged"
    );
    require(
        glyph.mask.width == 1 && glyph.bearing_x == 5 && glyph.bearing_y == 6,
        "rejected bitmap leaves glyph metadata unchanged"
    );

    auto unsupported = bitmap;
    unsupported.pixel_mode = FT_PIXEL_MODE_BGRA;
    require(
        veyra::copy_freetype_bitmap(
            unsupported,
            0,
            0,
            veyra::GlyphRasterMode::grayscale,
            output,
            2,
            glyph
        ) == veyra::GlyphRasterizeResult::unsupported_bitmap,
        "colored bitmaps are rejected"
    );

    auto short_pitch = bitmap;
    short_pitch.pitch = 1;
    require(
        veyra::copy_freetype_bitmap(
            short_pitch,
            0,
            0,
            veyra::GlyphRasterMode::grayscale,
            output,
            2,
            glyph
        ) == veyra::GlyphRasterizeResult::unsupported_bitmap,
        "bitmap rows shorter than their pixel width are rejected"
    );

    auto invalid_gray_range = bitmap;
    invalid_gray_range.num_grays = 1;
    require(
        veyra::copy_freetype_bitmap(
            invalid_gray_range,
            0,
            0,
            veyra::GlyphRasterMode::grayscale,
            output,
            2,
            glyph
        ) == veyra::GlyphRasterizeResult::unsupported_bitmap,
        "grayscale bitmaps need at least two levels"
    );

    auto negative_rows = bitmap;
    negative_rows.rows = -1;
    negative_rows.width = 0;
    negative_rows.pitch = 0;
    require(
        veyra::copy_freetype_bitmap(
            negative_rows,
            0,
            0,
            veyra::GlyphRasterMode::grayscale,
            output,
            0,
            glyph
        ) == veyra::GlyphRasterizeResult::invalid_input,
        "negative row counts are rejected"
    );
    require(
        output[0] == 7 && output[1] == 7 && output[2] == 7 && output[3] == 7,
        "negative rows leave output bytes unchanged"
    );
    require(
        glyph.mask.width == 1 && glyph.bearing_x == 5 && glyph.bearing_y == 6,
        "negative rows leave glyph metadata unchanged"
    );

    auto negative_width = bitmap;
    negative_width.width = -1;
    require(
        veyra::copy_freetype_bitmap(
            negative_width,
            0,
            0,
            veyra::GlyphRasterMode::grayscale,
            output,
            2,
            glyph
        ) == veyra::GlyphRasterizeResult::glyph_too_large,
        "wrapped widths do not fit the output"
    );

    require(
        veyra::rasterize_freetype_glyph(
            nullptr,
            1,
            veyra::GlyphRasterMode::grayscale,
            output,
            2,
            glyph
        ) == veyra::GlyphRasterizeResult::invalid_input,
        "null font faces are rejected"
    );
}

void check_color_bitmap_conversion_and_rendering() {
    std::array<FT_Byte, 12> source{
        0,
        0,
        128,
        128,
        0,
        255,
        0,
        255,
        255,
        255,
        255,
        0,
    };
    const FT_Bitmap bitmap{
        .rows = 1,
        .width = 3,
        .pitch = 12,
        .buffer = source.data(),
        .pixel_mode = FT_PIXEL_MODE_BGRA,
    };
    const auto sentinel = veyra::SoftwarePixel{.red = 7, .green = 8, .blue = 9, .alpha = 10};
    std::array<veyra::SoftwarePixel, 4> output;
    output.fill(sentinel);
    veyra::GlyphColorImage glyph;

    const auto result = veyra::copy_freetype_color_bitmap(bitmap, -1, 12, output, 4, glyph);
    require(result == veyra::GlyphRasterizeResult::complete, "color bitmap copies");
    require(
        glyph.image.width == 3 && glyph.image.height == 1 && glyph.image.stride == 4,
        "color image dimensions and stride are retained"
    );
    require(glyph.bearing_x == -1 && glyph.bearing_y == 12, "color image bearings are retained");
    require(
        output[0].red == 255 && output[0].alpha == 128,
        "premultiplied half-alpha red becomes straight RGBA"
    );
    require(output[1].green == 255 && output[1].alpha == 255, "BGRA channel order is converted");
    require(
        output[2].red == 0 && output[2].green == 0 && output[2].blue == 0 && output[2].alpha == 0,
        "fully transparent color pixels discard premultiplied channels"
    );
    require(output[3].red == sentinel.red, "output row padding remains untouched");

    std::array<veyra::SoftwarePixel, 3> surface_pixels{};
    surface_pixels.fill({.alpha = 0});
    const veyra::SoftwareSurface surface{
        .width = 3,
        .height = 1,
        .stride = 3,
        .pixels = surface_pixels,
    };
    const std::array<veyra::DisplayCommand, 1> commands{
        veyra::DrawImage{
            .source =
                {
                    .width = static_cast<float>(glyph.image.width),
                    .height = static_cast<float>(glyph.image.height),
                },
            .destination =
                {
                    .width = static_cast<float>(glyph.image.width),
                    .height = static_cast<float>(glyph.image.height),
                },
            .image = &glyph.image,
        },
    };
    require(
        veyra::render_software(commands, surface, {}) == veyra::SoftwareRenderResult::complete,
        "converted color glyph draws through the software renderer"
    );
    require(
        surface_pixels[0].red == 255 && surface_pixels[0].alpha == 128,
        "the rendered color glyph keeps its transparency"
    );
    require(surface_pixels[2].alpha == 0, "transparent color pixels leave the surface clear");

    const auto unchanged = glyph;
    std::array<veyra::SoftwarePixel, 1> short_output{sentinel};
    require(
        veyra::copy_freetype_color_bitmap(bitmap, 0, 0, short_output, 2, glyph) ==
            veyra::GlyphRasterizeResult::glyph_too_large,
        "short color output is rejected"
    );
    require(glyph.bearing_x == unchanged.bearing_x, "rejected copy leaves metadata unchanged");
    require(short_output[0].red == sentinel.red, "rejected copy leaves pixels unchanged");

    auto grayscale = bitmap;
    grayscale.pixel_mode = FT_PIXEL_MODE_GRAY;
    require(
        veyra::copy_freetype_color_bitmap(grayscale, 0, 0, output, 4, glyph) ==
            veyra::GlyphRasterizeResult::unsupported_bitmap,
        "non-color bitmaps are rejected"
    );

    auto negative_color_rows = bitmap;
    negative_color_rows.rows = -1;
    negative_color_rows.width = 0;
    negative_color_rows.pitch = 0;
    require(
        veyra::copy_freetype_color_bitmap(negative_color_rows, 0, 0, output, 0, glyph) ==
            veyra::GlyphRasterizeResult::invalid_input,
        "negative color row counts are rejected"
    );
    require(output[0].red == 255, "negative color rows leave pixels unchanged");
    require(glyph.image.width == 3, "negative color rows leave metadata unchanged");
    require(
        veyra::rasterize_freetype_color_glyph(nullptr, 1, output, 4, glyph) ==
            veyra::GlyphRasterizeResult::invalid_input,
        "null color glyph faces are rejected"
    );

    std::array<FT_Byte, 8> reversed_rows{0, 0, 255, 255, 255, 0, 0, 255};
    const FT_Bitmap negative_pitch_bitmap{
        .rows = 2,
        .width = 1,
        .pitch = -4,
        .buffer = reversed_rows.data() + 4,
        .pixel_mode = FT_PIXEL_MODE_BGRA,
    };
    std::array<veyra::SoftwarePixel, 2> negative_pitch_output{};
    require(
        veyra::copy_freetype_color_bitmap(
            negative_pitch_bitmap,
            0,
            0,
            negative_pitch_output,
            1,
            glyph
        ) == veyra::GlyphRasterizeResult::complete,
        "negative-pitch color bitmap copies"
    );
    require(
        negative_pitch_output[0].blue == 255 && negative_pitch_output[1].red == 255,
        "negative-pitch color rows are copied in their FreeType order"
    );
}

void check_color_bitmap_edge_cases() {
    std::array<FT_Byte, 20> source{
        5, 6, 7, 255, 99, 99, 99, 99, 99, 99, 8, 9, 10, 255, 99, 99, 99, 99, 99, 99,
    };
    const FT_Bitmap padded{
        .rows = 2,
        .width = 1,
        .pitch = 10,
        .buffer = source.data(),
        .num_grays = 0,
        .pixel_mode = FT_PIXEL_MODE_BGRA,
    };
    std::array<veyra::SoftwarePixel, 4> output{};
    const auto pad = veyra::SoftwarePixel{.red = 9, .green = 8, .blue = 7, .alpha = 6};
    output.fill(pad);
    veyra::GlyphColorImage glyph;

    require(
        veyra::copy_freetype_color_bitmap(padded, 3, 9, output, 2, glyph) ==
            veyra::GlyphRasterizeResult::complete,
        "padded color rows copy without reading the gray count"
    );
    require(
        output[0].red == 7 && output[0].green == 6 && output[0].blue == 5 && output[0].alpha == 255,
        "first padded color row converts BGRA order"
    );
    require(
        output[2].red == 10 && output[2].green == 9 && output[2].blue == 8 &&
            output[2].alpha == 255,
        "second padded color row skips source padding"
    );
    require(
        output[1].red == pad.red && output[1].alpha == pad.alpha && output[3].blue == pad.blue &&
            output[3].alpha == pad.alpha,
        "output padding stays untouched"
    );
    require(glyph.bearing_x == 3 && glyph.bearing_y == 9, "padded color bearings are retained");

    const auto accepted = glyph;
    const auto sentinel = veyra::SoftwarePixel{.red = 1, .green = 2, .blue = 3, .alpha = 4};
    std::array<veyra::SoftwarePixel, 4> guarded;
    guarded.fill(sentinel);

    auto null_buffer = padded;
    null_buffer.buffer = nullptr;
    require(
        veyra::copy_freetype_color_bitmap(null_buffer, 0, 0, guarded, 2, glyph) ==
            veyra::GlyphRasterizeResult::unsupported_bitmap,
        "null color buffer with nonzero size is rejected"
    );

    auto short_rows = padded;
    short_rows.pitch = 3;
    require(
        veyra::copy_freetype_color_bitmap(short_rows, 0, 0, guarded, 2, glyph) ==
            veyra::GlyphRasterizeResult::unsupported_bitmap,
        "color rows shorter than their pixel width are rejected"
    );
    require(
        guarded[0].red == sentinel.red && guarded[1].alpha == sentinel.alpha &&
            guarded[2].blue == sentinel.blue && guarded[3].green == sentinel.green,
        "rejected color copies leave pixels unchanged"
    );
    require(
        glyph.bearing_x == accepted.bearing_x && glyph.image.width == accepted.image.width,
        "rejected color copies leave metadata unchanged"
    );
}

void check_bitmap_cache_render_path() {
    std::array<FT_Byte, 4> source{0, 255, 128, 255};
    const FT_Bitmap bitmap{
        .rows = 2,
        .width = 2,
        .pitch = 2,
        .buffer = source.data(),
        .num_grays = 256,
        .pixel_mode = FT_PIXEL_MODE_GRAY,
    };
    std::array<std::uint8_t, 4> raster_storage{};
    veyra::GlyphMask rasterized;
    require(
        veyra::copy_freetype_bitmap(
            bitmap,
            1,
            1,
            veyra::GlyphRasterMode::grayscale,
            raster_storage,
            2,
            rasterized
        ) == veyra::GlyphRasterizeResult::complete,
        "FreeType bitmap rasterizes"
    );

    std::array<veyra::GlyphMaskCacheSlot, 4> slots{};
    std::array<std::uint8_t, 16> cache_storage{};
    veyra::GlyphMaskCache cache(slots, cache_storage, 2, 2);
    const veyra::GlyphMaskKey key{
        .face_id = 1,
        .glyph_id = 7,
        .pixel_size = 12,
    };
    require(
        cache.insert(key, rasterized) == veyra::GlyphMaskCacheResult::inserted,
        "rasterized mask enters the cache"
    );

    const auto glyph = cache.find(key);
    if (!glyph.has_value()) {
        throw std::runtime_error("cached glyph is found");
    }
    std::array<veyra::SoftwarePixel, 16> pixels{};
    pixels.fill({.alpha = 0});
    const veyra::SoftwareSurface surface{
        .width = 4,
        .height = 4,
        .stride = 4,
        .pixels = pixels,
    };
    std::array<veyra::SoftwareClipRect, 1> clips{};
    const veyra::SoftwareRenderWorkspace workspace{.clips = clips};
    const std::array<veyra::DisplayCommand, 1> commands{
        veyra::DrawMask{
            .x = static_cast<float>(1 + glyph->bearing_x),
            .y = static_cast<float>(3 - glyph->bearing_y),
            .mask = &glyph->mask,
            .color = {.red = 240, .green = 10, .blue = 20, .alpha = 255},
        },
    };

    require(
        veyra::render_software(commands, surface, workspace) ==
            veyra::SoftwareRenderResult::complete,
        "cached glyph draws through the software renderer"
    );
    require(pixels[2 * 4 + 2].alpha == 0, "transparent glyph coverage leaves the surface clear");
    require(pixels[2 * 4 + 3].alpha == 255, "full glyph coverage paints the surface");
    require(pixels[3 * 4 + 2].alpha == 128, "partial glyph coverage blends with the surface");
}

void check_measure_rejects_null_face() {
    auto bounds = veyra::GlyphBounds{.width = 7, .height = 7, .bearing_x = 7, .bearing_y = 7};
    require(
        veyra::measure_freetype_glyph_bounds(
            nullptr,
            65,
            veyra::GlyphRasterMode::grayscale,
            bounds
        ) == veyra::GlyphRasterizeResult::invalid_input,
        "a null face never measures"
    );
    require(
        bounds.width == 7 && bounds.height == 7 && bounds.bearing_x == 7 && bounds.bearing_y == 7,
        "a rejected measure leaves its output unchanged"
    );
}

}

int main() {
    try {
        check_grayscale_rows_and_bearings();
        check_monochrome_negative_pitch();
        check_rejections_leave_outputs_unchanged();
        check_color_bitmap_conversion_and_rendering();
        check_color_bitmap_edge_cases();
        check_bitmap_cache_render_path();
        check_measure_rejects_null_face();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
