#include "veyra/text/glyph_mask_cache.hpp"

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

veyra::GlyphMaskKey key(std::uint32_t glyph_id) {
    return {
        .face_id = 12,
        .variation_id = 34,
        .glyph_id = glyph_id,
        .pixel_size = 16,
        .scale_x_16_16 = 65'536,
        .scale_y_16_16 = 65'536,
        .mode = veyra::GlyphRasterMode::grayscale,
    };
}

void require_distinct_keys(
    veyra::GlyphMaskCache& cache,
    const veyra::GlyphMaskKey& first,
    const veyra::GlyphMaskKey& second,
    const veyra::GlyphMask& mask
) {
    cache.clear();
    require(
        cache.insert(first, mask) == veyra::GlyphMaskCacheResult::inserted,
        "first cache key inserts"
    );
    require(
        cache.insert(second, mask) == veyra::GlyphMaskCacheResult::inserted,
        "changed key field inserts separately"
    );
    require(cache.find(first).has_value(), "first key remains addressable");
    require(cache.find(second).has_value(), "changed key remains addressable");
}

void check_key_fields() {
    std::array<veyra::GlyphMaskCacheSlot, 4> slots{};
    std::array<std::uint8_t, 16> storage{};
    veyra::GlyphMaskCache cache(slots, storage, 2, 2);
    const std::array<std::uint8_t, 1> coverage{255};
    const veyra::GlyphMask mask{
        .mask = {
            .width = 1,
            .height = 1,
            .stride = 1,
            .coverage = coverage,
            .generation = 0,
        },
    };
    const auto base = key(42);

    auto variant = base;
    variant.face_id += 1;
    require_distinct_keys(cache, base, variant, mask);

    variant = base;
    variant.variation_id += 1;
    require_distinct_keys(cache, base, variant, mask);

    variant = base;
    variant.glyph_id += 1;
    require_distinct_keys(cache, base, variant, mask);

    variant = base;
    variant.pixel_size += 1;
    require_distinct_keys(cache, base, variant, mask);

    variant = base;
    variant.scale_x_16_16 += 1;
    require_distinct_keys(cache, base, variant, mask);

    variant = base;
    variant.scale_y_16_16 += 1;
    require_distinct_keys(cache, base, variant, mask);

    variant = base;
    variant.mode = veyra::GlyphRasterMode::monochrome;
    require_distinct_keys(cache, base, variant, mask);
}

void check_copy_and_update() {
    std::array<veyra::GlyphMaskCacheSlot, 4> slots{};
    std::array<std::uint8_t, 24> storage{};
    veyra::GlyphMaskCache cache(slots, storage, 3, 2);
    const std::array<std::uint8_t, 6> source{10, 11, 90, 20, 21, 91};
    const veyra::GlyphMask mask{
        .mask =
            {
                .width = 2,
                .height = 2,
                .stride = 3,
                .coverage = source,
                .generation = 0,
            },
        .bearing_x = -2,
        .bearing_y = 11,
    };

    require(cache.valid(), "caller storage configures the cache");
    require(
        cache.insert(key(1), mask) == veyra::GlyphMaskCacheResult::inserted,
        "new glyph mask is stored"
    );

    auto cached = cache.find(key(1));
    if (!cached.has_value()) {
        throw std::runtime_error("stored mask is found");
    }
    require(cached->mask.width == 2 && cached->mask.height == 2, "glyph dimensions are retained");
    require(cached->mask.stride == 3, "stored rows use fixed slot pitch");
    require(cached->mask.coverage.size() == 6, "view spans its pitched rows");
    require(cached->bearing_x == -2 && cached->bearing_y == 11, "glyph bearings are retained");
    require(
        cached->mask.coverage[0] == 10 && cached->mask.coverage[1] == 11 &&
            cached->mask.coverage[3] == 20 && cached->mask.coverage[4] == 21,
        "mask rows are copied without their source padding"
    );

    const std::array<std::uint8_t, 2> replacement{40, 41};
    const veyra::GlyphMask replacement_mask{
        .mask =
            {
                .width = 2,
                .height = 1,
                .stride = 2,
                .coverage = replacement,
                .generation = 0,
            },
        .bearing_x = 3,
        .bearing_y = 4,
    };
    require(
        cache.insert(key(1), replacement_mask) == veyra::GlyphMaskCacheResult::updated,
        "existing key updates its pixels"
    );
    cached = cache.find(key(1));
    if (!cached.has_value()) {
        throw std::runtime_error("updated glyph remains cached");
    }
    require(cached->mask.height == 1, "updated glyph dimensions are visible");
    require(cached->bearing_x == 3 && cached->bearing_y == 4, "updated bearings are visible");
    require(
        cached->mask.coverage[0] == 40 && cached->mask.coverage[1] == 41,
        "updated pixels are visible"
    );
}

void check_lru_replacement() {
    std::array<veyra::GlyphMaskCacheSlot, 4> slots{};
    std::array<std::uint8_t, 16> storage{};
    veyra::GlyphMaskCache cache(slots, storage, 2, 2);
    const std::array<std::uint8_t, 1> coverage{255};
    const veyra::GlyphMask mask{
        .mask = {
            .width = 1,
            .height = 1,
            .stride = 1,
            .coverage = coverage,
            .generation = 0,
        },
    };

    for (std::uint32_t glyph = 1; glyph <= 4; ++glyph) {
        require(
            cache.insert(key(glyph), mask) == veyra::GlyphMaskCacheResult::inserted,
            "each way accepts one colliding glyph"
        );
    }
    require(cache.find(key(1)).has_value(), "lookup refreshes a glyph's recency");
    require(
        cache.insert(key(5), mask) == veyra::GlyphMaskCacheResult::inserted,
        "full bucket replaces its least-recently-used glyph"
    );

    require(cache.find(key(1)).has_value(), "recent glyph stays cached");
    require(!cache.find(key(2)).has_value(), "oldest colliding glyph is evicted");
    require(cache.find(key(3)).has_value(), "other glyph remains cached");
    require(cache.find(key(4)).has_value(), "other recent glyph remains cached");
    require(cache.find(key(5)).has_value(), "replacement glyph is cached");

    cache.clear();
    require(!cache.find(key(1)).has_value(), "clear drops all cache keys");
}

void check_invalid_inputs_and_capacity() {
    std::array<veyra::GlyphMaskCacheSlot, 3> wrong_slot_count{};
    std::array<std::uint8_t, 12> wrong_slot_storage{};
    veyra::GlyphMaskCache invalid_cache(wrong_slot_count, wrong_slot_storage, 2, 2);
    require(!invalid_cache.valid(), "slot count must form complete four-way buckets");

    std::array<veyra::GlyphMaskCacheSlot, 4> slots{};
    std::array<std::uint8_t, 15> short_storage{};
    veyra::GlyphMaskCache short_cache(slots, short_storage, 2, 2);
    require(!short_cache.valid(), "storage must fit every fixed-size slot");

    std::array<std::uint8_t, 16> storage{};
    veyra::GlyphMaskCache cache(slots, storage, 2, 2);
    const std::array<std::uint8_t, 4> coverage{1, 2, 3, 4};
    const veyra::GlyphMask mask{
        .mask = {
            .width = 2,
            .height = 2,
            .stride = 2,
            .coverage = coverage,
            .generation = 0,
        },
    };
    auto invalid_key = key(1);
    invalid_key.face_id = 0;
    require(
        cache.insert(invalid_key, mask) == veyra::GlyphMaskCacheResult::invalid_key,
        "zero face identity is rejected"
    );

    const veyra::GlyphMask short_pitch_mask{
        .mask = {
            .width = 2,
            .height = 2,
            .stride = 1,
            .coverage = coverage,
            .generation = 0,
        },
    };
    require(
        cache.insert(key(1), short_pitch_mask) == veyra::GlyphMaskCacheResult::invalid_mask,
        "short source pitch is rejected"
    );

    const std::array<std::uint8_t, 3> short_coverage{};
    const veyra::GlyphMask short_storage_mask{
        .mask = {
            .width = 2,
            .height = 2,
            .stride = 2,
            .coverage = short_coverage,
            .generation = 0,
        },
    };
    require(
        cache.insert(key(1), short_storage_mask) == veyra::GlyphMaskCacheResult::invalid_mask,
        "short source storage is rejected"
    );

    const std::array<std::uint8_t, 8> large_coverage{};
    const veyra::GlyphMask large_mask{
        .mask = {
            .width = 4,
            .height = 2,
            .stride = 4,
            .coverage = large_coverage,
            .generation = 0,
        },
    };
    require(
        cache.insert(key(1), large_mask) == veyra::GlyphMaskCacheResult::glyph_too_large,
        "glyphs larger than configured slots are rejected"
    );
    require(!cache.find(key(1)).has_value(), "rejected inserts do not create cache entries");
}

void check_generation_carried_and_matched() {
    std::array<veyra::GlyphMaskCacheSlot, 4> slots{};
    std::array<std::uint8_t, 16> storage{};
    veyra::GlyphMaskCache cache(slots, storage, 2, 2);
    const std::array<std::uint8_t, 1> coverage{200};
    const veyra::GlyphMask first{
        .mask =
            {
                .width = 1,
                .height = 1,
                .stride = 1,
                .coverage = coverage,
                .generation = 7,
            },
        .bearing_x = 1,
        .bearing_y = 2,
    };

    require(
        cache.insert(key(1), first) == veyra::GlyphMaskCacheResult::inserted,
        "a versioned glyph inserts"
    );

    const auto stored = cache.find(key(1));
    if (!stored.has_value()) {
        throw std::runtime_error("versioned glyph is found");
    }
    require(stored->mask.generation == 7, "find carries the stored generation");
    require(cache.find(key(1), 7).has_value(), "a matching generation is a hit");
    require(cache.find(key(1), 0).has_value(), "an unversioned lookup skips the check");
    require(!cache.find(key(1), 8).has_value(), "a generation mismatch is a miss");

    const std::array<std::uint8_t, 1> replacement{44};
    const veyra::GlyphMask second{
        .mask =
            {
                .width = 1,
                .height = 1,
                .stride = 1,
                .coverage = replacement,
                .generation = 8,
            },
        .bearing_x = 1,
        .bearing_y = 2,
    };
    require(
        cache.insert(key(1), second) == veyra::GlyphMaskCacheResult::updated,
        "repainting the same key overwrites its pixels"
    );

    const auto updated = cache.find(key(1), 8);
    if (!updated.has_value()) {
        throw std::runtime_error("the bumped generation is found");
    }
    require(updated->mask.coverage[0] == 44, "the bumped pixels are visible");
    require(!cache.find(key(1), 7).has_value(), "the old generation no longer matches");
}

}

int main() {
    try {
        check_copy_and_update();
        check_key_fields();
        check_lru_replacement();
        check_invalid_inputs_and_capacity();
        check_generation_carried_and_matched();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
