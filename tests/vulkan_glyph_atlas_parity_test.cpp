#include "veyra/render/software_renderer.hpp"
#include "veyra/render/vulkan_context.hpp"
#include "veyra/render/vulkan_rect_pipeline.hpp"
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

std::atomic<std::size_t> g_allocation_count{0};
std::atomic<bool> g_counting{false};

void note_allocation() noexcept {
    if (g_counting.load(std::memory_order_relaxed)) {
        g_allocation_count.fetch_add(1, std::memory_order_relaxed);
    }
}

}

void* operator new(std::size_t size) {
    note_allocation();

    if (size == 0) {
        size = 1;
    }

    void* result = std::malloc(size);

    if (result == nullptr) {
        throw std::bad_alloc{};
    }

    return result;
}

void operator delete(void* ptr) noexcept {
    std::free(ptr);
}

void operator delete(void* ptr, std::size_t) noexcept {
    std::free(ptr);
}

void* operator new[](std::size_t size) {
    return operator new(size);
}

void operator delete[](void* ptr) noexcept {
    operator delete(ptr);
}

void operator delete[](void* ptr, std::size_t size) noexcept {
    operator delete(ptr, size);
}

namespace {

using veyra::vulkan_parity::check_parity;
using veyra::vulkan_parity::render_reference;
using veyra::vulkan_parity::require;
using veyra::vulkan_parity::SoftwareRooms;

constexpr std::size_t kWidth = 96;
constexpr std::size_t kHeight = 72;
constexpr std::size_t kGlyphsPerRun = 3;
constexpr std::size_t kRuns = 4;
constexpr std::size_t kAtlasWidth = 32;
constexpr std::size_t kAtlasHeight = 32;
constexpr std::size_t kEvictionAtlasWidth = 16;
constexpr std::size_t kEvictionAtlasHeight = 16;

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

struct AtlasFixtures {
    std::array<veyra::GlyphAtlasSlot, 4> slots{};
    std::array<std::uint8_t, kAtlasWidth * kAtlasHeight> coverage{};
    std::array<stbrp_node, kAtlasWidth> nodes{};
    std::array<veyra::ShapedGlyph, kGlyphsPerRun> glyphs{};
    std::size_t glyph_count{};
};

struct EvictionRooms {
    std::array<veyra::GlyphAtlasSlot, 4> slots{};
    std::array<std::uint8_t, kEvictionAtlasWidth * kEvictionAtlasHeight> coverage{};
    std::array<stbrp_node, kEvictionAtlasWidth> nodes{};
};

void insert_run_glyphs(veyra::GlyphAtlas& atlas, veyra::GlyphMaskKey key) {
    auto pattern = std::array<std::uint8_t, 35>{};

    for (auto index = std::size_t{}; index < pattern.size(); ++index) {
        pattern[index] = static_cast<std::uint8_t>(64 + (index % 5) * 48);
    }

    const std::array<std::uint32_t, 3> ids{97, 98, 99};

    for (const auto id : ids) {
        key.glyph_id = id;

        const auto mask = veyra::GlyphMask{
            .mask =
                {
                    .width = 5,
                    .height = 7,
                    .stride = 5,
                    .coverage = std::span<std::uint8_t>(pattern),
                    .generation = 0,
                },
            .bearing_x = 0,
            .bearing_y = 5,
        };

        const auto inserted = atlas.insert(key, mask);

        require(
            inserted == veyra::GlyphAtlasResult::inserted ||
                inserted == veyra::GlyphAtlasResult::updated,
            "run glyphs pack into the atlas"
        );
    }
}

void shape_runs(SyntheticFont& font, AtlasFixtures& fixtures) {
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
    AtlasFixtures& fixtures,
    veyra::GlyphAtlas& atlas,
    veyra::LayoutPoint baseline,
    veyra::DisplayColor color,
    std::span<veyra::DisplayCommand> commands
) {
    auto command_count = std::size_t{};

    require(
        veyra::build_glyph_atlas_draws(
            std::span<const veyra::ShapedGlyph>(fixtures.glyphs).first(fixtures.glyph_count),
            atlas,
            {.face_id = 7, .pixel_size = 16},
            baseline,
            62.5,
            62.5,
            color,
            commands,
            command_count
        ) == veyra::GlyphRunResult::complete,
        "shaped glyphs lower to atlas draws"
    );

    return command_count;
}

void check_atlas_parity(
    veyra::VulkanContext& context,
    veyra::VulkanRectPipeline& pipeline,
    std::span<veyra::SoftwarePixel> expected,
    std::span<veyra::SoftwarePixel> actual,
    SoftwareRooms& rooms
) {
    auto font = SyntheticFont{};
    auto fixtures = AtlasFixtures{};
    auto atlas = veyra::GlyphAtlas(
        std::span<veyra::GlyphAtlasSlot>(fixtures.slots),
        std::span<std::uint8_t>(fixtures.coverage),
        std::span<stbrp_node>(fixtures.nodes),
        kAtlasWidth,
        kAtlasHeight
    );

    require(atlas.valid(), "test atlas validates");

    insert_run_glyphs(atlas, {.face_id = 7, .pixel_size = 16});
    shape_runs(font, fixtures);

    auto run_commands = std::array<veyra::DisplayCommand, kGlyphsPerRun * kRuns>{};

    auto run = build_run(
        fixtures,
        atlas,
        {.x = 8.0F, .y = 24.0F},
        {.red = 20, .green = 20, .blue = 24, .alpha = 255},
        std::span<veyra::DisplayCommand>(run_commands).subspan(0, kGlyphsPerRun)
    );
    require(run == kGlyphsPerRun, "opaque run emits one draw per glyph");

    run = build_run(
        fixtures,
        atlas,
        {.x = 8.0F, .y = 24.0F},
        {.red = 210, .green = 60, .blue = 40, .alpha = 160},
        std::span<veyra::DisplayCommand>(run_commands).subspan(kGlyphsPerRun, kGlyphsPerRun)
    );
    require(run == kGlyphsPerRun, "translucent run overlaps the opaque run");

    run = build_run(
        fixtures,
        atlas,
        {.x = 8.0F, .y = 24.0F},
        {.red = 30, .green = 140, .blue = 60, .alpha = 255},
        std::span<veyra::DisplayCommand>(run_commands).subspan(2 * kGlyphsPerRun, kGlyphsPerRun)
    );
    require(run == kGlyphsPerRun, "translated run emits one draw per glyph");

    run = build_run(
        fixtures,
        atlas,
        {.x = 8.0F, .y = 56.0F},
        {.red = 40, .green = 70, .blue = 210, .alpha = 255},
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
        "atlas overlap blends both runs"
    );

    auto stats = veyra::VulkanRectStats{};

    require(
        veyra::render_vulkan_rects(context, pipeline, scene, actual, stats) ==
            veyra::VulkanRenderResult::complete,
        "atlas runs render on the gpu path"
    );
    require(stats.submitted == 13, "every atlas glyph draw submits once");
    require(stats.mask_draws == 0, "atlas glyphs skip the mask pipeline");
    require(stats.glyph_draws >= 1, "atlas glyphs batch through the glyph pipeline");

    check_parity(context, pipeline, scene, expected, actual, "glyph-atlas", 1);
}

void check_atlas_rejections(
    veyra::VulkanContext& context,
    veyra::VulkanRectPipeline& pipeline,
    std::span<veyra::SoftwarePixel> scratch
) {
    auto stats = veyra::VulkanRectStats{};

    const std::array<veyra::DisplayCommand, 1> null_atlas{
        veyra::DrawGlyph{
            .atlas = nullptr,
            .width = 4,
            .height = 4,
            .color = {.red = 255, .alpha = 255},
        },
    };

    require(
        veyra::render_vulkan_rects(
            context,
            pipeline,
            std::span<const veyra::DisplayCommand>(null_atlas),
            scratch,
            stats
        ) == veyra::VulkanRenderResult::invalid_input,
        "glyphs without an atlas are rejected"
    );

    auto rooms = AtlasFixtures{};
    auto atlas = veyra::GlyphAtlas(
        std::span<veyra::GlyphAtlasSlot>(rooms.slots),
        std::span<std::uint8_t>(rooms.coverage),
        std::span<stbrp_node>(rooms.nodes),
        kAtlasWidth,
        kAtlasHeight
    );

    const std::array<veyra::DisplayCommand, 1> outside{
        veyra::DrawGlyph{
            .atlas = &atlas,
            .u = static_cast<std::uint32_t>(kAtlasWidth),
            .v = 0,
            .width = 4,
            .height = 4,
            .color = {.red = 255, .alpha = 255},
        },
    };

    require(
        veyra::render_vulkan_rects(
            context,
            pipeline,
            std::span<const veyra::DisplayCommand>(outside),
            scratch,
            stats
        ) == veyra::VulkanRenderResult::invalid_input,
        "glyphs outside the atlas are rejected"
    );

    const std::array<veyra::DisplayCommand, 1> empty{
        veyra::DrawGlyph{
            .atlas = &atlas,
            .width = 0,
            .height = 4,
            .color = {.red = 255, .alpha = 255},
        },
    };

    require(
        veyra::render_vulkan_rects(
            context,
            pipeline,
            std::span<const veyra::DisplayCommand>(empty),
            scratch,
            stats
        ) == veyra::VulkanRenderResult::invalid_input,
        "empty glyph draws are rejected"
    );
}

void check_atlas_eviction_parity(
    veyra::VulkanContext& context,
    veyra::VulkanRectPipeline& pipeline,
    std::span<veyra::SoftwarePixel> expected,
    std::span<veyra::SoftwarePixel> actual,
    SoftwareRooms& rooms
) {
    auto font = SyntheticFont{};
    auto fixtures = AtlasFixtures{};

    auto churn = EvictionRooms{};
    auto atlas = veyra::GlyphAtlas(
        std::span<veyra::GlyphAtlasSlot>(churn.slots),
        std::span<std::uint8_t>(churn.coverage),
        std::span<stbrp_node>(churn.nodes),
        kEvictionAtlasWidth,
        kEvictionAtlasHeight
    );

    require(atlas.valid(), "eviction atlas validates");

    insert_run_glyphs(atlas, {.face_id = 7, .pixel_size = 16});
    shape_runs(font, fixtures);

    auto run_commands = std::array<veyra::DisplayCommand, kGlyphsPerRun>{};

    require(
        build_run(
            fixtures,
            atlas,
            {.x = 8.0F, .y = 24.0F},
            {.red = 20, .green = 20, .blue = 24, .alpha = 255},
            std::span<veyra::DisplayCommand>(run_commands)
        ) == kGlyphsPerRun,
        "pre-eviction run builds"
    );

    const std::array<veyra::DisplayCommand, 4> before{
        veyra::FillRect{
            .bounds = {.width = static_cast<float>(kWidth), .height = static_cast<float>(kHeight)},
            .color = {.red = 240, .green = 240, .blue = 244, .alpha = 255},
        },
        run_commands[0],
        run_commands[1],
        run_commands[2],
    };

    const auto before_scene = std::span<const veyra::DisplayCommand>{before.data(), before.size()};

    render_reference(before_scene, expected, kWidth, kHeight, rooms);
    check_parity(context, pipeline, before_scene, expected, actual, "glyph-atlas-prefill", 1);

    auto churn_pixels = std::array<std::uint8_t, 144>{};
    churn_pixels.fill(128);

    auto evicted = false;

    for (auto id = std::uint32_t{100}; id < 124 && !evicted; ++id) {
        const auto churned = atlas.insert(
            veyra::GlyphMaskKey{.face_id = 7, .glyph_id = id, .pixel_size = 16},
            veyra::GlyphMask{
                .mask =
                    {
                        .width = 12,
                        .height = 12,
                        .stride = 12,
                        .coverage = std::span<std::uint8_t>(churn_pixels),
                        .generation = 0,
                    },
                .bearing_x = 0,
                .bearing_y = 10,
            }
        );

        require(
            churned == veyra::GlyphAtlasResult::inserted ||
                churned == veyra::GlyphAtlasResult::inserted_after_eviction,
            "churn tiles pack"
        );

        evicted = churned == veyra::GlyphAtlasResult::inserted_after_eviction;
    }

    require(evicted, "churn overflows the small atlas");
    require(
        !atlas.find(veyra::GlyphMaskKey{.face_id = 7, .glyph_id = 97, .pixel_size = 16})
             .has_value(),
        "eviction drops old placements"
    );

    auto pattern = std::array<std::uint8_t, 35>{};

    for (auto index = std::size_t{}; index < pattern.size(); ++index) {
        pattern[index] = static_cast<std::uint8_t>(64 + (index % 5) * 48);
    }

    for (const auto id : {97U, 98U, 99U}) {
        const auto remade = atlas.insert(
            veyra::GlyphMaskKey{.face_id = 7, .glyph_id = id, .pixel_size = 16},
            veyra::GlyphMask{
                .mask =
                    {
                        .width = 5,
                        .height = 7,
                        .stride = 5,
                        .coverage = std::span<std::uint8_t>(pattern),
                        .generation = 0,
                    },
                .bearing_x = 0,
                .bearing_y = 5,
            }
        );

        require(
            remade == veyra::GlyphAtlasResult::inserted ||
                remade == veyra::GlyphAtlasResult::inserted_after_eviction,
            "runs re-pack after eviction"
        );
    }

    require(
        build_run(
            fixtures,
            atlas,
            {.x = 8.0F, .y = 24.0F},
            {.red = 20, .green = 20, .blue = 24, .alpha = 255},
            std::span<veyra::DisplayCommand>(run_commands)
        ) == kGlyphsPerRun,
        "post-eviction run rebuilds from fresh placements"
    );

    const std::array<veyra::DisplayCommand, 4> after{
        veyra::FillRect{
            .bounds = {.width = static_cast<float>(kWidth), .height = static_cast<float>(kHeight)},
            .color = {.red = 240, .green = 240, .blue = 244, .alpha = 255},
        },
        run_commands[0],
        run_commands[1],
        run_commands[2],
    };

    const auto after_scene = std::span<const veyra::DisplayCommand>{after.data(), after.size()};

    render_reference(after_scene, expected, kWidth, kHeight, rooms);

    auto inked = false;

    for (auto row = std::size_t{15}; row < 27 && !inked; ++row) {
        for (auto column = std::size_t{6}; column < 46 && !inked; ++column) {
            inked = expected[row * kWidth + column].red < 150;
        }
    }

    require(inked, "rebuilt run inks the reference");

    auto stats = veyra::VulkanRectStats{};

    require(
        veyra::render_vulkan_rects(context, pipeline, after_scene, actual, stats) ==
            veyra::VulkanRenderResult::complete,
        "rebuilt run renders on the gpu path"
    );

    require(stats.submitted == 4, "rebuilt run submits fill and glyphs");
    require(stats.mask_draws == 0, "rebuilt glyphs skip the mask pipeline");
    require(stats.glyph_draws == 1, "rebuilt glyphs batch through the glyph pipeline");

    check_parity(context, pipeline, after_scene, expected, actual, "glyph-atlas-post-eviction", 1);
}

void check_steady_atlas_allocate_nothing(
    veyra::VulkanContext& context,
    veyra::VulkanRectPipeline& pipeline,
    std::span<veyra::SoftwarePixel> expected,
    std::span<veyra::SoftwarePixel> actual,
    SoftwareRooms& rooms
) {
    auto font = SyntheticFont{};
    auto fixtures = AtlasFixtures{};
    auto atlas = veyra::GlyphAtlas(
        std::span<veyra::GlyphAtlasSlot>(fixtures.slots),
        std::span<std::uint8_t>(fixtures.coverage),
        std::span<stbrp_node>(fixtures.nodes),
        kAtlasWidth,
        kAtlasHeight
    );

    insert_run_glyphs(atlas, {.face_id = 7, .pixel_size = 16});
    shape_runs(font, fixtures);

    auto run_commands = std::array<veyra::DisplayCommand, kGlyphsPerRun>{};
    const auto run = build_run(
        fixtures,
        atlas,
        {.x = 8.0F, .y = 24.0F},
        {.red = 20, .green = 20, .blue = 24, .alpha = 255},
        std::span<veyra::DisplayCommand>(run_commands)
    );
    require(run == kGlyphsPerRun, "steady run builds");

    const auto scene =
        std::span<const veyra::DisplayCommand>{run_commands.data(), run_commands.size()};

    render_reference(scene, expected, kWidth, kHeight, rooms);

    auto stats = veyra::VulkanRectStats{};

    require(
        veyra::render_vulkan_rects(context, pipeline, scene, actual, stats) ==
            veyra::VulkanRenderResult::complete,
        "steady window opens clean"
    );
    require(stats.glyph_draws == 1, "one atlas run draws once");
    require(stats.uploads == 1, "one atlas run uploads once");

    g_allocation_count.store(0, std::memory_order_relaxed);
    g_counting.store(true, std::memory_order_relaxed);

    for (auto repeat = std::size_t{}; repeat < 60; ++repeat) {
        const auto result = veyra::render_vulkan_rects(context, pipeline, scene, actual, stats);

        if (result != veyra::VulkanRenderResult::complete) {
            g_counting.store(false, std::memory_order_relaxed);

            throw std::runtime_error("steady atlas repeats stay complete");
        }
    }

    g_counting.store(false, std::memory_order_relaxed);

    std::cout << "vulkan glyph-atlas repeats=60 host_allocations="
              << g_allocation_count.load(std::memory_order_relaxed) << '\n';

    require(
        g_allocation_count.load(std::memory_order_relaxed) == 0,
        "atlas draws allocate nothing"
    );
    require(stats.uploads == 0, "steady atlas repeats never re-upload");
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

    check_atlas_parity(
        context,
        pipeline,
        std::span<veyra::SoftwarePixel>{expected.data(), expected.size()},
        std::span<veyra::SoftwarePixel>{actual.data(), actual.size()},
        rooms
    );

    check_atlas_rejections(
        context,
        pipeline,
        std::span<veyra::SoftwarePixel>{actual.data(), actual.size()}
    );

    check_atlas_eviction_parity(
        context,
        pipeline,
        std::span<veyra::SoftwarePixel>{expected.data(), expected.size()},
        std::span<veyra::SoftwarePixel>{actual.data(), actual.size()},
        rooms
    );

    check_steady_atlas_allocate_nothing(
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
