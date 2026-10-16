#include "allocation_counter.hpp"
#include "veyra/render/glyph_atlas.hpp"
#include "veyra/render/software_renderer.hpp"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <new>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

void require(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

constexpr std::size_t kAtlasWidth = 16;
constexpr std::size_t kAtlasHeight = 16;

struct AtlasRooms {
    std::array<veyra::GlyphAtlasSlot, 4> slots{};
    std::array<std::uint8_t, kAtlasWidth * kAtlasHeight> coverage{};
    std::array<stbrp_node, kAtlasWidth> nodes{};
};

veyra::GlyphMaskKey test_key(std::uint32_t glyph_id) {
    return veyra::GlyphMaskKey{
        .face_id = 7,
        .glyph_id = glyph_id,
        .pixel_size = 16,
    };
}

veyra::GlyphMask
test_mask(std::span<const std::uint8_t> pixels, std::size_t width, std::size_t height) {
    return veyra::GlyphMask{
        .mask =
            {
                .width = width,
                .height = height,
                .stride = width,
                .coverage = pixels,
                .generation = 3,
            },
        .bearing_x = 1,
        .bearing_y = 6,
    };
}

void check_insert_and_find() {
    auto rooms = AtlasRooms{};
    auto atlas = veyra::GlyphAtlas(
        std::span<veyra::GlyphAtlasSlot>(rooms.slots),
        std::span<std::uint8_t>(rooms.coverage),
        std::span<stbrp_node>(rooms.nodes),
        kAtlasWidth,
        kAtlasHeight
    );

    require(atlas.valid(), "sized atlas validates");

    auto pixels = std::array<std::uint8_t, 35>{};
    pixels.fill(200);

    require(
        atlas.insert(test_key(97), test_mask(pixels, 5, 7)) == veyra::GlyphAtlasResult::inserted,
        "first glyph packs"
    );

    const auto found = atlas.find(test_key(97));
    require(found.has_value(), "packed glyph resolves");
    require(found->width == 5 && found->height == 7, "placement keeps glyph extents");
    require(found->bearing_x == 1 && found->bearing_y == 6, "placement keeps bearings");
    require(found->u + found->width <= kAtlasWidth, "placement fits atlas width");
    require(found->v + found->height <= kAtlasHeight, "placement fits atlas height");

    const auto view = atlas.view();
    for (auto row = std::size_t{}; row < found->height; ++row) {
        for (auto column = std::size_t{}; column < found->width; ++column) {
            require(
                view.coverage[(found->v + row) * view.stride + found->u + column] == 200,
                "packed pixels land at the placement"
            );
        }
    }

    require(!atlas.find(test_key(98)).has_value(), "missing glyphs miss");

    const auto generation = atlas.generation();
    auto replacement = std::array<std::uint8_t, 35>{};
    replacement.fill(100);

    require(
        atlas.insert(test_key(97), test_mask(replacement, 5, 7)) ==
            veyra::GlyphAtlasResult::updated,
        "same key with same extents updates in place"
    );
    require(atlas.generation() != generation, "updates bump the generation");

    const auto again = atlas.find(test_key(97));
    require(again.has_value(), "updated glyph still resolves");
    require(again->u == found->u && again->v == found->v, "updates keep the placement");

    const auto updated_view = atlas.view();
    require(
        updated_view.coverage[again->v * updated_view.stride + again->u] == 100,
        "updates replace the pixels"
    );
}

void check_eviction_and_clear() {
    auto rooms = AtlasRooms{};
    auto atlas = veyra::GlyphAtlas(
        std::span<veyra::GlyphAtlasSlot>(rooms.slots),
        std::span<std::uint8_t>(rooms.coverage),
        std::span<stbrp_node>(rooms.nodes),
        8,
        8
    );

    require(atlas.valid(), "small atlas validates");

    auto pixels = std::array<std::uint8_t, 16>{};
    pixels.fill(200);

    require(
        atlas.insert(test_key(1), test_mask(pixels, 4, 4)) == veyra::GlyphAtlasResult::inserted,
        "first tile packs"
    );
    require(
        atlas.insert(test_key(2), test_mask(pixels, 4, 4)) == veyra::GlyphAtlasResult::inserted,
        "second tile packs"
    );
    require(
        atlas.insert(test_key(3), test_mask(pixels, 4, 4)) == veyra::GlyphAtlasResult::inserted,
        "third tile packs beside the others"
    );

    const auto full = atlas.insert(test_key(4), test_mask(pixels, 4, 4));

    require(
        full == veyra::GlyphAtlasResult::inserted ||
            full == veyra::GlyphAtlasResult::inserted_after_eviction,
        "fourth tile packs or evicts"
    );

    const auto generation = atlas.generation();
    auto big = std::array<std::uint8_t, 49>{};
    big.fill(150);

    require(
        atlas.insert(test_key(9), test_mask(big, 7, 7)) ==
            veyra::GlyphAtlasResult::inserted_after_eviction,
        "oversized tile evicts the atlas"
    );
    require(atlas.generation() != generation, "eviction bumps the generation");
    require(!atlas.find(test_key(1)).has_value(), "eviction drops old placements");

    const auto kept = atlas.find(test_key(9));
    require(kept.has_value(), "evicting insert keeps the new tile");

    atlas.clear();
    require(!atlas.find(test_key(9)).has_value(), "clear drops placements");

    auto refilled = std::array<std::uint8_t, 16>{};
    refilled.fill(200);

    require(
        atlas.insert(test_key(1), test_mask(refilled, 4, 4)) == veyra::GlyphAtlasResult::inserted,
        "cleared atlas packs again"
    );
}

void check_full_bucket_reuses_oldest() {
    auto rooms = AtlasRooms{};
    auto atlas = veyra::GlyphAtlas(
        std::span<veyra::GlyphAtlasSlot>(rooms.slots),
        std::span<std::uint8_t>(rooms.coverage),
        std::span<stbrp_node>(rooms.nodes),
        kAtlasWidth,
        kAtlasHeight
    );

    require(atlas.valid(), "reuse atlas validates");

    auto pixels = std::array<std::uint8_t, 35>{};
    pixels.fill(200);

    for (auto id = std::uint32_t{1}; id <= 4; ++id) {
        require(
            atlas.insert(test_key(id), test_mask(pixels, 5, 7)) ==
                veyra::GlyphAtlasResult::inserted,
            "bucket fills one way at a time"
        );
    }

    require(
        atlas.insert(test_key(5), test_mask(pixels, 5, 7)) == veyra::GlyphAtlasResult::inserted,
        "a full bucket reuses its oldest slot"
    );
    require(!atlas.find(test_key(1)).has_value(), "reuse drops the previous key");

    const auto placed = atlas.find(test_key(5));
    require(placed.has_value(), "reused slot resolves the new key");

    for (auto id = std::uint32_t{2}; id <= 4; ++id) {
        const auto kept = atlas.find(test_key(id));
        require(kept.has_value(), "reuse keeps the other residents");
        require(kept->width == 5 && kept->height == 7, "kept placements keep their extents");
    }

    const auto view = atlas.view();
    for (auto row = std::size_t{}; row < placed->height; ++row) {
        for (auto column = std::size_t{}; column < placed->width; ++column) {
            require(
                view.coverage[(placed->v + row) * view.stride + placed->u + column] == 200,
                "reused slot holds the new pixels"
            );
        }
    }
}

void check_rejections() {
    auto rooms = AtlasRooms{};
    auto atlas = veyra::GlyphAtlas(
        std::span<veyra::GlyphAtlasSlot>(rooms.slots),
        std::span<std::uint8_t>(rooms.coverage),
        std::span<stbrp_node>(rooms.nodes),
        kAtlasWidth,
        kAtlasHeight
    );

    auto pixels = std::array<std::uint8_t, 35>{};
    pixels.fill(200);

    auto bad_key = test_key(97);
    bad_key.face_id = 0;

    require(
        atlas.insert(bad_key, test_mask(pixels, 5, 7)) == veyra::GlyphAtlasResult::invalid_key,
        "anonymous faces are rejected"
    );

    auto bad_pixels = std::array<std::uint8_t, 10>{};
    const auto bad_mask = veyra::GlyphMask{
        .mask =
            {
                .width = 5,
                .height = 7,
                .stride = 5,
                .coverage = bad_pixels,
                .generation = 0,
            },
        .bearing_x = 0,
        .bearing_y = 0,
    };

    require(
        atlas.insert(test_key(97), bad_mask) == veyra::GlyphAtlasResult::invalid_mask,
        "short coverage is rejected"
    );

    auto huge = std::array<std::uint8_t, 400>{};
    huge.fill(200);

    require(
        atlas.insert(test_key(97), test_mask(huge, 20, 20)) ==
            veyra::GlyphAtlasResult::glyph_too_large,
        "glyphs larger than the atlas are rejected"
    );

    auto empty_rooms = AtlasRooms{};
    auto empty_slots = std::span<veyra::GlyphAtlasSlot>{};

    auto broken = veyra::GlyphAtlas(
        empty_slots,
        std::span<std::uint8_t>(empty_rooms.coverage),
        std::span<stbrp_node>(empty_rooms.nodes),
        kAtlasWidth,
        kAtlasHeight
    );

    require(!broken.valid(), "slotless atlas never validates");
    require(
        broken.insert(test_key(97), test_mask(pixels, 5, 7)) ==
            veyra::GlyphAtlasResult::invalid_workspace,
        "broken atlas rejects inserts"
    );
    require(!broken.find(test_key(97)).has_value(), "broken atlas misses finds");
}

void check_glyph_draws() {
    constexpr auto width = std::size_t{32};
    constexpr auto height = std::size_t{24};

    auto rooms = AtlasRooms{};
    auto atlas = veyra::GlyphAtlas(
        std::span<veyra::GlyphAtlasSlot>(rooms.slots),
        std::span<std::uint8_t>(rooms.coverage),
        std::span<stbrp_node>(rooms.nodes),
        kAtlasWidth,
        kAtlasHeight
    );

    auto pixels = std::array<std::uint8_t, 35>{};

    for (auto index = std::size_t{}; index < pixels.size(); ++index) {
        pixels[index] = static_cast<std::uint8_t>(64 + (index % 5) * 48);
    }

    require(
        atlas.insert(test_key(97), test_mask(pixels, 5, 7)) == veyra::GlyphAtlasResult::inserted,
        "drawn glyph packs"
    );

    const auto placed = atlas.find(test_key(97));
    require(placed.has_value(), "drawn glyph resolves");

    const std::array<veyra::DisplayCommand, 4> glyph_scene{
        veyra::FillRect{
            .bounds = {.width = static_cast<float>(width), .height = static_cast<float>(height)},
            .color = {.red = 240, .green = 240, .blue = 244, .alpha = 255},
        },
        veyra::PushClip{.bounds = {.x = 8.0F, .y = 8.0F, .width = 16.0F, .height = 12.0F}},
        veyra::DrawGlyph{
            .atlas = &atlas,
            .u = placed->u,
            .v = placed->v,
            .width = placed->width,
            .height = placed->height,
            .x = 6.0F,
            .y = 6.0F,
            .color = {.red = 20, .green = 20, .blue = 24, .alpha = 255},
        },
        veyra::PopClip{},
    };

    std::array<veyra::SoftwarePixel, width * height> glyph_pixels{};
    std::array<veyra::SoftwareClipRect, 1> clips{};
    const auto glyph_result = veyra::render_software(
        glyph_scene,
        {
            .width = width,
            .height = height,
            .stride = width,
            .pixels = glyph_pixels,
        },
        {.clips = clips}
    );

    require(glyph_result == veyra::SoftwareRenderResult::complete, "glyph scene renders");
    require(glyph_pixels[10 * width + 9].red < 100, "atlas texels ink the glyph");
    require(glyph_pixels[2 * width + 2].red > 200, "background stays clean");

    const auto bad_scene = std::array<veyra::DisplayCommand, 1>{
        veyra::DrawGlyph{
            .atlas = &atlas,
            .u = placed->u,
            .v = placed->v,
            .width = 0,
            .height = placed->height,
            .x = 6.0F,
            .y = 6.0F,
            .color = {.red = 20, .green = 20, .blue = 24, .alpha = 255},
        },
    };

    require(
        veyra::render_software(
            bad_scene,
            {
                .width = width,
                .height = height,
                .stride = width,
                .pixels = glyph_pixels,
            },
            {}
        ) == veyra::SoftwareRenderResult::invalid_input,
        "empty glyph draws are rejected"
    );
}

constexpr std::size_t kChurnAtlasWidth = 128;
constexpr std::size_t kChurnAtlasHeight = 128;
constexpr std::size_t kChurnSlots = 256;
constexpr std::uint32_t kChurnGlyphs = 2000;

struct ChurnRooms {
    std::array<veyra::GlyphAtlasSlot, kChurnSlots> slots{};
    std::array<std::uint8_t, kChurnAtlasWidth * kChurnAtlasHeight> coverage{};
    std::array<stbrp_node, kChurnAtlasWidth> nodes{};
};

std::uint8_t churn_byte(std::uint32_t id, std::size_t row, std::size_t column) noexcept {
    return static_cast<std::uint8_t>((id * 31 + row * 13 + column * 7) % 251);
}

std::size_t churn_width(std::uint32_t id) noexcept {
    return 8 + (id % 9);
}

std::size_t churn_height(std::uint32_t id) noexcept {
    return 8 + ((id * 7) % 9);
}

void require_resident_exact(veyra::GlyphAtlas& atlas, std::uint32_t id, std::string_view message) {
    const auto found = atlas.find(test_key(id));

    if (!found.has_value()) {
        return;
    }

    const auto width = churn_width(id);
    const auto height = churn_height(id);

    require(found->width == width && found->height == height, message);
    require(
        found->u + found->width <= kChurnAtlasWidth &&
            found->v + found->height <= kChurnAtlasHeight,
        message
    );

    const auto view = atlas.view();

    for (auto row = std::size_t{}; row < height; ++row) {
        for (auto column = std::size_t{}; column < width; ++column) {
            require(
                view.coverage[(found->v + row) * view.stride + found->u + column] ==
                    churn_byte(id, row, column),
                message
            );
        }
    }
}

void check_cjk_churn() {
    auto rooms = ChurnRooms{};
    auto atlas = veyra::GlyphAtlas(
        std::span<veyra::GlyphAtlasSlot>(rooms.slots),
        std::span<std::uint8_t>(rooms.coverage),
        std::span<stbrp_node>(rooms.nodes),
        kChurnAtlasWidth,
        kChurnAtlasHeight
    );

    require(atlas.valid(), "churn atlas validates");

    auto pixels = std::array<std::uint8_t, 256>{};
    auto evictions = std::size_t{};

    for (auto id = std::uint32_t{1}; id <= kChurnGlyphs; ++id) {
        const auto width = churn_width(id);
        const auto height = churn_height(id);

        for (auto row = std::size_t{}; row < height; ++row) {
            for (auto column = std::size_t{}; column < width; ++column) {
                pixels[row * width + column] = churn_byte(id, row, column);
            }
        }

        const auto result = atlas.insert(test_key(id), test_mask(pixels, width, height));

        require(
            result == veyra::GlyphAtlasResult::inserted ||
                result == veyra::GlyphAtlasResult::inserted_after_eviction,
            "churn inserts pack"
        );

        if (result == veyra::GlyphAtlasResult::inserted_after_eviction) {
            ++evictions;
        }
    }

    require(evictions >= 1, "churn overflows the atlas at least once");
    require(evictions <= 32, "churn clears the atlas a handful of times, not per glyph");
    require(
        atlas.generation() == kChurnGlyphs + evictions,
        "every insert and every clear bumps the generation exactly once"
    );

    for (auto id = std::uint32_t{1}; id <= kChurnGlyphs; ++id) {
        require_resident_exact(atlas, id, "residents stay byte-exact under eviction");
    }

    const auto last = atlas.find(test_key(kChurnGlyphs));
    require(last.has_value(), "the freshest glyph always resolves");

    auto steady_pixels = std::array<std::uint8_t, 256>{};
    steady_pixels.fill(200);

    const auto measure = Measure{};

    for (auto repeat = 0; repeat < 60; ++repeat) {
        require(
            atlas.insert(test_key(5001), test_mask(steady_pixels, 16, 16)) !=
                veyra::GlyphAtlasResult::invalid_workspace,
            "steady traffic packs"
        );
        require(atlas.find(test_key(5001)).has_value(), "steady traffic resolves");
    }

    require(measure.count() == 0, "steady atlas traffic allocates nothing");
}

int run() {
    check_insert_and_find();
    check_eviction_and_clear();
    check_full_bucket_reuses_oldest();
    check_rejections();
    check_glyph_draws();
    check_cjk_churn();

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
