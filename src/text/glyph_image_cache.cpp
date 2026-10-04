#include "veyra/text/glyph_image_cache.hpp"

namespace veyra {
namespace {

std::uint64_t mix_hash(std::uint64_t value) noexcept {
    value += 0x9e3779b97f4a7c15ULL;
    value = (value ^ (value >> 30U)) * 0xbf58476d1ce4e5b9ULL;
    value = (value ^ (value >> 27U)) * 0x94d049bb133111ebULL;
    return value ^ (value >> 31U);
}

}

std::size_t GlyphImageHash::operator()(const GlyphImageKey& key) const noexcept {
    auto hash = mix_hash(key.face_id);
    hash ^= mix_hash(key.variation_id + hash);
    hash ^= mix_hash((static_cast<std::uint64_t>(key.glyph_id) << 32U) | key.pixel_size);
    hash ^= mix_hash((static_cast<std::uint64_t>(key.scale_x_16_16) << 32U) | key.scale_y_16_16);
    hash ^= mix_hash(key.palette_id + hash);
    return static_cast<std::size_t>(mix_hash(hash));
}

bool GlyphImageKeyValidator::operator()(const GlyphImageKey& key) const noexcept {
    return key.face_id != 0 && key.pixel_size != 0 && key.scale_x_16_16 != 0 &&
        key.scale_y_16_16 != 0;
}

}
