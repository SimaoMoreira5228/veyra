#include "veyra/text/rendering/glyph_bounds_cache.hpp"

#include <array>
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

veyra::GlyphBoundsKey key_for(std::uint32_t scale) {
    return veyra::GlyphBoundsKey{
        .face_id = 3,
        .glyph_id = 65,
        .pixel_size = 16,
        .scale_x_16_16 = scale,
        .scale_y_16_16 = scale,
    };
}

void check_insert_and_find() {
    std::array<veyra::GlyphBoundsCacheSlot, 4> slots{};
    veyra::GlyphBoundsCache cache(slots);
    require(cache.valid(), "provided workspace is valid");

    const auto key = key_for(65'536);
    auto bounds = veyra::GlyphBounds{};
    require(
        cache.find(key, bounds) == veyra::GlyphBoundsCacheResult::miss,
        "an empty cache misses"
    );

    const auto stored = veyra::GlyphBounds{
        .width = 9,
        .height = 12,
        .bearing_x = 1,
        .bearing_y = 10,
    };
    require(
        cache.insert(key, stored) == veyra::GlyphBoundsCacheResult::stored,
        "measured bounds enter the cache"
    );
    require(cache.find(key, bounds) == veyra::GlyphBoundsCacheResult::hit, "identical bounds hit");
    require(
        bounds.width == 9 && bounds.height == 12 && bounds.bearing_x == 1 && bounds.bearing_y == 10,
        "a hit returns the stored bounds unchanged"
    );

    const auto found = cache.find(key);
    require(found.has_value(), "the optional lookup hits as well");
    require(
        found->width == 9 && found->height == 12 && found->bearing_x == 1 && found->bearing_y == 10,
        "the optional lookup carries identical bounds"
    );
}

void check_scale_and_face_isolate() {
    std::array<veyra::GlyphBoundsCacheSlot, 8> slots{};
    veyra::GlyphBoundsCache cache(slots);
    const auto stored = veyra::GlyphBounds{
        .width = 9,
        .height = 12,
        .bearing_x = 1,
        .bearing_y = 10,
    };
    require(
        cache.insert(key_for(65'536), stored) == veyra::GlyphBoundsCacheResult::stored,
        "reference bounds enter the cache"
    );

    auto bounds = veyra::GlyphBounds{};
    require(
        cache.find(key_for(131'072), bounds) == veyra::GlyphBoundsCacheResult::miss,
        "a scale change misses instead of serving stale ink"
    );

    auto other_face = key_for(65'536);
    other_face.face_id = 4;
    require(
        cache.find(other_face, bounds) == veyra::GlyphBoundsCacheResult::miss,
        "a face change misses"
    );

    auto other_glyph = key_for(65'536);
    other_glyph.glyph_id = 66;
    require(
        cache.find(other_glyph, bounds) == veyra::GlyphBoundsCacheResult::miss,
        "a glyph change misses"
    );

    auto mono = key_for(65'536);
    mono.mode = veyra::GlyphRasterMode::monochrome;
    require(
        cache.find(mono, bounds) == veyra::GlyphBoundsCacheResult::miss,
        "a raster mode change misses"
    );

    require(
        cache.find(key_for(65'536), bounds) == veyra::GlyphBoundsCacheResult::hit,
        "the reference entry still hits"
    );
    require(bounds.width == 9 && bounds.height == 12, "the reference entry keeps its bounds");
}

void check_limits_and_empty_bounds() {
    std::array<veyra::GlyphBoundsCacheSlot, 1> slots{};
    veyra::GlyphBoundsCache invalid_cache({});
    require(!invalid_cache.valid(), "empty workspace is rejected");
    auto bounds = veyra::GlyphBounds{};
    require(
        invalid_cache.find(key_for(65'536), bounds) ==
            veyra::GlyphBoundsCacheResult::invalid_workspace,
        "an invalid cache reports its state"
    );

    veyra::GlyphBoundsCache cache(slots);
    auto bad_key = key_for(65'536);
    bad_key.face_id = 0;
    require(
        cache.find(bad_key, bounds) == veyra::GlyphBoundsCacheResult::invalid_key,
        "a zero face is rejected"
    );
    require(
        cache.insert(bad_key, {}) == veyra::GlyphBoundsCacheResult::invalid_key,
        "a zero face never stores"
    );

    const auto negative = veyra::GlyphBounds{.width = -1};
    require(
        cache.insert(key_for(65'536), negative) == veyra::GlyphBoundsCacheResult::invalid_bounds,
        "negative extents never store"
    );

    require(
        cache.insert(key_for(65'536), {}) == veyra::GlyphBoundsCacheResult::stored,
        "empty bounds cache like empty ink"
    );
    require(
        cache.find(key_for(65'536), bounds) == veyra::GlyphBoundsCacheResult::hit,
        "empty bounds read back"
    );
    require(bounds.width == 0 && bounds.height == 0, "empty bounds stay empty");

    cache.clear();
    require(
        cache.find(key_for(65'536), bounds) == veyra::GlyphBoundsCacheResult::miss,
        "clear invalidates every entry"
    );
}

}

int main() {
    try {
        check_insert_and_find();
        check_scale_and_face_isolate();
        check_limits_and_empty_bounds();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
