#ifndef VEYRA_TEXT_GLYPH_MASK_CACHE_HPP
#define VEYRA_TEXT_GLYPH_MASK_CACHE_HPP

#include "veyra/foundation/glyph.hpp"
#include "veyra/foundation/image.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace veyra {

struct GlyphMaskCacheSlot {
    GlyphMaskKey key;
    std::size_t width{};
    std::size_t height{};
    std::int32_t bearing_x{};
    std::int32_t bearing_y{};
    std::uint64_t generation{};
    std::uint8_t recency{};
    bool occupied{};
};

enum class GlyphMaskCacheResult : std::uint8_t {
    inserted,
    updated,
    invalid_workspace,
    invalid_key,
    invalid_mask,
    glyph_too_large,
};

class GlyphMaskCache {
  public:
    GlyphMaskCache(
        std::span<GlyphMaskCacheSlot> slots,
        std::span<std::uint8_t> coverage_storage,
        std::size_t maximum_width,
        std::size_t maximum_height
    ) noexcept;

    GlyphMaskCache(const GlyphMaskCache&) = delete;
    GlyphMaskCache& operator=(const GlyphMaskCache&) = delete;
    GlyphMaskCache(GlyphMaskCache&&) = delete;
    GlyphMaskCache& operator=(GlyphMaskCache&&) = delete;

    bool valid() const noexcept;
    void clear() noexcept;
    std::optional<GlyphMask> find(const GlyphMaskKey& key) noexcept;
    std::optional<GlyphMask>
    find(const GlyphMaskKey& key, std::uint64_t expected_generation) noexcept;
    GlyphMaskCacheResult insert(const GlyphMaskKey& key, const GlyphMask& mask) noexcept;

  private:
    std::span<GlyphMaskCacheSlot> slots_;
    std::span<std::uint8_t> coverage_storage_;
    std::size_t maximum_width_{};
    std::size_t maximum_height_{};
    std::size_t slot_capacity_{};
    std::size_t bucket_count_{};
    bool valid_{};

    std::size_t bucket_start(const GlyphMaskKey& key) const noexcept;
    std::size_t find_slot(const GlyphMaskKey& key) const noexcept;
    void mark_recent(std::size_t slot_index) noexcept;
};

}

namespace veyra::text {
using ::veyra::GlyphMaskCache;
using ::veyra::GlyphMaskCacheResult;
using ::veyra::GlyphMaskCacheSlot;
}

namespace veyra::text::rendering {
using ::veyra::GlyphMaskCache;
using ::veyra::GlyphMaskCacheResult;
using ::veyra::GlyphMaskCacheSlot;
}

#endif
