#include "veyra/files/file_thumbnail_cache.hpp"

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

}

int main() {
    try {
        check_identity_and_freshness();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
