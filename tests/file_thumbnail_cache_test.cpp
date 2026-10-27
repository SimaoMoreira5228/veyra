#include "veyra/files/thumbnails/file_thumbnail_cache.hpp"

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

void check_identity_and_freshness() {
    std::array<veyra::FileThumbnailCacheSlot, 4> slots{};
    std::array<veyra::SoftwarePixel, 16> pixel_storage{};
    auto cache = veyra::FileThumbnailCache(slots, pixel_storage, 2, 2);
    const auto pixels = std::array{
        veyra::SoftwarePixel{.red = 21, .alpha = 255},
        veyra::SoftwarePixel{.green = 32, .alpha = 255},
    };
    const auto thumbnail = veyra::FileThumbnail{
        .image = {
            .width = 2,
            .height = 1,
            .stride = 2,
            .pixels = pixels,
        },
    };
    const auto key = veyra::FileThumbnailKey{
        .file_id = veyra::FileId{1, 1},
        .size_bytes = 4096,
        .modified_nanoseconds = 17,
    };

    require(cache.valid(), "caller storage configures the thumbnail cache");
    require(
        cache.insert({}, thumbnail) == veyra::FileThumbnailCacheResult::invalid_key,
        "cache rejects keys without a filesystem identity"
    );
    require(
        cache.insert(key, thumbnail) == veyra::FileThumbnailCacheResult::inserted,
        "thumbnail inserts for a file identity"
    );

    const auto cached = cache.find(key);
    if (!cached) {
        throw std::runtime_error("thumbnail lookup hits for matching metadata");
    }
    require(
        cached->image.pixels[0].red == 21 && cached->image.pixels[1].green == 32,
        "thumbnail pixels are retained"
    );

    auto changed_time = key;
    ++changed_time.modified_nanoseconds;
    require(!cache.find(changed_time).has_value(), "new modification time misses the old image");

    auto changed_size = key;
    ++changed_size.size_bytes;
    require(!cache.find(changed_size).has_value(), "new file size misses the old image");

    auto changed_identity = key;
    ++changed_identity.file_id.object;
    require(!cache.find(changed_identity).has_value(), "different file identity misses the image");

    changed_identity = key;
    ++changed_identity.file_id.volume;
    require(!cache.find(changed_identity).has_value(), "different volumes miss the image");
}

void check_least_recently_used_eviction() {
    auto slots = std::array<veyra::FileThumbnailCacheSlot, 4>{};
    auto pixel_storage = std::array<veyra::SoftwarePixel, 4>{};
    auto cache = veyra::FileThumbnailCache(slots, pixel_storage, 1, 1);
    auto keys = std::array<veyra::FileThumbnailKey, 5>{};
    auto pixel = veyra::SoftwarePixel{.alpha = 255};
    const auto image = veyra::FileThumbnail{
        .image = {
            .width = 1,
            .height = 1,
            .stride = 1,
            .pixels = std::span<const veyra::SoftwarePixel>{&pixel, 1},
        },
    };

    for (std::size_t index = 0; index < slots.size(); ++index) {
        keys[index].file_id = veyra::FileId{1, index + 1};
        require(
            cache.insert(keys[index], image) == veyra::FileThumbnailCacheResult::inserted,
            "colliding thumbnails enter the cache"
        );
    }

    require(cache.find(keys[0]).has_value(), "a hit refreshes the thumbnail's recency");
    keys[4].file_id = veyra::FileId{1, 5};
    require(
        cache.insert(keys[4], image) == veyra::FileThumbnailCacheResult::inserted,
        "a colliding thumbnail replaces one cached image"
    );
    require(!cache.find(keys[1]).has_value(), "least recent thumbnail is evicted");
    require(cache.find(keys[0]).has_value(), "recent thumbnail survives eviction");
    require(cache.find(keys[2]).has_value(), "unexpired thumbnail remains available");
    require(cache.find(keys[3]).has_value(), "newest thumbnail remains available");
    require(cache.find(keys[4]).has_value(), "replacement thumbnail is available");
}

void check_erase_reuses_bucket_and_preserves_recency() {
    auto slots = std::array<veyra::FileThumbnailCacheSlot, 4>{};
    auto pixel_storage = std::array<veyra::SoftwarePixel, 4>{};
    auto cache = veyra::FileThumbnailCache(slots, pixel_storage, 1, 1);
    auto keys = std::array<veyra::FileThumbnailKey, 6>{};
    auto pixel = veyra::SoftwarePixel{.alpha = 255};
    const auto image = veyra::FileThumbnail{
        .image = {
            .width = 1,
            .height = 1,
            .stride = 1,
            .pixels = std::span<const veyra::SoftwarePixel>{&pixel, 1},
        },
    };

    for (std::size_t index = 0; index < 4; ++index) {
        keys[index].file_id = veyra::FileId{2, index + 1};
        require(
            cache.insert(keys[index], image) == veyra::FileThumbnailCacheResult::inserted,
            "cache fills a collision bucket before erasure"
        );
    }

    require(cache.find(keys[0]).has_value(), "first key becomes recent");
    require(cache.find(keys[1]).has_value(), "second key becomes most recent");
    require(cache.erase(keys[3]), "cache erases a matching key");
    require(!cache.erase(keys[3]), "erasing an absent key reports no change");
    require(!cache.erase({}), "erasing an invalid key does not touch the cache");

    keys[4].file_id = veyra::FileId{2, 5};
    keys[5].file_id = veyra::FileId{2, 6};
    require(
        cache.insert(keys[4], image) == veyra::FileThumbnailCacheResult::inserted,
        "cache reuses an erased bucket slot before evicting"
    );
    require(
        cache.insert(keys[5], image) == veyra::FileThumbnailCacheResult::inserted,
        "a full bucket evicts after slot reuse"
    );
    require(!cache.find(keys[2]).has_value(), "erasure preserves least-recently-used ordering");
    require(cache.find(keys[0]).has_value(), "recent keys survive the next eviction");
    require(cache.find(keys[1]).has_value(), "the most recent key survives the next eviction");
}

}

int main() {
    try {
        check_identity_and_freshness();
        check_least_recently_used_eviction();
        check_erase_reuses_bucket_and_preserves_recency();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
