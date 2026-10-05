#ifndef VEYRA_FILES_FILE_THUMBNAIL_CACHE_HPP
#define VEYRA_FILES_FILE_THUMBNAIL_CACHE_HPP

#include "veyra/files/file_id.hpp"
#include "veyra/render/bounded_image_cache.hpp"

#include <cstddef>
#include <cstdint>

namespace veyra {

struct FileThumbnailKey {
    FileId file_id;
    std::uint64_t size_bytes{};
    std::int64_t modified_nanoseconds{};

    friend constexpr bool operator==(const FileThumbnailKey&, const FileThumbnailKey&) = default;
};

struct FileThumbnailHash {
    std::size_t operator()(const FileThumbnailKey& key) const noexcept {
        const auto mix = [](std::uint64_t value) noexcept {
            value += 0x9e3779b97f4a7c15ULL;
            value = (value ^ (value >> 30U)) * 0xbf58476d1ce4e5b9ULL;
            value = (value ^ (value >> 27U)) * 0x94d049bb133111ebULL;
            return value ^ (value >> 31U);
        };

        auto hash = mix(key.file_id.volume);
        hash ^= mix(key.file_id.object + hash);
        hash ^= mix(key.size_bytes + hash);
        hash ^= mix(static_cast<std::uint64_t>(key.modified_nanoseconds) + hash);
        return static_cast<std::size_t>(mix(hash));
    }
};

struct FileThumbnailKeyValidator {
    bool operator()(const FileThumbnailKey& key) const noexcept {
        return key.file_id.valid();
    }
};

using FileThumbnail = CachedImage;
using FileThumbnailCacheSlot = BoundedImageCacheSlot<FileThumbnailKey>;
using FileThumbnailCacheResult = BoundedImageCacheResult;
using FileThumbnailCache =
    BoundedImageCache<FileThumbnailKey, FileThumbnailHash, FileThumbnailKeyValidator>;

}

#endif
