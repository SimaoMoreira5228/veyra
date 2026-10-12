#ifndef VEYRA_TEXT_GLYPH_BOUNDS_CACHE_HPP
#define VEYRA_TEXT_GLYPH_BOUNDS_CACHE_HPP

#include "veyra/text/glyph_mask_cache.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace veyra {

struct GlyphBoundsKey {
    std::uint64_t face_id{};
    std::uint64_t variation_id{};
    std::uint32_t glyph_id{};
    std::uint32_t pixel_size{};
    std::uint32_t scale_x_16_16{65'536};
    std::uint32_t scale_y_16_16{65'536};
    GlyphRasterMode mode{GlyphRasterMode::grayscale};

    friend constexpr bool operator==(const GlyphBoundsKey&, const GlyphBoundsKey&) = default;
};

struct GlyphBounds {
    std::int32_t width{};
    std::int32_t height{};
    std::int32_t bearing_x{};
    std::int32_t bearing_y{};
};

struct GlyphBoundsCacheSlot {
  private:
    friend class GlyphBoundsCache;

    GlyphBoundsKey key;
    std::uint64_t hash{};
    GlyphBounds bounds;
    bool occupied{};
};

enum class GlyphBoundsCacheResult : std::uint8_t {
    hit,
    miss,
    invalid_workspace,
    invalid_key,
    invalid_bounds,
    stored,
};

class GlyphBoundsCache {
  public:
    GlyphBoundsCache(std::span<GlyphBoundsCacheSlot> slots) noexcept : slots_(slots) {
        if (slots.empty()) {
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
        std::fill(slots_.begin(), slots_.end(), GlyphBoundsCacheSlot{});
    }

    GlyphBoundsCacheResult find(const GlyphBoundsKey& key, GlyphBounds& bounds) noexcept {
        if (!valid_) {
            return GlyphBoundsCacheResult::invalid_workspace;
        }
        if (!valid_key(key)) {
            return GlyphBoundsCacheResult::invalid_key;
        }

        const auto hash = hash_key(key);
        const auto index = static_cast<std::size_t>(hash % slots_.size());
        const auto& slot = slots_[index];
        if (!slot.occupied) {
            return GlyphBoundsCacheResult::miss;
        }
        if (slot.hash != hash) {
            return GlyphBoundsCacheResult::miss;
        }
        if (!(slot.key == key)) {
            return GlyphBoundsCacheResult::miss;
        }

        bounds = slot.bounds;
        return GlyphBoundsCacheResult::hit;
    }

    std::optional<GlyphBounds> find(const GlyphBoundsKey& key) noexcept {
        auto bounds = GlyphBounds{};
        if (find(key, bounds) != GlyphBoundsCacheResult::hit) {
            return std::nullopt;
        }
        return bounds;
    }

    GlyphBoundsCacheResult insert(const GlyphBoundsKey& key, GlyphBounds bounds) noexcept {
        if (!valid_) {
            return GlyphBoundsCacheResult::invalid_workspace;
        }
        if (!valid_key(key)) {
            return GlyphBoundsCacheResult::invalid_key;
        }
        if (!valid_bounds(bounds)) {
            return GlyphBoundsCacheResult::invalid_bounds;
        }

        const auto hash = hash_key(key);
        const auto index = static_cast<std::size_t>(hash % slots_.size());
        auto& slot = slots_[index];
        slot.key = key;
        slot.hash = hash;
        slot.bounds = bounds;
        slot.occupied = true;
        return GlyphBoundsCacheResult::stored;
    }

  private:
    static constexpr auto fnv_offset = std::uint64_t{14695981039346656037ULL};
    static constexpr auto fnv_prime = std::uint64_t{1099511628211ULL};

    std::span<GlyphBoundsCacheSlot> slots_;
    bool valid_{};

    static bool valid_key(const GlyphBoundsKey& key) noexcept {
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

    static bool valid_bounds(GlyphBounds bounds) noexcept {
        return bounds.width >= 0 && bounds.height >= 0;
    }

    static std::uint64_t hash_key(const GlyphBoundsKey& key) noexcept {
        auto hash = fnv_offset;
        const auto add_value = [&hash](std::uint64_t value) noexcept {
            for (auto shift = 0U; shift < 64U; shift += 8U) {
                hash = (hash ^ static_cast<std::uint8_t>(value >> shift)) * fnv_prime;
            }
        };
        add_value(key.face_id);
        add_value(key.variation_id);
        add_value((static_cast<std::uint64_t>(key.glyph_id) << 32U) | key.pixel_size);
        add_value((static_cast<std::uint64_t>(key.scale_x_16_16) << 32U) | key.scale_y_16_16);
        add_value(static_cast<std::uint64_t>(key.mode));
        return hash;
    }
};

}

#endif
