#ifndef VEYRA_TEXT_GLYPH_IMAGE_CACHE_HPP
#define VEYRA_TEXT_GLYPH_IMAGE_CACHE_HPP

#include "veyra/render/resource/bounded_image_cache.hpp"

#include <cstddef>
#include <cstdint>

namespace veyra {

struct GlyphImageKey {
    std::uint64_t face_id{};
    std::uint64_t variation_id{};
    std::uint32_t glyph_id{};
    std::uint32_t pixel_size{};
    std::uint32_t palette_id{};
    std::uint32_t scale_x_16_16{65'536};
    std::uint32_t scale_y_16_16{65'536};

    friend constexpr bool operator==(const GlyphImageKey&, const GlyphImageKey&) = default;
};

struct GlyphImageHash {
    std::size_t operator()(const GlyphImageKey& key) const noexcept;
};

struct GlyphImageKeyValidator {
    bool operator()(const GlyphImageKey& key) const noexcept;
};

using GlyphImage = CachedImage;
using GlyphImageCacheSlot = BoundedImageCacheSlot<GlyphImageKey>;
using GlyphImageCacheResult = BoundedImageCacheResult;
using GlyphImageCache = BoundedImageCache<GlyphImageKey, GlyphImageHash, GlyphImageKeyValidator>;

}

namespace veyra::text {
using ::veyra::GlyphImage;
using ::veyra::GlyphImageCache;
using ::veyra::GlyphImageCacheResult;
using ::veyra::GlyphImageCacheSlot;
using ::veyra::GlyphImageHash;
using ::veyra::GlyphImageKey;
using ::veyra::GlyphImageKeyValidator;
}

namespace veyra::text::rendering {
using ::veyra::GlyphImage;
using ::veyra::GlyphImageCache;
using ::veyra::GlyphImageCacheResult;
using ::veyra::GlyphImageCacheSlot;
using ::veyra::GlyphImageHash;
using ::veyra::GlyphImageKey;
using ::veyra::GlyphImageKeyValidator;
}

#endif
