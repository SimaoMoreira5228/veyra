#include "veyra/media/animation_frame_cache.hpp"

#include <array>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

void require(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

veyra::AnimationFrame frame(const veyra::SoftwarePixel& pixel) {
    return {
        .image = {
            .width = 1,
            .height = 1,
            .stride = 1,
            .pixels = std::span<const veyra::SoftwarePixel>(&pixel, 1),
        },
    };
}

void check_frame_identity_and_eviction() {
    std::array<veyra::AnimationFrameCacheSlot, 4> slots{};
    std::array<veyra::SoftwarePixel, 4> storage{};
    veyra::AnimationFrameCache cache(slots, storage, 1, 1);
    const std::array<veyra::SoftwarePixel, 5> pixels{
        veyra::SoftwarePixel{.red = 1, .alpha = 255},
        veyra::SoftwarePixel{.red = 2, .alpha = 255},
        veyra::SoftwarePixel{.red = 3, .alpha = 255},
        veyra::SoftwarePixel{.red = 4, .alpha = 255},
        veyra::SoftwarePixel{.red = 5, .alpha = 255},
    };
    const std::array<veyra::AnimationFrameKey, 4> keys{
        veyra::AnimationFrameKey{.animation_id = 1, .frame_index = 0},
        veyra::AnimationFrameKey{.animation_id = 1, .frame_index = 1},
        veyra::AnimationFrameKey{.animation_id = 2, .frame_index = 0},
        veyra::AnimationFrameKey{.animation_id = 2, .frame_index = 1},
    };

    require(cache.valid(), "caller storage configures the frame cache");
    for (std::size_t index = 0; index < keys.size(); ++index) {
        require(
            cache.insert(keys[index], frame(pixels[index])) ==
                veyra::AnimationFrameCacheResult::inserted,
            "each animation frame gets a cache entry"
        );
    }

    const auto animation_one_first = cache.find(keys[0]);
    const auto animation_two_first = cache.find(keys[2]);
    if (!animation_one_first.has_value() || !animation_two_first.has_value()) {
        throw std::runtime_error("frame indexes are scoped to their animation identity");
    }
    require(
        animation_one_first->image.pixels[0].red == 1 &&
            animation_two_first->image.pixels[0].red == 3,
        "separate animation frames keep separate pixels"
    );

    const veyra::AnimationFrameKey fifth_key{.animation_id = 3, .frame_index = 0};
    require(
        cache.insert(fifth_key, frame(pixels[4])) == veyra::AnimationFrameCacheResult::inserted,
        "full bucket replaces its least recently used frame"
    );
    require(!cache.find(keys[1]).has_value(), "least recent frame is evicted");
    require(cache.find(keys[0]).has_value(), "recent frame remains cached");
}

void check_validation_and_capacity() {
    std::array<veyra::AnimationFrameCacheSlot, 4> slots{};
    std::array<veyra::SoftwarePixel, 16> storage{};
    veyra::AnimationFrameCache cache(slots, storage, 2, 2);
    const veyra::SoftwarePixel pixel{.blue = 9, .alpha = 255};
    const auto image = frame(pixel);
    const veyra::AnimationFrameKey invalid_key{.animation_id = 0, .frame_index = 0};
    require(cache.valid(), "configured frame cache has enough pixel storage");
    require(
        cache.insert(invalid_key, image) == veyra::AnimationFrameCacheResult::invalid_key,
        "zero animation identity is rejected"
    );

    std::array<veyra::SoftwarePixel, 9> oversized_pixels{};
    const veyra::AnimationFrame oversized{
        .image = {
            .width = 3,
            .height = 3,
            .stride = 3,
            .pixels = oversized_pixels,
        },
    };
    require(
        cache.insert({.animation_id = 1, .frame_index = 0}, oversized) ==
            veyra::AnimationFrameCacheResult::image_too_large,
        "frames larger than the configured slot are rejected"
    );
}

}

int main() {
    try {
        check_frame_identity_and_eviction();
        check_validation_and_capacity();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
