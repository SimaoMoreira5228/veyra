#include "veyra/text/glyph_mask_cache.hpp"

#include <algorithm>
#include <cstring>
#include <limits>

namespace veyra {
namespace {

constexpr std::size_t ways_per_bucket = 4;

std::uint64_t mix_hash(std::uint64_t value) noexcept {
    value += 0x9e3779b97f4a7c15ULL;
    value = (value ^ (value >> 30U)) * 0xbf58476d1ce4e5b9ULL;
    value = (value ^ (value >> 27U)) * 0x94d049bb133111ebULL;
    return value ^ (value >> 31U);
}

std::uint64_t key_hash(const GlyphMaskKey& key) noexcept {
    auto hash = mix_hash(key.face_id);
    hash ^= mix_hash(key.variation_id + hash);
    hash ^= mix_hash((static_cast<std::uint64_t>(key.glyph_id) << 32U) | key.pixel_size);
    hash ^= mix_hash((static_cast<std::uint64_t>(key.scale_x_16_16) << 32U) | key.scale_y_16_16);
    hash ^= mix_hash(static_cast<std::uint64_t>(key.mode));
    return mix_hash(hash);
}

bool valid_key(const GlyphMaskKey& key) noexcept {
    if (key.face_id == 0 || key.pixel_size == 0 || key.scale_x_16_16 == 0 ||
        key.scale_y_16_16 == 0) {
        return false;
    }

    switch (key.mode) {
    case GlyphRasterMode::grayscale:
    case GlyphRasterMode::monochrome:
        return true;
    }
    return false;
}

bool valid_mask(const SoftwareMask& mask) noexcept {
    if (mask.stride < mask.width ||
        (mask.height != 0 && mask.stride > std::numeric_limits<std::size_t>::max() / mask.height)) {
        return false;
    }
    return mask.coverage.size() >= mask.height * mask.stride;
}

}

GlyphMaskCache::GlyphMaskCache(
    std::span<GlyphMaskCacheSlot> slots,
    std::span<std::uint8_t> coverage_storage,
    std::size_t maximum_width,
    std::size_t maximum_height
) noexcept :
    slots_(slots), coverage_storage_(coverage_storage), maximum_width_(maximum_width),
    maximum_height_(maximum_height) {
    if (slots.empty() || slots.size() % ways_per_bucket != 0 || maximum_width == 0 ||
        maximum_height == 0 ||
        maximum_width > std::numeric_limits<std::size_t>::max() / maximum_height) {
        return;
    }

    slot_capacity_ = maximum_width * maximum_height;
    if (slots.size() > std::numeric_limits<std::size_t>::max() / slot_capacity_ ||
        coverage_storage.size() < slots.size() * slot_capacity_) {
        return;
    }

    bucket_count_ = slots.size() / ways_per_bucket;
    valid_ = true;
    clear();
}

bool GlyphMaskCache::valid() const noexcept {
    return valid_;
}

void GlyphMaskCache::clear() noexcept {
    if (!valid_) {
        return;
    }
    std::fill(slots_.begin(), slots_.end(), GlyphMaskCacheSlot{});
}

std::optional<GlyphMask> GlyphMaskCache::find(const GlyphMaskKey& key) noexcept {
    if (!valid_ || !valid_key(key)) {
        return std::nullopt;
    }

    const auto slot_index = find_slot(key);
    if (slot_index == slots_.size()) {
        return std::nullopt;
    }

    mark_recent(slot_index);
    const auto& slot = slots_[slot_index];
    const auto coverage_size = slot.height * maximum_width_;
    const auto storage = coverage_storage_.subspan(slot_index * slot_capacity_, coverage_size);
    return GlyphMask{
        .mask =
            {
                .width = slot.width,
                .height = slot.height,
                .stride = maximum_width_,
                .coverage = storage,
            },
        .bearing_x = slot.bearing_x,
        .bearing_y = slot.bearing_y,
    };
}

GlyphMaskCacheResult
GlyphMaskCache::insert(const GlyphMaskKey& key, const GlyphMask& mask) noexcept {
    if (!valid_) {
        return GlyphMaskCacheResult::invalid_workspace;
    }
    if (!valid_key(key)) {
        return GlyphMaskCacheResult::invalid_key;
    }
    if (!valid_mask(mask.mask)) {
        return GlyphMaskCacheResult::invalid_mask;
    }
    if (mask.mask.width > maximum_width_ || mask.mask.height > maximum_height_) {
        return GlyphMaskCacheResult::glyph_too_large;
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
    slot.width = mask.mask.width;
    slot.height = mask.mask.height;
    slot.bearing_x = mask.bearing_x;
    slot.bearing_y = mask.bearing_y;
    slot.occupied = true;

    if (mask.mask.width != 0) {
        auto* destination = coverage_storage_.data() + slot_index * slot_capacity_;
        for (std::size_t row = 0; row < mask.mask.height; ++row) {
            std::memmove(
                destination + row * maximum_width_,
                mask.mask.coverage.data() + row * mask.mask.stride,
                mask.mask.width
            );
        }
    }

    return updated ? GlyphMaskCacheResult::updated : GlyphMaskCacheResult::inserted;
}

std::size_t GlyphMaskCache::bucket_start(const GlyphMaskKey& key) const noexcept {
    return static_cast<std::size_t>(key_hash(key) % bucket_count_) * ways_per_bucket;
}

std::size_t GlyphMaskCache::find_slot(const GlyphMaskKey& key) const noexcept {
    const auto bucket = bucket_start(key);
    for (std::size_t way = 0; way < ways_per_bucket; ++way) {
        const auto slot_index = bucket + way;
        if (slots_[slot_index].occupied && slots_[slot_index].key == key) {
            return slot_index;
        }
    }
    return slots_.size();
}

void GlyphMaskCache::mark_recent(std::size_t slot_index) noexcept {
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

}
