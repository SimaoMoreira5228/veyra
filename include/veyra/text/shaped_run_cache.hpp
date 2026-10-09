#ifndef VEYRA_TEXT_SHAPED_RUN_CACHE_HPP
#define VEYRA_TEXT_SHAPED_RUN_CACHE_HPP

#include "veyra/text/shaper.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace veyra {

struct ShapedRunCacheKey {
    std::uint64_t font_id{};
    std::uint64_t style_id{};
    std::int32_t font_scale_x{};
    std::int32_t font_scale_y{};
};

struct ShapedRunCacheSlot {
  private:
    friend class ShapedRunCache;

    ShapedRunCacheKey key;
    std::uint64_t hash{};
    std::size_t text_length{};
    std::size_t glyph_count{};
    bool occupied{};
};

enum class ShapedRunCacheResult : std::uint8_t {
    hit,
    miss,
    insufficient_output,
    invalid_workspace,
    entry_too_large,
    stored,
};

class ShapedRunCache {
  public:
    ShapedRunCache(
        std::span<ShapedRunCacheSlot> slots,
        std::span<char> text_storage,
        std::span<ShapedGlyph> glyph_storage,
        std::size_t text_bytes_per_slot,
        std::size_t glyphs_per_slot
    ) noexcept;

    bool valid() const noexcept;
    void clear() noexcept;

    ShapedRunCacheResult find(
        ShapedRunCacheKey key,
        std::string_view text,
        std::span<ShapedGlyph> output,
        std::size_t& glyph_count
    ) noexcept;

    ShapedRunCacheResult insert(
        ShapedRunCacheKey key,
        std::string_view text,
        std::span<const ShapedGlyph> glyphs
    ) noexcept;

  private:
    std::span<ShapedRunCacheSlot> slots_;
    std::span<char> text_storage_;
    std::span<ShapedGlyph> glyph_storage_;
    std::size_t text_bytes_per_slot_{};
    std::size_t glyphs_per_slot_{};
    bool valid_{};

    std::span<char> text_for_slot(std::size_t index) const noexcept;
    std::span<ShapedGlyph> glyphs_for_slot(std::size_t index) const noexcept;
};

}

#endif
