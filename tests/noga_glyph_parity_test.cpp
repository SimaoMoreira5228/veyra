#include "allocation_counter.hpp"
#include "veyra/render/noga_frame.hpp"
#include "veyra/render/noga_rect.hpp"
#include "veyra/render/software_renderer.hpp"
#include "veyra/text/glyph_run.hpp"
#include "veyra/text/shaper.hpp"

#include "noga_parity_helpers.hpp"

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

using veyra::noga_parity::check_parity;
using veyra::noga_parity::render_reference;
using veyra::noga_parity::require;
using veyra::noga_parity::SoftwareRooms;

constexpr std::size_t kWidth = 96;
constexpr std::size_t kHeight = 72;
constexpr std::size_t kGlyphsPerRun = 3;
constexpr std::size_t kRuns = 4;

hb_bool_t map_codepoint(hb_font_t*, void*, hb_codepoint_t codepoint, hb_codepoint_t* glyph, void*) {
    *glyph = codepoint;
    return true;
}

hb_position_t fixed_advance(hb_font_t*, void*, hb_codepoint_t, void*) {
    return 600;
}

class SyntheticFont {
  public:
    SyntheticFont() :
        font_(hb_font_create(hb_face_get_empty()), hb_font_destroy),
        functions_(hb_font_funcs_create(), hb_font_funcs_destroy) {
        hb_font_funcs_set_nominal_glyph_func(functions_.get(), map_codepoint, nullptr, nullptr);
        hb_font_funcs_set_glyph_h_advance_func(functions_.get(), fixed_advance, nullptr, nullptr);
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

struct GlyphFixtures {
    std::array<veyra::GlyphMaskCacheSlot, 4> slots{};
    std::array<std::uint8_t, 256> coverage{};
    std::array<veyra::ShapedGlyph, kGlyphsPerRun> glyphs{};
    std::size_t glyph_count{};
};

void insert_glyph_masks(veyra::GlyphMaskCache& cache) {
    auto pattern = std::array<std::uint8_t, 35>{};

    for (auto index = std::size_t{}; index < pattern.size(); ++index) {
        const auto column = index % 5;
        pattern[index] = static_cast<std::uint8_t>(64 + column * 48);
    }

    auto key = veyra::GlyphMaskKey{
        .face_id = 7,
        .pixel_size = 16,
    };

    const std::array<std::uint32_t, 3> ids{97, 98, 99};

    for (auto slot = std::size_t{}; slot < ids.size(); ++slot) {
        key.glyph_id = ids[slot];

        const auto mask = veyra::GlyphMask{
            .mask =
                {
                    .width = 5,
                    .height = 7,
                    .stride = 5,
                    .coverage = std::span<std::uint8_t>(pattern),
                    .generation = slot,
                },
            .bearing_x = 0,
            .bearing_y = 5,
        };

        require(
            cache.insert(key, mask) == veyra::GlyphMaskCacheResult::inserted,
            "synthetic glyph mask enters the cache"
        );
    }
}

void shape_runs(SyntheticFont& font, GlyphFixtures& fixtures) {
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
    require(fixtures.glyph_count == kGlyphsPerRun, "every codepoint shapes one glyph");

    for (auto& glyph : fixtures.glyphs) {
        glyph.x_advance = 600;
    }
}

std::size_t build_run(
    GlyphFixtures& fixtures,
    veyra::GlyphMaskCache& cache,
    veyra::LayoutPoint baseline,
    veyra::DisplayColor color,
    std::span<veyra::SoftwareMask> mask_views,
    std::span<veyra::DisplayCommand> commands
) {
    auto key = veyra::GlyphMaskKey{
        .face_id = 7,
        .pixel_size = 16,
    };
    auto command_count = std::size_t{};

    require(
        veyra::build_glyph_draw_commands(
            std::span<const veyra::ShapedGlyph>(fixtures.glyphs).first(fixtures.glyph_count),
            cache,
            key,
            baseline,
            62.5,
            62.5,
            color,
            mask_views,
            commands,
            command_count
        ) == veyra::GlyphRunResult::complete,
        "shaped glyphs lower to mask draws"
    );

    return command_count;
}

void check_glyph_parity(
    veyra::NogaContext& context,
    veyra::NogaFramePipeline& pipeline,
    std::span<veyra::SoftwarePixel> expected,
    std::span<veyra::SoftwarePixel> actual,
    SoftwareRooms& rooms
) {
    auto font = SyntheticFont{};
    auto fixtures = GlyphFixtures{};
    auto cache = veyra::GlyphMaskCache(
        std::span<veyra::GlyphMaskCacheSlot>(fixtures.slots),
        std::span<std::uint8_t>(fixtures.coverage),
        8,
        8
    );

    insert_glyph_masks(cache);
    shape_runs(font, fixtures);

    auto mask_views = std::array<veyra::SoftwareMask, kGlyphsPerRun * kRuns>{};
    auto run_commands = std::array<veyra::DisplayCommand, kGlyphsPerRun * kRuns>{};

    auto run = build_run(
        fixtures,
        cache,
        {.x = 8.0F, .y = 24.0F},
        {.red = 20, .green = 20, .blue = 24, .alpha = 255},
        std::span<veyra::SoftwareMask>(mask_views).subspan(0, kGlyphsPerRun),
        std::span<veyra::DisplayCommand>(run_commands).subspan(0, kGlyphsPerRun)
    );
    require(run == kGlyphsPerRun, "opaque run emits one draw per glyph");

    run = build_run(
        fixtures,
        cache,
        {.x = 8.0F, .y = 24.0F},
        {.red = 210, .green = 60, .blue = 40, .alpha = 160},
        std::span<veyra::SoftwareMask>(mask_views).subspan(kGlyphsPerRun, kGlyphsPerRun),
        std::span<veyra::DisplayCommand>(run_commands).subspan(kGlyphsPerRun, kGlyphsPerRun)
    );
    require(run == kGlyphsPerRun, "translucent run overlaps the opaque run");

    run = build_run(
        fixtures,
        cache,
        {.x = 8.0F, .y = 24.0F},
        {.red = 30, .green = 140, .blue = 60, .alpha = 255},
        std::span<veyra::SoftwareMask>(mask_views).subspan(2 * kGlyphsPerRun, kGlyphsPerRun),
        std::span<veyra::DisplayCommand>(run_commands).subspan(2 * kGlyphsPerRun, kGlyphsPerRun)
    );
    require(run == kGlyphsPerRun, "translated run emits one draw per glyph");

    run = build_run(
        fixtures,
        cache,
        {.x = 8.0F, .y = 56.0F},
        {.red = 40, .green = 70, .blue = 210, .alpha = 255},
        std::span<veyra::SoftwareMask>(mask_views).subspan(3 * kGlyphsPerRun, kGlyphsPerRun),
        std::span<veyra::DisplayCommand>(run_commands).subspan(3 * kGlyphsPerRun, kGlyphsPerRun)
    );
    require(run == kGlyphsPerRun, "clipped run emits one draw per glyph");

    const std::array<veyra::DisplayCommand, 17> storage{
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
        veyra::PushTranslation{.offset = {.x = 40.0F, .y = 28.0F}},
        run_commands[6],
        run_commands[7],
        run_commands[8],
        veyra::PopTranslation{},
        veyra::PushClip{.bounds = {.x = 8.0F, .y = 48.0F, .width = 32.0F, .height = 16.0F}},
        run_commands[9],
        run_commands[10],
        run_commands[11],
        veyra::PopClip{},
    };

    const auto scene = std::span<const veyra::DisplayCommand>{storage.data(), storage.size()};

    render_reference(scene, expected, kWidth, kHeight, rooms);

    require(
        expected[21 * kWidth + 20].red > 60 && expected[21 * kWidth + 20].red < 150,
        "translucent overlap blends both runs"
    );

    auto stats = veyra::NogaFrameStats{};

    require(
        veyra::render_noga_frame(context, pipeline, scene, actual, stats) ==
            veyra::NogaRenderResult::complete,
        "shaped runs render on the gpu path"
    );
    require(stats.submitted == 13, "every glyph draw submits once");
    require(stats.mask_draws >= 4, "glyphs batch through the mask pipeline");

    check_parity(context, pipeline, scene, expected, actual, "glyph", 1);
}

void check_steady_glyphs_allocate_nothing(
    veyra::NogaContext& context,
    veyra::NogaFramePipeline& pipeline,
    std::span<veyra::SoftwarePixel> expected,
    std::span<veyra::SoftwarePixel> actual,
    SoftwareRooms& rooms
) {
    auto font = SyntheticFont{};
    auto fixtures = GlyphFixtures{};
    auto cache = veyra::GlyphMaskCache(
        std::span<veyra::GlyphMaskCacheSlot>(fixtures.slots),
        std::span<std::uint8_t>(fixtures.coverage),
        8,
        8
    );

    insert_glyph_masks(cache);
    shape_runs(font, fixtures);

    auto mask_views = std::array<veyra::SoftwareMask, kGlyphsPerRun>{};
    auto run_commands = std::array<veyra::DisplayCommand, kGlyphsPerRun>{};

    const auto run = build_run(
        fixtures,
        cache,
        {.x = 8.0F, .y = 24.0F},
        {.red = 20, .green = 20, .blue = 24, .alpha = 255},
        std::span<veyra::SoftwareMask>(mask_views),
        std::span<veyra::DisplayCommand>(run_commands)
    );
    require(run == kGlyphsPerRun, "steady run builds");

    const auto scene =
        std::span<const veyra::DisplayCommand>{run_commands.data(), run_commands.size()};

    render_reference(scene, expected, kWidth, kHeight, rooms);

    auto stats = veyra::NogaFrameStats{};

    require(
        veyra::render_noga_frame(context, pipeline, scene, actual, stats) ==
            veyra::NogaRenderResult::complete,
        "steady window opens clean"
    );

    g_allocation_count.store(0, std::memory_order_relaxed);
    g_counting.store(true, std::memory_order_relaxed);

    for (auto repeat = std::size_t{}; repeat < 60; ++repeat) {
        const auto result = veyra::render_noga_frame(context, pipeline, scene, actual, stats);

        if (result != veyra::NogaRenderResult::complete) {
            g_counting.store(false, std::memory_order_relaxed);

            throw std::runtime_error("steady glyph repeats stay complete");
        }
    }

    g_counting.store(false, std::memory_order_relaxed);

    std::cout << "noga glyph repeats=60 host_allocations="
              << g_allocation_count.load(std::memory_order_relaxed) << '\n';

    require(g_allocation_count.load(std::memory_order_relaxed) == 0, "glyphs allocate nothing");
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
        "glyph-capable pipeline comes up"
    );

    auto rooms = SoftwareRooms{};
    auto expected = std::array<veyra::SoftwarePixel, kWidth * kHeight>{};
    auto actual = std::array<veyra::SoftwarePixel, kWidth * kHeight>{};

    check_glyph_parity(
        context,
        pipeline,
        std::span<veyra::SoftwarePixel>{expected.data(), expected.size()},
        std::span<veyra::SoftwarePixel>{actual.data(), actual.size()},
        rooms
    );

    check_steady_glyphs_allocate_nothing(
        context,
        pipeline,
        std::span<veyra::SoftwarePixel>{expected.data(), expected.size()},
        std::span<veyra::SoftwarePixel>{actual.data(), actual.size()},
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
