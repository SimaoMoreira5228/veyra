#ifndef VEYRA_RENDER_GLYPH_ATLAS_HPP
#define VEYRA_RENDER_GLYPH_ATLAS_HPP

#include "veyra/render/software_renderer.hpp"
#include "veyra/text/glyph_mask_cache.hpp"

#include "stb_rect_pack.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace veyra {

struct GlyphAtlasSlot {
    GlyphMaskKey key{};
    std::uint32_t u{};
    std::uint32_t v{};
    std::uint32_t width{};
    std::uint32_t height{};
    std::int32_t bearing_x{};
    std::int32_t bearing_y{};
    std::uint64_t generation{};
    std::uint8_t recency{};
    bool occupied{};
};

struct GlyphAtlasPlacement {
    std::uint32_t u{};
    std::uint32_t v{};
    std::uint32_t width{};
    std::uint32_t height{};
    std::int32_t bearing_x{};
    std::int32_t bearing_y{};
    std::uint64_t generation{};
};

enum class GlyphAtlasResult : std::uint8_t {
    inserted,
    inserted_after_eviction,
    updated,
    invalid_workspace,
    invalid_key,
    invalid_mask,
    glyph_too_large,
};

class GlyphAtlas {
  public:
    GlyphAtlas(
        std::span<GlyphAtlasSlot> slots,
        std::span<std::uint8_t> coverage,
        std::span<stbrp_node> nodes,
        std::size_t width,
        std::size_t height
    ) noexcept;

    GlyphAtlas(const GlyphAtlas&) = delete;
    GlyphAtlas& operator=(const GlyphAtlas&) = delete;
    GlyphAtlas(GlyphAtlas&&) = delete;
    GlyphAtlas& operator=(GlyphAtlas&&) = delete;

    bool valid() const noexcept;
    void clear() noexcept;
    std::uint64_t generation() const noexcept;
    std::size_t width() const noexcept;
    std::size_t height() const noexcept;
    const SoftwareMask& view() const noexcept;
    std::optional<GlyphAtlasPlacement> find(const GlyphMaskKey& key) noexcept;
    GlyphAtlasResult insert(const GlyphMaskKey& key, const GlyphMask& mask) noexcept;

  private:
    std::size_t bucket_start(const GlyphMaskKey& key) const noexcept;
    std::size_t find_slot(const GlyphMaskKey& key) const noexcept;
    void mark_recent(std::size_t slot_index) noexcept;
    bool pack(std::uint32_t width, std::uint32_t height, std::uint32_t& u, std::uint32_t& v);

    std::span<GlyphAtlasSlot> slots_;
    std::span<std::uint8_t> coverage_;
    std::span<stbrp_node> nodes_;
    std::size_t width_{};
    std::size_t height_{};
    SoftwareMask view_{};
    std::size_t bucket_count_{};
    std::uint64_t generation_{};
    stbrp_context packer_{};
    bool valid_{};
};

}

#endif
