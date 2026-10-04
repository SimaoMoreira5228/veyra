#include "veyra/text/glyph_image_cache.hpp"

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

veyra::GlyphImageKey key(std::uint32_t glyph_id) {
    return {
        .face_id = 12,
        .variation_id = 34,
        .glyph_id = glyph_id,
        .pixel_size = 16,
    };
}

void check_copy_update_and_eviction() {
    std::array<veyra::GlyphImageCacheSlot, 4> slots{};
    std::array<veyra::SoftwarePixel, 16> storage{};
    veyra::GlyphImageCache cache(slots, storage, 2, 2);
    const std::array<veyra::SoftwarePixel, 6> source{
        veyra::SoftwarePixel{.red = 1, .alpha = 255},
        veyra::SoftwarePixel{.red = 2, .alpha = 255},
        veyra::SoftwarePixel{.red = 99, .alpha = 255},
        veyra::SoftwarePixel{.red = 3, .alpha = 255},
        veyra::SoftwarePixel{.red = 4, .alpha = 255},
        veyra::SoftwarePixel{.red = 99, .alpha = 255},
    };
    const veyra::GlyphImage image{
        .image =
            {
                .width = 2,
                .height = 2,
                .stride = 3,
                .pixels = source,
            },
        .bearing_x = -2,
        .bearing_y = 11,
    };

    require(cache.valid(), "caller storage configures the image cache");
    require(
        cache.insert(key(1), image) == veyra::GlyphImageCacheResult::inserted,
        "new color glyph is stored"
    );
    auto cached = cache.find(key(1));
    if (!cached.has_value()) {
        throw std::runtime_error("stored image is found");
    }
    require(cached->image.stride == 2, "cached rows use fixed slot pitch");
    require(
        cached->image.pixels[0].red == 1 && cached->image.pixels[1].red == 2 &&
            cached->image.pixels[2].red == 3 && cached->image.pixels[3].red == 4,
        "copy skips source row padding"
    );
    require(cached->bearing_x == -2 && cached->bearing_y == 11, "bearings are retained");

    const std::array<veyra::SoftwarePixel, 1> replacement{
        veyra::SoftwarePixel{.green = 8, .alpha = 255},
    };
    require(
        cache.insert(
            key(1),
            {{.width = 1, .height = 1, .stride = 1, .pixels = replacement}, 3, 4}
        ) == veyra::GlyphImageCacheResult::updated,
        "existing key updates its pixels"
    );
    cached = cache.find(key(1));
    if (!cached.has_value()) {
        throw std::runtime_error("updated image is found");
    }
    require(cached->image.pixels[0].green == 8, "updated pixel is visible");
    require(cached->bearing_x == 3 && cached->bearing_y == 4, "updated bearings are visible");

    for (std::uint32_t glyph = 2; glyph <= 5; ++glyph) {
        require(
            cache.insert(key(glyph), image) == veyra::GlyphImageCacheResult::inserted,
            "colliding images insert"
        );
    }
    require(cache.find(key(2)).has_value(), "lookup refreshes recency");
    require(
        cache.insert(key(6), image) == veyra::GlyphImageCacheResult::inserted,
        "full bucket replaces least recent image"
    );
    require(!cache.find(key(3)).has_value(), "least recent colliding image is evicted");

    cache.clear();
    require(!cache.find(key(1)).has_value(), "clear drops all cache entries");

    const auto default_palette_key = key(1);
    auto alternate_palette_key = default_palette_key;
    alternate_palette_key.palette_id = 1;
    require(
        cache.insert(default_palette_key, image) == veyra::GlyphImageCacheResult::inserted,
        "default palette image inserts"
    );
    require(
        cache.insert(
            alternate_palette_key,
            {{.width = 1, .height = 1, .stride = 1, .pixels = replacement}, 0, 0}
        ) == veyra::GlyphImageCacheResult::inserted,
        "alternate palette has a distinct cache entry"
    );
    const auto default_palette_image = cache.find(default_palette_key);
    const auto alternate_palette_image = cache.find(alternate_palette_key);
    if (!default_palette_image.has_value() || !alternate_palette_image.has_value()) {
        throw std::runtime_error("both palette images remain addressable");
    }
    require(
        default_palette_image->image.pixels[0].red == 1 &&
            alternate_palette_image->image.pixels[0].green == 8,
        "each palette returns its own pixels"
    );
}

void check_invalid_storage_and_input() {
    std::array<veyra::GlyphImageCacheSlot, 3> wrong_slots{};
    std::array<veyra::SoftwarePixel, 16> storage{};
    veyra::GlyphImageCache invalid_cache(wrong_slots, storage, 2, 2);
    require(!invalid_cache.valid(), "slot count must form complete four-way buckets");

    std::array<veyra::GlyphImageCacheSlot, 4> slots{};
    std::array<veyra::SoftwarePixel, 15> short_storage{};
    veyra::GlyphImageCache short_cache(slots, short_storage, 2, 2);
    require(!short_cache.valid(), "storage must fit all fixed-size slots");

    veyra::GlyphImageCache cache(slots, storage, 2, 2);
    const std::array<veyra::SoftwarePixel, 4> pixels{};
    const veyra::GlyphImage image{
        .image = {.width = 2, .height = 2, .stride = 2, .pixels = pixels},
    };
    auto invalid_key = key(1);
    invalid_key.face_id = 0;
    require(
        cache.insert(invalid_key, image) == veyra::GlyphImageCacheResult::invalid_key,
        "zero face identity is rejected"
    );

    const veyra::GlyphImage invalid_image{
        .image = {.width = 2, .height = 2, .stride = 1, .pixels = pixels},
    };
    require(
        cache.insert(key(1), invalid_image) == veyra::GlyphImageCacheResult::invalid_image,
        "short image pitch is rejected"
    );

    const std::array<veyra::SoftwarePixel, 1> short_pixels{};
    const veyra::GlyphImage short_image{
        .image = {.width = 2, .height = 2, .stride = 2, .pixels = short_pixels},
    };
    require(
        cache.insert(key(1), short_image) == veyra::GlyphImageCacheResult::invalid_image,
        "short pixel storage is rejected"
    );

    const std::array<veyra::SoftwarePixel, 6> oversized_pixels{};
    const veyra::GlyphImage oversized{
        .image = {.width = 3, .height = 2, .stride = 3, .pixels = oversized_pixels},
    };
    require(
        cache.insert(key(1), oversized) == veyra::GlyphImageCacheResult::image_too_large,
        "images larger than configured slots are rejected"
    );
}

}

int main() {
    try {
        check_copy_update_and_eviction();
        check_invalid_storage_and_input();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
