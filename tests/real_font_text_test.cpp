#include "veyra/render/software_renderer.hpp"
#include "veyra/render/vulkan_context.hpp"
#include "veyra/render/vulkan_rect_pipeline.hpp"
#include "veyra/text/freetype_rasterizer.hpp"
#include "veyra/text/glyph_run.hpp"
#include "veyra/text/shaped_run_cache.hpp"
#include "veyra/text/shaper.hpp"

#include "vulkan_parity_helpers.hpp"

#include <hb-ot.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

using veyra::vulkan_parity::check_parity;
using veyra::vulkan_parity::render_reference;
using veyra::vulkan_parity::require;
using veyra::vulkan_parity::SoftwareRooms;

using BlobPtr = std::unique_ptr<hb_blob_t, decltype(&hb_blob_destroy)>;
using FacePtr = std::unique_ptr<hb_face_t, decltype(&hb_face_destroy)>;
using FontPtr = std::unique_ptr<hb_font_t, decltype(&hb_font_destroy)>;

constexpr std::size_t kWidth = 192;
constexpr std::size_t kHeight = 144;
constexpr std::size_t kAtlasWidth = 64;
constexpr std::size_t kAtlasHeight = 64;
constexpr std::size_t kAtlasSlots = 64;
constexpr std::size_t kMaskWidth = 32;
constexpr std::size_t kMaskHeight = 32;
constexpr std::uint32_t kArabicFace = 7;
constexpr std::uint32_t kCjkFace = 8;
constexpr std::uint32_t kPixelSize = 16;

struct RealFont {
    BlobPtr blob{nullptr, hb_blob_destroy};
    FacePtr face{nullptr, hb_face_destroy};
    FontPtr font{nullptr, hb_font_destroy};
    FT_Library library{};
    FT_Face ft_face{};
    unsigned int units_per_em{};
};

bool load_font(const char* path, RealFont& loaded) {
    loaded.blob.reset(hb_blob_create_from_file_or_fail(path));
    if (!loaded.blob) {
        return false;
    }

    loaded.face.reset(hb_face_create(loaded.blob.get(), 0));
    if (!loaded.face || hb_face_get_glyph_count(loaded.face.get()) == 0) {
        return false;
    }

    loaded.font.reset(hb_font_create(loaded.face.get()));
    if (!loaded.font) {
        return false;
    }

    hb_ot_font_set_funcs(loaded.font.get());
    loaded.units_per_em = hb_face_get_upem(loaded.face.get());
    if (loaded.units_per_em == 0) {
        return false;
    }

    const auto scale = static_cast<int>(loaded.units_per_em);
    hb_font_set_scale(loaded.font.get(), scale, scale);

    if (FT_Init_FreeType(&loaded.library) != 0) {
        return false;
    }

    if (FT_New_Face(loaded.library, path, 0, &loaded.ft_face) != 0) {
        return false;
    }

    if (FT_Set_Pixel_Sizes(loaded.ft_face, kPixelSize, kPixelSize) != 0) {
        return false;
    }

    return true;
}

void close_font(RealFont& loaded) noexcept {
    if (loaded.ft_face != nullptr) {
        FT_Done_Face(loaded.ft_face);
    }

    if (loaded.library != nullptr) {
        FT_Done_FreeType(loaded.library);
    }
}

struct ShapedRun {
    std::array<veyra::ShapedGlyph, 16> glyphs{};
    std::size_t count{};
};

ShapedRun shape_run(
    veyra::TextShaper& shaper,
    hb_font_t* font,
    std::string_view text,
    veyra::TextDirection direction,
    std::string_view script
) {
    ShapedRun run{};

    require(
        shaper.shape_utf8(
            font,
            text,
            {.direction = direction, .script = script},
            std::span<veyra::ShapedGlyph>(run.glyphs),
            run.count
        ) == veyra::ShapeResult::complete,
        "real font shapes its run"
    );
    require(run.count > 0, "shaped runs are never empty");

    return run;
}

std::uint32_t cluster_glyph(const ShapedRun& run, std::uint32_t cluster) {
    for (const auto& glyph : std::span<const veyra::ShapedGlyph>(run.glyphs).first(run.count)) {
        if (glyph.cluster == cluster) {
            return glyph.id;
        }
    }

    require(false, "every cluster resolves to a glyph");

    return 0;
}

void check_arabic_joining(veyra::TextShaper& shaper, RealFont& arabic) {
    const auto isolated =
        shape_run(shaper, arabic.font.get(), "س", veyra::TextDirection::right_to_left, "Arab");
    require(isolated.count == 1, "isolated seen shapes to one glyph");

    const auto word =
        shape_run(shaper, arabic.font.get(), "سلام", veyra::TextDirection::right_to_left, "Arab");
    require(word.count <= 4, "lam-alef never grows the run");

    require(
        cluster_glyph(word, 0) != isolated.glyphs[0].id,
        "word-initial seen differs from its isolated form"
    );

    for (const auto& glyph : std::span<const veyra::ShapedGlyph>(word.glyphs).first(word.count)) {
        require(glyph.x_advance != 0, "shaped glyphs advance the pen");
        require(glyph.cluster < 8, "clusters stay inside the source word");
    }
}

ShapedRun check_cjk_shaping(veyra::TextShaper& shaper, RealFont& cjk) {
    const auto run =
        shape_run(shaper, cjk.font.get(), "一丁七丂", veyra::TextDirection::left_to_right, "Hani");
    require(run.count == 4, "four han characters shape to four glyphs");

    for (auto index = std::size_t{}; index < run.count; ++index) {
        require(run.glyphs[index].x_advance > 0, "han glyphs advance forward");

        for (auto other = 0; other < index; ++other) {
            require(
                run.glyphs[other].id != run.glyphs[index].id,
                "distinct han characters shape distinctly"
            );
        }
    }

    return run;
}

struct AtlasFixtures {
    std::array<veyra::GlyphAtlasSlot, kAtlasSlots> slots{};
    std::array<std::uint8_t, kAtlasWidth * kAtlasHeight> coverage{};
    std::array<stbrp_node, kAtlasWidth> nodes{};
    std::array<std::uint8_t, kMaskWidth * kMaskHeight> mask_pixels{};
};

void insert_real_glyph(
    veyra::GlyphAtlas& atlas,
    FT_Face face,
    std::uint32_t font_id,
    std::uint32_t glyph_id,
    std::span<std::uint8_t> scratch
) {
    veyra::GlyphMask glyph{};

    require(
        veyra::rasterize_freetype_glyph(
            face,
            glyph_id,
            veyra::GlyphRasterMode::grayscale,
            scratch,
            kMaskWidth,
            glyph
        ) == veyra::GlyphRasterizeResult::complete,
        "real outlines rasterize"
    );
    require(glyph.mask.width > 0 && glyph.mask.height > 0, "rasterized glyphs cover pixels");

    auto inked = false;

    for (auto row = std::size_t{}; row < glyph.mask.height && !inked; ++row) {
        for (auto column = std::size_t{}; column < glyph.mask.width; ++column) {
            if (glyph.mask.coverage[row * glyph.mask.stride + column] != 0) {
                inked = true;
                break;
            }
        }
    }

    require(inked, "rasterized glyphs leave ink");

    const auto inserted = atlas.insert(
        veyra::GlyphMaskKey{
            .face_id = font_id,
            .glyph_id = glyph_id,
            .pixel_size = kPixelSize,
        },
        glyph
    );

    require(
        inserted == veyra::GlyphAtlasResult::inserted ||
            inserted == veyra::GlyphAtlasResult::updated,
        "real glyphs pack into the atlas"
    );
}

std::size_t build_run(
    veyra::GlyphAtlas& atlas,
    const ShapedRun& run,
    veyra::GlyphMaskKey key,
    double units_per_pixel,
    veyra::LayoutPoint baseline,
    std::span<veyra::DisplayCommand> commands
) {
    auto command_count = std::size_t{};

    require(
        veyra::build_glyph_atlas_draws(
            std::span<const veyra::ShapedGlyph>(run.glyphs).first(run.count),
            atlas,
            key,
            baseline,
            units_per_pixel,
            units_per_pixel,
            {.red = 20, .green = 20, .blue = 24, .alpha = 255},
            commands,
            command_count
        ) == veyra::GlyphRunResult::complete,
        "real runs lower to atlas draws"
    );

    return command_count;
}

void check_real_font_parity(
    veyra::VulkanContext& context,
    veyra::VulkanRectPipeline& pipeline,
    std::span<veyra::SoftwarePixel> expected,
    std::span<veyra::SoftwarePixel> actual,
    SoftwareRooms& rooms
) {
    const std::string arabic_path = std::string(VEYRA_TESTDATA_DIR) + "/fonts/latin-arabic.ttf";
    const std::string cjk_path = std::string(VEYRA_TESTDATA_DIR) + "/fonts/cjk-sample.ttf";

    auto arabic = RealFont{};
    require(load_font(arabic_path.c_str(), arabic), "arabic test font loads");

    auto cjk = RealFont{};
    const auto cjk_loaded = load_font(cjk_path.c_str(), cjk);

    require(cjk_loaded, "cjk test font loads");

    auto shaper = veyra::TextShaper(256);
    check_arabic_joining(shaper, arabic);
    const auto cjk_run = check_cjk_shaping(shaper, cjk);

    const auto arabic_word =
        shape_run(shaper, arabic.font.get(), "سلام", veyra::TextDirection::right_to_left, "Arab");

    auto fixtures = AtlasFixtures{};
    auto atlas = veyra::GlyphAtlas(
        std::span<veyra::GlyphAtlasSlot>(fixtures.slots),
        std::span<std::uint8_t>(fixtures.coverage),
        std::span<stbrp_node>(fixtures.nodes),
        kAtlasWidth,
        kAtlasHeight
    );

    require(atlas.valid(), "real-font atlas validates");

    for (const auto& glyph :
         std::span<const veyra::ShapedGlyph>(arabic_word.glyphs).first(arabic_word.count)) {
        insert_real_glyph(
            atlas,
            arabic.ft_face,
            kArabicFace,
            glyph.id,
            std::span<std::uint8_t>(fixtures.mask_pixels)
        );
    }

    for (const auto& glyph :
         std::span<const veyra::ShapedGlyph>(cjk_run.glyphs).first(cjk_run.count)) {
        insert_real_glyph(
            atlas,
            cjk.ft_face,
            kCjkFace,
            glyph.id,
            std::span<std::uint8_t>(fixtures.mask_pixels)
        );
    }

    const auto arabic_scale = static_cast<double>(arabic.units_per_em) / kPixelSize;
    const auto cjk_scale = static_cast<double>(cjk.units_per_em) / kPixelSize;

    auto run_commands = std::array<veyra::DisplayCommand, 32>{};

    const auto arabic_count = build_run(
        atlas,
        arabic_word,
        {.face_id = kArabicFace, .pixel_size = kPixelSize},
        arabic_scale,
        {.x = 120.0F, .y = 48.0F},
        std::span<veyra::DisplayCommand>(run_commands)
    );

    const auto cjk_count = build_run(
        atlas,
        cjk_run,
        {.face_id = kCjkFace, .pixel_size = kPixelSize},
        cjk_scale,
        {.x = 8.0F, .y = 96.0F},
        std::span<veyra::DisplayCommand>(run_commands).subspan(arabic_count)
    );

    const std::array<veyra::DisplayCommand, 9> storage{
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
        run_commands[6],
        run_commands[7],
    };

    const auto scene = std::span<const veyra::DisplayCommand>{
        storage.data(),
        1 + arabic_count + cjk_count,
    };

    render_reference(scene, expected, kWidth, kHeight, rooms);

    auto inked = false;

    for (const auto pixel : expected) {
        if (pixel.red < 150) {
            inked = true;
            break;
        }
    }

    require(inked, "real runs ink the reference");

    auto stats = veyra::VulkanRectStats{};

    require(
        veyra::render_vulkan_rects(context, pipeline, scene, actual, stats) ==
            veyra::VulkanRenderResult::complete,
        "real runs render on the gpu path"
    );
    require(stats.glyph_draws >= 1, "real runs batch through the glyph pipeline");
    require(stats.uploads >= 1, "real runs upload their atlas");

    check_parity(context, pipeline, scene, expected, actual, "real-font", 2);

    close_font(cjk);
    close_font(arabic);
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

    check_real_font_parity(
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
