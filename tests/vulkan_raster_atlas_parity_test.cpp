#include "allocation_counter.hpp"
#include "veyra/render/software_renderer.hpp"
#include "veyra/render/vulkan_context.hpp"
#include "veyra/render/vulkan_rect_pipeline.hpp"
#include "veyra/text/freetype_rasterizer.hpp"
#include "veyra/text/glyph_run.hpp"
#include "veyra/text/shaper.hpp"

#include "vulkan_parity_helpers.hpp"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <new>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

using veyra::vulkan_parity::check_parity;
using veyra::vulkan_parity::render_reference;
using veyra::vulkan_parity::require;
using veyra::vulkan_parity::SoftwareRooms;

constexpr std::size_t kWidth = 96;
constexpr std::size_t kHeight = 72;
constexpr std::size_t kGlyphs = 3;
constexpr std::size_t kAtlasWidth = 32;
constexpr std::size_t kAtlasHeight = 32;

hb_bool_t map_codepoint(hb_font_t*, void*, hb_codepoint_t codepoint, hb_codepoint_t* glyph, void*) {
    *glyph = codepoint;
    return true;
}

class SyntheticFont {
  public:
    SyntheticFont() :
        font_(hb_font_create(hb_face_get_empty()), hb_font_destroy),
        functions_(hb_font_funcs_create(), hb_font_funcs_destroy) {
        hb_font_funcs_set_nominal_glyph_func(functions_.get(), map_codepoint, nullptr, nullptr);
        hb_font_funcs_make_immutable(functions_.get());
        hb_font_set_funcs(font_.get(), functions_.get(), nullptr, nullptr);
        hb_font_set_scale(font_.get(), 1000, 1000);
    }

    hb_font_t* get() const noexcept {
        return font_.get();
    }

  private:
    std::unique_ptr<hb_font_t, decltype(&hb_font_destroy)> font_;
    std::unique_ptr<hb_font_funcs_t, decltype(&hb_font_funcs_destroy)> functions_;
};

struct RasterAtlas {
    std::array<veyra::GlyphAtlasSlot, 4> slots{};
    std::array<std::uint8_t, kAtlasWidth * kAtlasHeight> coverage{};
    std::array<stbrp_node, kAtlasWidth> nodes{};
    std::array<veyra::ShapedGlyph, kGlyphs> glyphs{};
    std::size_t glyph_count{};
};

void rasterize_into_atlas(veyra::GlyphAtlas& atlas) {
    auto gray_source = std::array<FT_Byte, 35>{};

    for (auto index = std::size_t{}; index < gray_source.size(); ++index) {
        gray_source[index] = static_cast<FT_Byte>(64 + (index % 5) * 48);
    }

    const FT_Bitmap gray_bitmap{
        .rows = 7,
        .width = 5,
        .pitch = 5,
        .buffer = gray_source.data(),
        .num_grays = 256,
        .pixel_mode = FT_PIXEL_MODE_GRAY,
    };

    auto gray_pixels = std::array<std::uint8_t, 35>{};
    auto gray_mask = veyra::GlyphMask{};

    require(
        veyra::copy_freetype_bitmap(
            gray_bitmap,
            0,
            6,
            veyra::GlyphRasterMode::grayscale,
            std::span<std::uint8_t>(gray_pixels),
            5,
            gray_mask
        ) == veyra::GlyphRasterizeResult::complete,
        "gray bitmap rasterizes"
    );
    require(
        atlas.insert({.face_id = 7, .glyph_id = 97, .pixel_size = 16}, gray_mask) ==
            veyra::GlyphAtlasResult::inserted,
        "rasterized gray glyph packs"
    );

    auto mono_source =
        std::array<FT_Byte, 10>{0xF0, 0xF0, 0xF0, 0xF0, 0xF0, 0x00, 0x00, 0x00, 0x00, 0x00};

    const FT_Bitmap mono_bitmap{
        .rows = 5,
        .width = 9,
        .pitch = -2,
        .buffer = mono_source.data() + 8,
        .num_grays = 2,
        .pixel_mode = FT_PIXEL_MODE_MONO,
    };

    auto mono_pixels = std::array<std::uint8_t, 45>{};
    auto mono_mask = veyra::GlyphMask{};

    require(
        veyra::copy_freetype_bitmap(
            mono_bitmap,
            1,
            7,
            veyra::GlyphRasterMode::monochrome,
            std::span<std::uint8_t>(mono_pixels),
            9,
            mono_mask
        ) == veyra::GlyphRasterizeResult::complete,
        "negative-pitch mono bitmap rasterizes"
    );
    require(
        atlas.insert({.face_id = 7, .glyph_id = 98, .pixel_size = 16}, mono_mask) ==
            veyra::GlyphAtlasResult::inserted,
        "rasterized mono glyph packs with its own stride"
    );

    auto padded_source = std::array<FT_Byte, 48>{};

    for (auto index = std::size_t{}; index < padded_source.size(); ++index) {
        padded_source[index] = static_cast<FT_Byte>(32 + (index % 4) * 64);
    }

    const FT_Bitmap padded_bitmap{
        .rows = 6,
        .width = 4,
        .pitch = 8,
        .buffer = padded_source.data(),
        .num_grays = 256,
        .pixel_mode = FT_PIXEL_MODE_GRAY,
    };

    auto padded_pixels = std::array<std::uint8_t, 24>{};
    auto padded_mask = veyra::GlyphMask{};

    require(
        veyra::copy_freetype_bitmap(
            padded_bitmap,
            -1,
            5,
            veyra::GlyphRasterMode::grayscale,
            std::span<std::uint8_t>(padded_pixels),
            4,
            padded_mask
        ) == veyra::GlyphRasterizeResult::complete,
        "padded gray bitmap rasterizes"
    );
    require(
        atlas.insert({.face_id = 7, .glyph_id = 99, .pixel_size = 16}, padded_mask) ==
            veyra::GlyphAtlasResult::inserted,
        "rasterized padded glyph packs"
    );
}

void check_raster_atlas_parity(
    veyra::VulkanContext& context,
    veyra::VulkanRectPipeline& pipeline,
    std::span<veyra::SoftwarePixel> expected,
    std::span<veyra::SoftwarePixel> actual,
    SoftwareRooms& rooms
) {
    auto font = SyntheticFont{};
    auto fixtures = RasterAtlas{};
    auto atlas = veyra::GlyphAtlas(
        std::span<veyra::GlyphAtlasSlot>(fixtures.slots),
        std::span<std::uint8_t>(fixtures.coverage),
        std::span<stbrp_node>(fixtures.nodes),
        kAtlasWidth,
        kAtlasHeight
    );

    require(atlas.valid(), "test atlas validates");
    rasterize_into_atlas(atlas);

    auto shaper = veyra::TextShaper(64);

    require(
        shaper.shape_utf8(
            font.get(),
            "abc",
            {.direction = veyra::TextDirection::left_to_right, .script = "Latn"},
            std::span<veyra::ShapedGlyph>(fixtures.glyphs),
            fixtures.glyph_count
        ) == veyra::ShapeResult::complete,
        "synthetic font shapes ascii"
    );
    require(fixtures.glyph_count == kGlyphs, "every codepoint shapes one glyph");

    for (auto& glyph : fixtures.glyphs) {
        glyph.x_advance = 600;
    }

    auto run_commands = std::array<veyra::DisplayCommand, kGlyphs * 2>{};
    auto command_count = std::size_t{};

    require(
        veyra::build_glyph_atlas_draws(
            std::span<const veyra::ShapedGlyph>(fixtures.glyphs).first(fixtures.glyph_count),
            atlas,
            {.face_id = 7, .pixel_size = 16},
            {.x = 8.0F, .y = 24.0F},
            62.5,
            62.5,
            {.red = 20, .green = 20, .blue = 24, .alpha = 255},
            std::span<veyra::DisplayCommand>(run_commands).first(kGlyphs),
            command_count
        ) == veyra::GlyphRunResult::complete,
        "rasterized glyphs lower to atlas draws"
    );
    require(command_count == kGlyphs, "opaque run emits one draw per glyph");

    require(
        veyra::build_glyph_atlas_draws(
            std::span<const veyra::ShapedGlyph>(fixtures.glyphs).first(fixtures.glyph_count),
            atlas,
            {.face_id = 7, .pixel_size = 16},
            {.x = 8.0F, .y = 24.0F},
            62.5,
            62.5,
            {.red = 210, .green = 60, .blue = 40, .alpha = 160},
            std::span<veyra::DisplayCommand>(run_commands).last(kGlyphs),
            command_count
        ) == veyra::GlyphRunResult::complete,
        "translucent raster run overlaps the opaque run"
    );
    require(command_count == kGlyphs, "translucent run emits one draw per glyph");

    const std::array<veyra::DisplayCommand, 7> storage{
        veyra::FillRect{
            .bounds = {.width = static_cast<float>(kWidth), .height = static_cast<float>(kHeight)},
            .color = {.red = 240, .green = 240, .blue = 244, .alpha = 255},
        },
        run_commands[0],
        run_commands[1],
        run_commands[2],
        run_commands[3],
        run_commands[4],
        run_commands[5],
    };

    const auto scene = std::span<const veyra::DisplayCommand>{storage.data(), storage.size()};

    render_reference(scene, expected, kWidth, kHeight, rooms);

    require(
        expected[21 * kWidth + 20].red > 40 && expected[21 * kWidth + 20].red < 200,
        "raster overlap blends both runs"
    );

    auto stats = veyra::VulkanRectStats{};

    require(
        veyra::render_vulkan_rects(context, pipeline, scene, actual, stats) ==
            veyra::VulkanRenderResult::complete,
        "raster atlas runs render on the gpu path"
    );
    require(stats.submitted == 7, "every raster glyph draw submits once");
    require(stats.glyph_draws == 1, "one atlas batches the whole run");

    check_parity(context, pipeline, scene, expected, actual, "raster-atlas", 1);
}

int run() {
    auto context = veyra::VulkanContext{};

    require(
        veyra::create_vulkan_context(context, kWidth, kHeight) == veyra::VulkanContextResult::ready,
        "vulkan context comes up"
    );

    auto pipeline = veyra::VulkanRectPipeline{};

    require(
        veyra::create_vulkan_rect_pipeline(context, pipeline) ==
            veyra::VulkanRenderResult::complete,
        "glyph-capable pipeline comes up"
    );

    auto rooms = SoftwareRooms{};
    auto expected = std::array<veyra::SoftwarePixel, kWidth * kHeight>{};
    auto actual = std::array<veyra::SoftwarePixel, kWidth * kHeight>{};

    check_raster_atlas_parity(
        context,
        pipeline,
        std::span<veyra::SoftwarePixel>{expected.data(), expected.size()},
        std::span<veyra::SoftwarePixel>{actual.data(), actual.size()},
        rooms
    );

    veyra::destroy_vulkan_rect_pipeline(context, pipeline);
    veyra::destroy_vulkan_context(context);

    require(!veyra::vulkan_context_ready(context), "context tears down");

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
