#ifndef VEYRA_LAYOUT_VIRTUAL_LIST_HPP
#define VEYRA_LAYOUT_VIRTUAL_LIST_HPP

#include "veyra/layout/scroll/scroll_axis.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

namespace veyra {

struct VirtualListRange {
    std::size_t first_item{};
    std::size_t last_item{};
    double first_offset{};
    double total_extent{};
};

struct VirtualListAnchor {
    std::size_t item_index{};
    double item_offset{};
    bool pinned_to_end{};
    std::uint64_t item_id{};
    std::uint64_t generation{};
};

using VirtualListItemIndexLookup =
    bool (*)(void* context, std::uint64_t item_id, std::size_t& item_index) noexcept;

class VirtualListIndex {
    std::span<float> extents_;
    std::span<double> fenwick_;
    double total_extent_{};

    double prefix_extent(std::size_t count) const noexcept;
    std::size_t count_ends(double offset, bool include_boundary) const noexcept;

  public:
    bool reset(std::span<float> extents, std::span<double> fenwick) noexcept;
    bool set_extent(std::size_t index, float extent, double& delta) noexcept;
    bool offset_of(std::size_t item_index, double& offset) const noexcept;
    bool capture_anchor(
        double scroll_offset,
        double viewport_extent,
        std::uint64_t scroll_generation,
        VirtualListAnchor& anchor
    ) const noexcept;
    bool capture_anchor(
        std::span<const std::uint64_t> item_ids,
        double scroll_offset,
        double viewport_extent,
        std::uint64_t scroll_generation,
        VirtualListAnchor& anchor
    ) const noexcept;
    bool restore_anchor(
        const VirtualListAnchor& anchor,
        double viewport_extent,
        std::uint64_t current_generation,
        double& scroll_offset
    ) const noexcept;
    bool restore_anchor(
        const VirtualListAnchor& anchor,
        double viewport_extent,
        std::uint64_t current_generation,
        VirtualListItemIndexLookup lookup,
        void* context,
        double& scroll_offset
    ) const noexcept;
    bool restore_pinned_end(
        const VirtualListAnchor& anchor,
        double viewport_extent,
        double& scroll_offset
    ) const noexcept;
    bool scroll_item_into_view(
        std::size_t item_index,
        double viewport_extent,
        double current_scroll_offset,
        double& scroll_offset
    ) const noexcept;
    bool visible_range(
        double scroll_offset,
        double viewport_extent,
        std::size_t overscan,
        VirtualListRange& range
    ) const noexcept;

    std::size_t size() const noexcept;
    double total_extent() const noexcept;
};

}

#endif
