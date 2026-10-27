#include "veyra/render/common/glyph_atlas.hpp"

#include <algorithm>
#include <cassert>
#include <cstring>
#include <limits>

#define STB_RECT_PACK_IMPLEMENTATION
#include "stb_rect_pack.h"

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

GlyphAtlas::GlyphAtlas(
    std::span<GlyphAtlasSlot> slots,
    std::span<std::uint8_t> coverage,
    std::span<stbrp_node> nodes,
    std::size_t width,
    std::size_t height
) noexcept :
    slots_(slots), coverage_(coverage), nodes_(nodes), width_(width), height_(height),
    bucket_count_(slots.size() / ways_per_bucket),
    valid_(
        !slots.empty() && slots.size() % ways_per_bucket == 0 && width > 0 && height > 0 &&
        width <= static_cast<std::size_t>(std::numeric_limits<int>::max()) &&
        height <= static_cast<std::size_t>(std::numeric_limits<int>::max()) &&
        coverage.size() >= width * height && nodes.size() >= width
    ) {
    if (!valid_) {
        return;
    }

    stbrp_init_target(
        &packer_,
        static_cast<int>(width),
        static_cast<int>(height),
        nodes.data(),
        static_cast<int>(nodes.size())
    );

    view_ = SoftwareMask{
        .width = width,
        .height = height,
        .stride = width,
        .coverage = coverage.first(width * height),
        .generation = 0,
    };
}

bool GlyphAtlas::valid() const noexcept {
    return valid_;
}

void GlyphAtlas::clear() noexcept {
    if (!valid_) {
        return;
    }

    std::fill(slots_.begin(), slots_.end(), GlyphAtlasSlot{});
    stbrp_init_target(
        &packer_,
        static_cast<int>(width_),
        static_cast<int>(height_),
        nodes_.data(),
        static_cast<int>(nodes_.size())
    );
    ++generation_;
    view_.generation = generation_;
}

std::uint64_t GlyphAtlas::generation() const noexcept {
    return generation_;
}

std::size_t GlyphAtlas::width() const noexcept {
    return width_;
}

std::size_t GlyphAtlas::height() const noexcept {
    return height_;
}

const SoftwareMask& GlyphAtlas::view() const noexcept {
    return view_;
}

std::size_t GlyphAtlas::bucket_start(const GlyphMaskKey& key) const noexcept {
    return static_cast<std::size_t>(key_hash(key) % bucket_count_) * ways_per_bucket;
}

std::size_t GlyphAtlas::find_slot(const GlyphMaskKey& key) const noexcept {
    const auto bucket = bucket_start(key);

    for (auto way = std::size_t{}; way < ways_per_bucket; ++way) {
        const auto candidate = bucket + way;

        if (slots_[candidate].occupied && slots_[candidate].key == key) {
            return candidate;
        }
    }

    return slots_.size();
}

void GlyphAtlas::mark_recent(std::size_t slot_index) noexcept {
    auto& slot = slots_[slot_index];
    slot.recency = 0;

    const auto bucket = slot_index - slot_index % ways_per_bucket;

    for (auto way = std::size_t{}; way < ways_per_bucket; ++way) {
        auto& other = slots_[bucket + way];

        if (&other != &slot && other.occupied && other.recency < 255) {
            ++other.recency;
        }
    }
}

bool GlyphAtlas::pack(
    std::uint32_t width,
    std::uint32_t height,
    std::uint32_t& u,
    std::uint32_t& v
) {
    auto rect = stbrp_rect{
        .id = 0,
        .w = static_cast<stbrp_coord>(width),
        .h = static_cast<stbrp_coord>(height),
    };

    stbrp_pack_rects(&packer_, &rect, 1);

    if (rect.was_packed == 0) {
        return false;
    }

    u = static_cast<std::uint32_t>(rect.x);
    v = static_cast<std::uint32_t>(rect.y);

    return true;
}

std::optional<GlyphAtlasPlacement> GlyphAtlas::find(const GlyphMaskKey& key) noexcept {
    if (!valid_ || !valid_key(key)) {
        return std::nullopt;
    }

    const auto slot_index = find_slot(key);

    if (slot_index == slots_.size()) {
        return std::nullopt;
    }

    mark_recent(slot_index);

    const auto& slot = slots_[slot_index];

    return GlyphAtlasPlacement{
        .u = slot.u,
        .v = slot.v,
        .width = slot.width,
        .height = slot.height,
        .bearing_x = slot.bearing_x,
        .bearing_y = slot.bearing_y,
        .generation = slot.generation,
    };
}

GlyphAtlasResult GlyphAtlas::insert(const GlyphMaskKey& key, const GlyphMask& mask) noexcept {
    if (!valid_) {
        return GlyphAtlasResult::invalid_workspace;
    }
    if (!valid_key(key)) {
        return GlyphAtlasResult::invalid_key;
    }
    if (!valid_mask(mask.mask)) {
        return GlyphAtlasResult::invalid_mask;
    }
    if (mask.mask.width > width_ || mask.mask.height > height_) {
        return GlyphAtlasResult::glyph_too_large;
    }

    const auto bucket = bucket_start(key);
    auto slot_index = find_slot(key);
    auto updated = slot_index != slots_.size();

    if (updated &&
        (slots_[slot_index].width != mask.mask.width ||
         slots_[slot_index].height != mask.mask.height)) {
        slots_[slot_index].occupied = false;
        slot_index = slots_.size();
        updated = false;
    }

    if (updated) {
        mark_recent(slot_index);
    } else {
        auto oldest = bucket;
        slot_index = slots_.size();
        for (auto way = std::size_t{}; way < ways_per_bucket; ++way) {
            const auto candidate = bucket + way;
            if (!slots_[candidate].occupied) {
                slot_index = candidate;
                break;
            }
            if (slots_[candidate].recency > slots_[oldest].recency) {
                oldest = candidate;
            }
        }
        if (slot_index == slots_.size()) {
            slot_index = oldest;
        }
        mark_recent(slot_index);
    }

    auto u = std::uint32_t{};
    auto v = std::uint32_t{};
    auto evicted = false;

    if (!updated) {
        const auto width = static_cast<std::uint32_t>(mask.mask.width);
        const auto height = static_cast<std::uint32_t>(mask.mask.height);

        if (!pack(width, height, u, v)) {
            clear();

            if (!pack(width, height, u, v)) {
                return GlyphAtlasResult::glyph_too_large;
            }

            slot_index = bucket_start(key);
            evicted = true;
        }
    } else {
        u = slots_[slot_index].u;
        v = slots_[slot_index].v;
    }

    auto& slot = slots_[slot_index];
    slot.key = key;
    slot.u = u;
    slot.v = v;
    slot.width = static_cast<std::uint32_t>(mask.mask.width);
    slot.height = static_cast<std::uint32_t>(mask.mask.height);
    slot.bearing_x = mask.bearing_x;
    slot.bearing_y = mask.bearing_y;
    slot.generation = generation_;
    slot.occupied = true;

    for (auto row = std::size_t{}; row < mask.mask.height; ++row) {
        const auto source = mask.mask.coverage.data() + row * mask.mask.stride;
        auto destination = coverage_.data() + (static_cast<std::size_t>(v) + row) * width_ + u;

        std::memcpy(destination, source, mask.mask.width);
    }

    ++generation_;
    view_.generation = generation_;

    if (updated) {
        return GlyphAtlasResult::updated;
    }

    return evicted ? GlyphAtlasResult::inserted_after_eviction : GlyphAtlasResult::inserted;
}

}
