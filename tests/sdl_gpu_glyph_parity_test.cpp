#include "veyra/render/cpu/software_renderer.hpp"
#include "veyra/render/gpu/sdl/sdl_gpu_renderer.hpp"
#include "veyra/render/resource/glyph_atlas.hpp"
#include "veyra/text/rendering/glyph_mask_cache.hpp"

#include "sdl_gpu_parity_helpers.hpp"

#include "stb_rect_pack.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

using veyra::sdl_gpu_parity::check_parity;
using veyra::sdl_gpu_parity::render_reference;
using veyra::sdl_gpu_parity::require;
using veyra::sdl_gpu_parity::SdlGpuFixture;
using veyra::sdl_gpu_parity::SoftwareRooms;

constexpr std::size_t kWidth = 96;
constexpr std::size_t kHeight = 72;
constexpr std::size_t kAtlasWidth = 32;
constexpr std::size_t kAtlasHeight = 32;

struct GlyphRooms {
    std::array<veyra::GlyphAtlasSlot, 8> slots{};
    std::array<std::uint8_t, kAtlasWidth * kAtlasHeight> coverage{};
    std::array<stbrp_node, kAtlasWidth> nodes{};
};

veyra::GlyphMaskKey glyph_key(std::uint32_t glyph_id) {
    return veyra::GlyphMaskKey{
        .face_id = 7,
        .glyph_id = glyph_id,
        .pixel_size = 16,
    };
}

void check_glyph_parity(
    veyra::SdlGpuRenderer& renderer,
    std::span<veyra::SoftwarePixel> expected,
    std::span<veyra::SoftwarePixel> actual,
    SoftwareRooms& rooms
) {
    auto attic = GlyphRooms{};
    auto atlas = veyra::GlyphAtlas(
        std::span<veyra::GlyphAtlasSlot>(attic.slots),
        std::span<std::uint8_t>(attic.coverage),
        std::span<stbrp_node>(attic.nodes),
        kAtlasWidth,
        kAtlasHeight
    );

    require(atlas.valid(), "sized atlas validates");

    auto pattern = std::array<std::uint8_t, 35>{};

    for (auto index = std::size_t{}; index < pattern.size(); ++index) {
        const auto column = index % 5;
        pattern[index] = static_cast<std::uint8_t>(64 + column * 48);
    }

    const std::array<std::uint32_t, 3> ids{97, 98, 99};
    auto placements = std::array<veyra::GlyphAtlasPlacement, 3>{};

    for (auto slot = std::size_t{}; slot < ids.size(); ++slot) {
        const auto mask = veyra::GlyphMask{
            .mask =
                {
                    .width = 5,
                    .height = 7,
                    .stride = 5,
                    .coverage = pattern,
                    .generation = 3,
                },
            .bearing_x = 0,
            .bearing_y = 5,
        };

        require(
            atlas.insert(glyph_key(ids[slot]), mask) == veyra::GlyphAtlasResult::inserted,
            "synthetic glyph inserts"
        );

        const auto found = atlas.find(glyph_key(ids[slot]));

        require(found.has_value(), "inserted glyph is found");
        placements[slot] = *found;
    }

    const std::array<veyra::DisplayCommand, 5> storage{
        veyra::FillRect{
            .bounds = {.width = static_cast<float>(kWidth), .height = static_cast<float>(kHeight)},
            .color = {.red = 240, .green = 240, .blue = 244, .alpha = 255},
        },
        veyra::DrawGlyph{
            .atlas = &atlas,
            .u = placements[0].u,
            .v = placements[0].v,
            .width = placements[0].width,
            .height = placements[0].height,
            .x = 8.0F,
            .y = 10.0F,
            .color = {.red = 30, .green = 31, .blue = 38, .alpha = 255},
        },
        veyra::DrawGlyph{
            .atlas = &atlas,
            .u = placements[1].u,
            .v = placements[1].v,
            .width = placements[1].width,
            .height = placements[1].height,
            .x = 20.5F,
            .y = 12.0F,
            .color = {.red = 120, .green = 40, .blue = 30, .alpha = 180},
        },
        veyra::DrawGlyph{
            .atlas = &atlas,
            .u = placements[2].u,
            .v = placements[2].v,
            .width = placements[2].width,
            .height = placements[2].height,
            .x = 60.0F,
            .y = 50.0F,
            .color = {.red = 20, .green = 110, .blue = 230, .alpha = 0},
        },
        veyra::DrawGlyph{
            .atlas = &atlas,
            .u = placements[0].u,
            .v = placements[0].v,
            .width = placements[0].width,
            .height = placements[0].height,
            .x = 200.0F,
            .y = 10.0F,
            .color = {.red = 20, .green = 110, .blue = 230, .alpha = 255},
        },
    };

    const auto scene = std::span<const veyra::DisplayCommand>{storage.data(), storage.size()};

    render_reference(scene, expected, kWidth, kHeight, rooms);

    auto stats = veyra::SdlGpuRenderStats{};

    require(
        veyra::render_sdl_gpu_rects(renderer, scene, actual, stats) == veyra::WindowResult::ready,
        "mixed rect and glyph scene completes"
    );
    require(stats.submitted == 3, "alpha-zero and offscreen glyphs never submit");
    require(stats.glyph_draws >= 1, "glyph runs draw through the glyph pipeline");

    check_parity(renderer, scene, expected, actual, "glyph", 1);

    require(
        veyra::render_sdl_gpu_rects(renderer, scene, actual, stats) == veyra::WindowResult::ready,
        "a repeat frame completes"
    );
    require(stats.uploads == 0, "the cached atlas never re-uploads");

    check_parity(renderer, scene, expected, actual, "glyph-cached", 1);
}

int run() {
    auto fixture = SdlGpuFixture{};

    if (!fixture.setup(kWidth, kHeight)) {
        std::cout << "sdl-gpu device or window unavailable, skipping\n";

        return 77;
    }

    std::cout << "sdl-gpu driver=" << fixture.presenter.device_driver() << '\n';

    auto rooms = SoftwareRooms{};
    auto expected = std::array<veyra::SoftwarePixel, kWidth * kHeight>{};
    auto actual = std::array<veyra::SoftwarePixel, kWidth * kHeight>{};

    check_glyph_parity(fixture.renderer, expected, actual, rooms);

    fixture.teardown();

    require(!veyra::sdl_gpu_renderer_ready(fixture.renderer), "renderer tears down");

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
