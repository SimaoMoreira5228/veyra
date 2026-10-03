#include "veyra/text/freetype_rasterizer.hpp"

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
            .x = 1 + glyph->bearing_x,
            .y = 3 - glyph->bearing_y,
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

}

int main() {
    try {
        check_grayscale_rows_and_bearings();
        check_monochrome_negative_pitch();
        check_rejections_leave_outputs_unchanged();
        check_bitmap_cache_render_path();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
