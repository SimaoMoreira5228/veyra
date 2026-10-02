#ifndef VEYRA_LAYOUT_VIRTUAL_LIST_HPP
#define VEYRA_LAYOUT_VIRTUAL_LIST_HPP

#include <cstddef>
#include <span>

namespace veyra {

struct VirtualListRange {
    std::size_t first_item{};
    std::size_t last_item{};
    double first_offset{};
    double total_extent{};
};

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
