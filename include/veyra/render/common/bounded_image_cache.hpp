#ifndef VEYRA_RENDER_BOUNDED_IMAGE_CACHE_HPP
#define VEYRA_RENDER_BOUNDED_IMAGE_CACHE_HPP

#include "veyra/render/cpu/software_renderer.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <optional>
#include <span>

namespace veyra {

struct CachedImage {
    SoftwareImage image;
    std::int32_t bearing_x{};
    std::int32_t bearing_y{};
};

enum class BoundedImageCacheResult : std::uint8_t {
    inserted,
    updated,
    invalid_workspace,
    invalid_key,
    invalid_image,
    image_too_large,
};

template <typename Key> struct BoundedImageCacheSlot {
    Key key;
    std::size_t width{};
    std::size_t height{};
    std::int32_t bearing_x{};
    std::int32_t bearing_y{};
    std::uint64_t generation{};
    std::uint8_t recency{};
    bool occupied{};
};

template <typename Key, typename Hash, typename KeyValidator> class BoundedImageCache {
  public:
    BoundedImageCache(
        std::span<BoundedImageCacheSlot<Key>> slots,
        std::span<SoftwarePixel> pixel_storage,
        std::size_t maximum_width,
        std::size_t maximum_height
    ) noexcept :
        slots_(slots), pixel_storage_(pixel_storage), maximum_width_(maximum_width),
        maximum_height_(maximum_height) {
        if (slots.empty() || slots.size() % ways_per_bucket != 0 || maximum_width == 0 ||
            maximum_height == 0 ||
            maximum_width > std::numeric_limits<std::size_t>::max() / sizeof(SoftwarePixel) ||
            maximum_width > std::numeric_limits<std::size_t>::max() / maximum_height) {
            return;
        }

        slot_capacity_ = maximum_width * maximum_height;
        if (slots.size() > std::numeric_limits<std::size_t>::max() / slot_capacity_ ||
            pixel_storage.size() < slots.size() * slot_capacity_) {
            return;
        }

        bucket_count_ = slots.size() / ways_per_bucket;
        valid_ = true;
        clear();
    }

    BoundedImageCache(const BoundedImageCache&) = delete;
    BoundedImageCache& operator=(const BoundedImageCache&) = delete;
    BoundedImageCache(BoundedImageCache&&) = delete;
    BoundedImageCache& operator=(BoundedImageCache&&) = delete;

    bool valid() const noexcept {
        return valid_;
    }

    void clear() noexcept {
        if (!valid_) {
            return;
        }
        std::fill(slots_.begin(), slots_.end(), BoundedImageCacheSlot<Key>{});
    }

    bool erase(const Key& key) noexcept {
        if (!valid_ || !KeyValidator{}(key)) {
            return false;
        }

        const auto slot_index = find_slot(key);
        if (slot_index == slots_.size()) {
            return false;
        }

        const auto bucket = bucket_start(key);
        const auto previous_recency = slots_[slot_index].recency;
        slots_[slot_index] = {};
        for (std::size_t way = 0; way < ways_per_bucket; ++way) {
            auto& candidate = slots_[bucket + way];
            if (candidate.occupied && candidate.recency > previous_recency) {
                --candidate.recency;
            }
        }
        return true;
    }

    std::optional<CachedImage> find(const Key& key) noexcept {
        if (!valid_ || !KeyValidator{}(key)) {
            return std::nullopt;
        }

        const auto slot_index = find_slot(key);
        if (slot_index == slots_.size()) {
            return std::nullopt;
        }

        mark_recent(slot_index);
        const auto& slot = slots_[slot_index];
        const auto pixel_count = slot.height * maximum_width_;
        const auto pixels = pixel_storage_.subspan(slot_index * slot_capacity_, pixel_count);
        return CachedImage{
            .image =
                {
                    .width = slot.width,
                    .height = slot.height,
                    .stride = maximum_width_,
                    .pixels = pixels,
                    .generation = slot.generation,
                },
            .bearing_x = slot.bearing_x,
            .bearing_y = slot.bearing_y,
        };
    }

    std::optional<CachedImage> find(const Key& key, std::uint64_t expected_generation) noexcept {
        if (expected_generation == 0) {
            return find(key);
        }

        if (!valid_ || !KeyValidator{}(key)) {
            return std::nullopt;
        }

        const auto slot_index = find_slot(key);
        if (slot_index == slots_.size() || slots_[slot_index].generation != expected_generation) {
            return std::nullopt;
        }

        return find(key);
    }

    BoundedImageCacheResult insert(const Key& key, const CachedImage& image) noexcept {
        if (!valid_) {
            return BoundedImageCacheResult::invalid_workspace;
        }
        if (!KeyValidator{}(key)) {
            return BoundedImageCacheResult::invalid_key;
        }
        if (!valid_image(image.image)) {
            return BoundedImageCacheResult::invalid_image;
        }
        if (image.image.width > maximum_width_ || image.image.height > maximum_height_) {
            return BoundedImageCacheResult::image_too_large;
        }

        const auto bucket = bucket_start(key);
        auto slot_index = find_slot(key);
        const auto updated = slot_index != slots_.size();
        if (updated) {
            mark_recent(slot_index);
        } else {
            auto active_count = std::size_t{0};
            auto oldest = bucket;
            for (std::size_t way = 0; way < ways_per_bucket; ++way) {
                const auto candidate = bucket + way;
                if (!slots_[candidate].occupied) {
                    slot_index = candidate;
                    break;
                }

                ++active_count;
                if (slots_[candidate].recency < slots_[oldest].recency) {
                    oldest = candidate;
                }
            }

            if (slot_index == slots_.size()) {
                slot_index = oldest;
                for (std::size_t way = 0; way < ways_per_bucket; ++way) {
                    const auto candidate = bucket + way;
                    if (candidate != slot_index) {
                        --slots_[candidate].recency;
                    }
                }
                slots_[slot_index] = {};
                slots_[slot_index].recency = static_cast<std::uint8_t>(ways_per_bucket - 1);
            } else {
                slots_[slot_index].recency = static_cast<std::uint8_t>(active_count);
            }
        }

        auto& slot = slots_[slot_index];
        slot.key = key;
        slot.width = image.image.width;
        slot.height = image.image.height;
        slot.bearing_x = image.bearing_x;
        slot.bearing_y = image.bearing_y;
        slot.generation = image.image.generation;
        slot.occupied = true;

        auto* destination = pixel_storage_.data() + slot_index * slot_capacity_;
        for (std::size_t row = 0; row < image.image.height; ++row) {
            std::memmove(
                destination + row * maximum_width_,
                image.image.pixels.data() + row * image.image.stride,
                image.image.width * sizeof(SoftwarePixel)
            );
        }

        return updated ? BoundedImageCacheResult::updated : BoundedImageCacheResult::inserted;
    }

  private:
    static constexpr std::size_t ways_per_bucket = 4;

    std::span<BoundedImageCacheSlot<Key>> slots_;
    std::span<SoftwarePixel> pixel_storage_;
    std::size_t maximum_width_{};
    std::size_t maximum_height_{};
    std::size_t slot_capacity_{};
    std::size_t bucket_count_{};
    bool valid_{};

    static bool valid_image(const SoftwareImage& image) noexcept {
        if (image.stride < image.width ||
            (image.height != 0 &&
             image.stride > std::numeric_limits<std::size_t>::max() / image.height)) {
            return false;
        }
        return image.pixels.size() >= image.height * image.stride;
    }

    std::size_t bucket_start(const Key& key) const noexcept {
        return static_cast<std::size_t>(Hash{}(key) % bucket_count_) * ways_per_bucket;
    }

    std::size_t find_slot(const Key& key) const noexcept {
        const auto bucket = bucket_start(key);
        for (std::size_t way = 0; way < ways_per_bucket; ++way) {
            const auto slot_index = bucket + way;
            if (slots_[slot_index].occupied && slots_[slot_index].key == key) {
                return slot_index;
            }
        }
        return slots_.size();
    }

    void mark_recent(std::size_t slot_index) noexcept {
        auto& slot = slots_[slot_index];
        const auto bucket = slot_index / ways_per_bucket * ways_per_bucket;
        const auto previous_recency = slot.recency;
        auto active_count = std::size_t{0};
        for (std::size_t way = 0; way < ways_per_bucket; ++way) {
            auto& candidate = slots_[bucket + way];
            if (candidate.occupied) {
                ++active_count;
                if (candidate.recency > previous_recency) {
                    --candidate.recency;
                }
            }
        }
        slot.recency = static_cast<std::uint8_t>(active_count - 1);
    }
};

}

#endif
