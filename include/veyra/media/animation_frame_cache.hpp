#ifndef VEYRA_MEDIA_ANIMATION_FRAME_CACHE_HPP
#define VEYRA_MEDIA_ANIMATION_FRAME_CACHE_HPP

#include "veyra/render/bounded_image_cache.hpp"

#include <cstddef>
#include <cstdint>

namespace veyra {

struct AnimationFrameKey {
    std::uint64_t animation_id{};
    std::uint32_t frame_index{};

    friend constexpr bool operator==(const AnimationFrameKey&, const AnimationFrameKey&) = default;
};

struct AnimationFrameHash {
    std::size_t operator()(const AnimationFrameKey& key) const noexcept;
};

struct AnimationFrameKeyValidator {
    bool operator()(const AnimationFrameKey& key) const noexcept;
};

using AnimationFrame = CachedImage;
using AnimationFrameCacheSlot = BoundedImageCacheSlot<AnimationFrameKey>;
using AnimationFrameCacheResult = BoundedImageCacheResult;
using AnimationFrameCache =
    BoundedImageCache<AnimationFrameKey, AnimationFrameHash, AnimationFrameKeyValidator>;

}

#endif
