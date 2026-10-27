#ifndef VEYRA_RENDER_DAMAGE_TRACKER_HPP
#define VEYRA_RENDER_DAMAGE_TRACKER_HPP

#include "veyra/layout/core/geometry.hpp"

#include <cstddef>
#include <span>

namespace veyra {

class DamageTracker {
  public:
    DamageTracker(
        std::size_t surface_width,
        std::size_t surface_height,
        std::span<LayoutRect> region_storage
    ) noexcept;

    bool valid() const noexcept;
    void clear() noexcept;
    bool mark_full() noexcept;
    bool add(LayoutRect region) noexcept;
    std::span<const LayoutRect> regions() const noexcept;
    bool full_surface() const noexcept;

  private:
    struct PixelBounds {
        double left{};
        double top{};
        double right{};
        double bottom{};
    };

    std::size_t surface_width_{};
    std::size_t surface_height_{};
    std::span<LayoutRect> region_storage_;
    std::size_t region_count_{};
    bool valid_{};
    bool full_surface_{};

    static bool valid_region(const LayoutRect& region) noexcept;
    PixelBounds pixel_bounds(const LayoutRect& region) const noexcept;
    LayoutRect clip_to_surface(const LayoutRect& region) const noexcept;
    static LayoutRect unite(const LayoutRect& left, const LayoutRect& right) noexcept;
    static bool overlaps(PixelBounds left, PixelBounds right) noexcept;
};

}

#endif
