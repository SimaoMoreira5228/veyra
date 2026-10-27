#include "veyra/text/shaping/shaped_run_cache.hpp"

#include <algorithm>
#include <cstring>
#include <limits>

namespace veyra {
namespace {

constexpr auto fnv_offset = std::uint64_t{14695981039346656037ULL};
constexpr auto fnv_prime = std::uint64_t{1099511628211ULL};

std::uint64_t hash_run(ShapedRunCacheKey key, std::string_view text) noexcept {
    auto hash = fnv_offset;
    const auto add_value = [&hash](std::uint64_t value) {
        for (auto shift = 0U; shift < 64U; shift += 8U) {
            hash = (hash ^ static_cast<std::uint8_t>(value >> shift)) * fnv_prime;
        }
    };
    add_value(key.font_id);
    add_value(key.style_id);
    add_value(static_cast<std::uint64_t>(key.font_scale_x));
    add_value(static_cast<std::uint64_t>(key.font_scale_y));
    add_value(static_cast<std::uint64_t>(key.direction));
    add_value(static_cast<std::uint64_t>(key.script_tag));
    add_value(static_cast<std::uint64_t>(key.language_id));
    add_value(key.fallback_generation);
    add_value(static_cast<std::uint64_t>(key.features_id));
    add_value(static_cast<std::uint64_t>(text.size()));
    for (const auto byte : text) {
        hash = (hash ^ static_cast<std::uint8_t>(byte)) * fnv_prime;
    }
    return hash;
}

bool has_capacity(std::size_t slots, std::size_t per_slot, std::size_t storage) noexcept {
    return per_slot == 0 ||
        (slots <= std::numeric_limits<std::size_t>::max() / per_slot &&
         storage >= slots * per_slot);
}

}

ShapedRunCache::ShapedRunCache(
    std::span<ShapedRunCacheSlot> slots,
    std::span<char> text_storage,
    std::span<ShapedGlyph> glyph_storage,
    std::size_t text_bytes_per_slot,
    std::size_t glyphs_per_slot
) noexcept :
    slots_(slots), text_storage_(text_storage), glyph_storage_(glyph_storage),
    text_bytes_per_slot_(text_bytes_per_slot), glyphs_per_slot_(glyphs_per_slot) {
    if (slots.empty() || !has_capacity(slots.size(), text_bytes_per_slot, text_storage.size()) ||
        !has_capacity(slots.size(), glyphs_per_slot, glyph_storage.size())) {
        return;
    }
    valid_ = true;
    clear();
}

bool ShapedRunCache::valid() const noexcept {
    return valid_;
}

void ShapedRunCache::clear() noexcept {
    if (!valid_) {
        return;
    }
    std::fill(slots_.begin(), slots_.end(), ShapedRunCacheSlot{});
}

ShapedRunCacheResult ShapedRunCache::find(
    ShapedRunCacheKey key,
    std::string_view text,
    std::span<ShapedGlyph> output,
    std::size_t& glyph_count
) noexcept {
    glyph_count = 0;
    if (!valid_) {
        return ShapedRunCacheResult::invalid_workspace;
    }
    if (text.size() > text_bytes_per_slot_) {
        return ShapedRunCacheResult::miss;
    }

    const auto hash = hash_run(key, text);
    const auto index = static_cast<std::size_t>(hash % slots_.size());
    const auto& slot = slots_[index];
    if (!slot.occupied || slot.hash != hash || slot.key.font_id != key.font_id ||
        slot.key.style_id != key.style_id || slot.key.font_scale_x != key.font_scale_x ||
        slot.key.font_scale_y != key.font_scale_y || slot.key.direction != key.direction ||
        slot.key.script_tag != key.script_tag || slot.key.language_id != key.language_id ||
        slot.key.fallback_generation != key.fallback_generation ||
        slot.key.features_id != key.features_id || slot.text_length != text.size() ||
        (!text.empty() &&
         std::memcmp(text_for_slot(index).data(), text.data(), text.size()) != 0)) {
        return ShapedRunCacheResult::miss;
    }

    glyph_count = slot.glyph_count;
    if (output.size() < glyph_count) {
        return ShapedRunCacheResult::insufficient_output;
    }
    if (glyph_count != 0) {
        std::memmove(
            output.data(),
            glyphs_for_slot(index).data(),
            glyph_count * sizeof(ShapedGlyph)
        );
    }
    return ShapedRunCacheResult::hit;
}

ShapedRunCacheResult ShapedRunCache::insert(
    ShapedRunCacheKey key,
    std::string_view text,
    std::span<const ShapedGlyph> glyphs
) noexcept {
    if (!valid_) {
        return ShapedRunCacheResult::invalid_workspace;
    }
    if (text.size() > text_bytes_per_slot_ || glyphs.size() > glyphs_per_slot_) {
        return ShapedRunCacheResult::entry_too_large;
    }

    const auto hash = hash_run(key, text);
    const auto index = static_cast<std::size_t>(hash % slots_.size());
    if (!text.empty()) {
        std::memmove(text_for_slot(index).data(), text.data(), text.size());
    }
    if (!glyphs.empty()) {
        std::memmove(glyphs_for_slot(index).data(), glyphs.data(), glyphs.size_bytes());
    }
    auto& slot = slots_[index];
    slot.key = key;
    slot.hash = hash;
    slot.text_length = text.size();
    slot.glyph_count = glyphs.size();
    slot.occupied = true;
    return ShapedRunCacheResult::stored;
}

std::uint32_t shaped_run_script_tag(std::string_view script) noexcept {
    if (script.empty()) {
        return 0;
    }
    if (script.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        return 0;
    }

    return static_cast<std::uint32_t>(
        hb_script_from_string(script.data(), static_cast<int>(script.size()))
    );
}

std::uint32_t shaped_run_language_id(std::string_view language) noexcept {
    if (language.empty()) {
        return 0;
    }

    auto hash = std::uint32_t{2166136261U};
    for (const auto byte : language) {
        hash = (hash ^ static_cast<std::uint8_t>(byte)) * 16777619U;
    }
    return hash;
}

std::span<char> ShapedRunCache::text_for_slot(std::size_t index) const noexcept {
    return text_storage_.subspan(index * text_bytes_per_slot_, text_bytes_per_slot_);
}

std::span<ShapedGlyph> ShapedRunCache::glyphs_for_slot(std::size_t index) const noexcept {
    return glyph_storage_.subspan(index * glyphs_per_slot_, glyphs_per_slot_);
}

}
