#ifndef VEYRA_TEXT_LINE_MEASURE_CACHE_HPP
#define VEYRA_TEXT_LINE_MEASURE_CACHE_HPP

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <span>
#include <string_view>

namespace veyra {

struct LineMeasureKey {
    std::uint64_t font_id{};
    std::uint64_t style_id{};
    std::int32_t font_scale_x{};
    std::int32_t font_scale_y{};
    float wrap_width{};
    std::uint64_t style_generation{};
};

struct LineMeasure {
    float width{};
    float height{};
    std::uint32_t line_count{};
};

struct LineMeasureCacheSlot {
  private:
    friend class LineMeasureCache;

    LineMeasureKey key;
    std::uint64_t hash{};
    std::size_t text_length{};
    LineMeasure measure{};
    bool occupied{};
};

enum class LineMeasureCacheResult : std::uint8_t {
    hit,
    miss,
    invalid_workspace,
    invalid_key,
    invalid_measure,
    entry_too_large,
    stored,
};

class LineMeasureCache {
  public:
    LineMeasureCache(
        std::span<LineMeasureCacheSlot> slots,
        std::span<char> text_storage,
        std::size_t text_bytes_per_slot
    ) noexcept :
        slots_(slots), text_storage_(text_storage), text_bytes_per_slot_(text_bytes_per_slot) {
        if (slots.empty()) {
            return;
        }
        if (!has_capacity(slots.size(), text_bytes_per_slot, text_storage.size())) {
            return;
        }
        valid_ = true;
        clear();
    }

    bool valid() const noexcept {
        return valid_;
    }

    void clear() noexcept {
        if (!valid_) {
            return;
        }
        std::fill(slots_.begin(), slots_.end(), LineMeasureCacheSlot{});
    }

    LineMeasureCacheResult
    find(LineMeasureKey key, std::string_view text, LineMeasure& measure) noexcept {
        if (!valid_) {
            return LineMeasureCacheResult::invalid_workspace;
        }
        if (!valid_key(key)) {
            return LineMeasureCacheResult::invalid_key;
        }
        if (text.size() > text_bytes_per_slot_) {
            return LineMeasureCacheResult::miss;
        }

        const auto hash = hash_key(key, text);
        const auto index = static_cast<std::size_t>(hash % slots_.size());
        const auto& slot = slots_[index];
        if (!slot.occupied) {
            return LineMeasureCacheResult::miss;
        }
        if (slot.hash != hash) {
            return LineMeasureCacheResult::miss;
        }
        if (slot.key.font_id != key.font_id) {
            return LineMeasureCacheResult::miss;
        }
        if (slot.key.style_id != key.style_id) {
            return LineMeasureCacheResult::miss;
        }
        if (slot.key.font_scale_x != key.font_scale_x) {
            return LineMeasureCacheResult::miss;
        }
        if (slot.key.font_scale_y != key.font_scale_y) {
            return LineMeasureCacheResult::miss;
        }
        if (std::bit_cast<std::uint32_t>(slot.key.wrap_width) !=
            std::bit_cast<std::uint32_t>(key.wrap_width)) {
            return LineMeasureCacheResult::miss;
        }
        if (slot.key.style_generation != key.style_generation) {
            return LineMeasureCacheResult::miss;
        }
        if (slot.text_length != text.size()) {
            return LineMeasureCacheResult::miss;
        }
        if (!text.empty() &&
            std::memcmp(text_for_slot(index).data(), text.data(), text.size()) != 0) {
            return LineMeasureCacheResult::miss;
        }

        measure = slot.measure;
        return LineMeasureCacheResult::hit;
    }

    LineMeasureCacheResult
    insert(LineMeasureKey key, std::string_view text, LineMeasure measure) noexcept {
        if (!valid_) {
            return LineMeasureCacheResult::invalid_workspace;
        }
        if (!valid_key(key)) {
            return LineMeasureCacheResult::invalid_key;
        }
        if (!valid_measure(measure)) {
            return LineMeasureCacheResult::invalid_measure;
        }
        if (text.size() > text_bytes_per_slot_) {
            return LineMeasureCacheResult::entry_too_large;
        }

        const auto hash = hash_key(key, text);
        const auto index = static_cast<std::size_t>(hash % slots_.size());
        if (!text.empty()) {
            std::memmove(text_for_slot(index).data(), text.data(), text.size());
        }
        auto& slot = slots_[index];
        slot.key = key;
        slot.hash = hash;
        slot.text_length = text.size();
        slot.measure = measure;
        slot.occupied = true;
        return LineMeasureCacheResult::stored;
    }

  private:
    static constexpr auto fnv_offset = std::uint64_t{14695981039346656037ULL};
    static constexpr auto fnv_prime = std::uint64_t{1099511628211ULL};

    std::span<LineMeasureCacheSlot> slots_;
    std::span<char> text_storage_;
    std::size_t text_bytes_per_slot_{};
    bool valid_{};

    static bool
    has_capacity(std::size_t slots, std::size_t per_slot, std::size_t storage) noexcept {
        if (per_slot == 0) {
            return true;
        }
        if (slots > std::numeric_limits<std::size_t>::max() / per_slot) {
            return false;
        }
        return storage >= slots * per_slot;
    }

    static bool valid_key(LineMeasureKey key) noexcept {
        return std::isfinite(key.wrap_width) && key.wrap_width > 0.0F;
    }

    static bool valid_measure(LineMeasure measure) noexcept {
        return std::isfinite(measure.width) && measure.width >= 0.0F &&
            std::isfinite(measure.height) && measure.height >= 0.0F;
    }

    static std::uint64_t hash_key(LineMeasureKey key, std::string_view text) noexcept {
        auto hash = fnv_offset;
        const auto add_value = [&hash](std::uint64_t value) noexcept {
            for (auto shift = 0U; shift < 64U; shift += 8U) {
                hash = (hash ^ static_cast<std::uint8_t>(value >> shift)) * fnv_prime;
            }
        };
        add_value(key.font_id);
        add_value(key.style_id);
        add_value(static_cast<std::uint64_t>(key.font_scale_x));
        add_value(static_cast<std::uint64_t>(key.font_scale_y));
        add_value(static_cast<std::uint64_t>(std::bit_cast<std::uint32_t>(key.wrap_width)));
        add_value(key.style_generation);
        add_value(static_cast<std::uint64_t>(text.size()));
        for (const auto byte : text) {
            hash = (hash ^ static_cast<std::uint8_t>(byte)) * fnv_prime;
        }
        return hash;
    }

    std::span<char> text_for_slot(std::size_t index) const noexcept {
        return text_storage_.subspan(index * text_bytes_per_slot_, text_bytes_per_slot_);
    }
};

}

#endif
