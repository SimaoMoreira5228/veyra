#ifndef VEYRA_TEXT_SHAPED_RUN_CACHE_HPP
#define VEYRA_TEXT_SHAPED_RUN_CACHE_HPP

#include "veyra/text/direction.hpp"
#include "veyra/text/shaping/shaper.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <type_traits>

namespace veyra {

struct ShapedRunCacheKey {
    std::uint64_t font_id{};
    std::uint64_t style_id{};
    std::int32_t font_scale_x{};
    std::int32_t font_scale_y{};
    TextDirection direction{TextDirection::left_to_right};
    std::uint32_t script_tag{};
    std::uint32_t language_id{};
    // NOTE: revision of the fallback font list that produced font_id; a remapped list must miss.
    std::uint64_t fallback_generation{};
    // NOTE: reserved while the shaper passes no features; zero today, compared so future
    // features can never collide with unfeatured runs.
    std::uint32_t features_id{};
};

static_assert(std::is_trivially_copyable_v<ShapedRunCacheKey>);

// NOTE: empty script maps to zero and leaves HarfBuzz guessing; explicit tags come from
// hb_script_from_string, so equal strings always build equal tags.
std::uint32_t shaped_run_script_tag(std::string_view script) noexcept;

// NOTE: raw bytes are hashed, so case variants miss instead of colliding; empty maps to zero.
std::uint32_t shaped_run_language_id(std::string_view language) noexcept;

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
