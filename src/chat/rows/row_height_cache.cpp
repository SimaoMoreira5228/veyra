#include "veyra/chat/rows/row_height_cache.hpp"

#include <bit>
#include <cmath>
#include <limits>

namespace veyra {
namespace {

std::uint64_t mix_hash(std::uint64_t value) noexcept {
    value += 0x9e3779b97f4a7c15ULL;
    value = (value ^ (value >> 30U)) * 0xbf58476d1ce4e5b9ULL;
    value = (value ^ (value >> 27U)) * 0x94d049bb133111ebULL;
    return value ^ (value >> 31U);
}

std::size_t hash_key(ChatRowHeightKey key) noexcept {
    auto hash = mix_hash(key.message_id);
    const auto width_bits =
        static_cast<std::uint64_t>(std::bit_cast<std::uint32_t>(key.available_width));
    hash ^= mix_hash(width_bits + hash);
    hash ^= mix_hash(key.style_generation + hash);
    return static_cast<std::size_t>(mix_hash(hash));
}

}

ChatRowHeightCache::ChatRowHeightCache(std::span<ChatRowHeightCacheSlot> slots) noexcept :
    slots_(slots) {
    if (slots.empty() || slots.size() % ways_per_bucket != 0) {
        return;
    }

    bucket_count_ = slots.size() / ways_per_bucket;
    valid_ = true;
    clear();
}

bool ChatRowHeightCache::valid() const noexcept {
    return valid_;
}

void ChatRowHeightCache::clear() noexcept {
    if (!valid_) {
        return;
    }

    for (auto& slot : slots_) {
        slot = {};
    }
}

ChatRowHeightCacheResult ChatRowHeightCache::find(ChatRowHeightKey key, float& height) noexcept {
    if (!valid_) {
        return ChatRowHeightCacheResult::invalid_workspace;
    }
    if (!valid_key(key)) {
        return ChatRowHeightCacheResult::invalid_key;
    }

    const auto slot_index = find_slot(key);
    if (slot_index == slots_.size()) {
        return ChatRowHeightCacheResult::miss;
    }

    mark_recent(slot_index);
    height = slots_[slot_index].height;
    return ChatRowHeightCacheResult::hit;
}

ChatRowHeightCacheResult ChatRowHeightCache::insert(ChatRowHeightKey key, float height) noexcept {
    if (!valid_) {
        return ChatRowHeightCacheResult::invalid_workspace;
    }
    if (!valid_key(key)) {
        return ChatRowHeightCacheResult::invalid_key;
    }
    if (!std::isfinite(height) || height < 0.0F) {
        return ChatRowHeightCacheResult::invalid_height;
    }

    auto slot_index = find_slot(key);
    const auto updated = slot_index != slots_.size();
    if (updated) {
        mark_recent(slot_index);
    } else {
        const auto bucket = bucket_start(key);
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
            remove(slot_index);
            slots_[slot_index].recency = static_cast<std::uint8_t>(ways_per_bucket - 1);
        } else {
            slots_[slot_index].recency = static_cast<std::uint8_t>(active_count);
        }
    }

    auto& slot = slots_[slot_index];
    slot.key = key;
    slot.height = height;
    slot.occupied = true;
    return updated ? ChatRowHeightCacheResult::updated : ChatRowHeightCacheResult::inserted;
}

std::size_t ChatRowHeightCache::invalidate_message(std::uint64_t message_id) noexcept {
    if (!valid_ || message_id == 0) {
        return 0;
    }

    auto removed_count = std::size_t{0};
    for (std::size_t index = 0; index < slots_.size(); ++index) {
        if (slots_[index].occupied && slots_[index].key.message_id == message_id) {
            remove(index);
            ++removed_count;
        }
    }
    return removed_count;
}

bool ChatRowHeightCache::valid_key(ChatRowHeightKey key) noexcept {
    return key.message_id != 0 && std::isfinite(key.available_width) && key.available_width > 0.0F;
}

std::size_t ChatRowHeightCache::bucket_start(ChatRowHeightKey key) const noexcept {
    return hash_key(key) % bucket_count_ * ways_per_bucket;
}

std::size_t ChatRowHeightCache::find_slot(ChatRowHeightKey key) const noexcept {
    const auto bucket = bucket_start(key);
    for (std::size_t way = 0; way < ways_per_bucket; ++way) {
        const auto index = bucket + way;
        if (slots_[index].occupied && slots_[index].key.message_id == key.message_id &&
            slots_[index].key.available_width == key.available_width &&
            slots_[index].key.style_generation == key.style_generation) {
            return index;
        }
    }
    return slots_.size();
}

void ChatRowHeightCache::mark_recent(std::size_t slot_index) noexcept {
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

void ChatRowHeightCache::remove(std::size_t slot_index) noexcept {
    auto& slot = slots_[slot_index];
    const auto bucket = slot_index / ways_per_bucket * ways_per_bucket;
    const auto previous_recency = slot.recency;
    slot = {};
    for (std::size_t way = 0; way < ways_per_bucket; ++way) {
        auto& candidate = slots_[bucket + way];
        if (candidate.occupied && candidate.recency > previous_recency) {
            --candidate.recency;
        }
    }
}

}
