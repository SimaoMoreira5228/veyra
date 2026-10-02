#include "veyra/layout/virtual_list.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>

namespace veyra {

bool VirtualListIndex::reset(std::span<float> extents, std::span<double> fenwick) noexcept {
    if (extents.size() == std::numeric_limits<std::size_t>::max() ||
        fenwick.size() < extents.size() + 1) {
        return false;
    }
    for (const auto extent : extents) {
        if (!std::isfinite(extent) || extent < 0.0F) {
            return false;
        }
    }

    std::fill_n(fenwick.begin(), extents.size() + 1, 0.0);
    double total = 0.0;
    for (std::size_t index = 1; index <= extents.size(); ++index) {
        const auto extent = static_cast<double>(extents[index - 1]);
        fenwick[index] += extent;
        total += extent;

        const auto step = index & -index;
        if (step <= extents.size() - index) {
            const auto parent = index + step;
            fenwick[parent] += fenwick[index];
        }
    }

    extents_ = extents;
    fenwick_ = fenwick.first(extents.size() + 1);
    total_extent_ = total;
    return true;
}

bool VirtualListIndex::set_extent(std::size_t index, float extent, double& delta) noexcept {
    delta = 0.0;
    if (index >= extents_.size() || !std::isfinite(extent) || extent < 0.0F) {
        return false;
    }

    delta = static_cast<double>(extent) - extents_[index];
    extents_[index] = extent;
    total_extent_ += delta;

    for (auto node = index + 1; node < fenwick_.size();) {
        fenwick_[node] += delta;
        const auto step = node & -node;
        if (node > extents_.size() - step) {
            break;
        }
        node += step;
    }
    return true;
}

bool VirtualListIndex::offset_of(std::size_t item_index, double& offset) const noexcept {
    if (item_index > extents_.size()) {
        return false;
    }
    offset = prefix_extent(item_index);
    return true;
}

bool VirtualListIndex::visible_range(
    double scroll_offset,
    double viewport_extent,
    std::size_t overscan,
    VirtualListRange& range
) const noexcept {
    if (!std::isfinite(scroll_offset) || scroll_offset < 0.0 || !std::isfinite(viewport_extent) ||
        viewport_extent < 0.0) {
        return false;
    }

    const auto visible_first = count_ends(scroll_offset, true);
    if (viewport_extent == 0.0 || scroll_offset >= total_extent_) {
        const auto offset = prefix_extent(visible_first);
        range = {visible_first, visible_first, offset, total_extent_};
        return true;
    }

    const auto remaining_extent = total_extent_ - scroll_offset;
    const auto viewport_end =
        viewport_extent >= remaining_extent ? total_extent_ : scroll_offset + viewport_extent;
    const auto visible_last = count_ends(viewport_end, false);
    const auto first = visible_first > overscan ? visible_first - overscan : 0;
    const auto last = std::min(
        extents_.size(),
        visible_last + std::min(overscan, extents_.size() - visible_last)
    );
    range = {first, last, prefix_extent(first), total_extent_};
    return true;
}

std::size_t VirtualListIndex::size() const noexcept {
    return extents_.size();
}

double VirtualListIndex::total_extent() const noexcept {
    return total_extent_;
}

double VirtualListIndex::prefix_extent(std::size_t count) const noexcept {
    double result = 0.0;
    for (auto index = count; index > 0; index -= index & -index) {
        result += fenwick_[index];
    }
    return result;
}

std::size_t VirtualListIndex::count_ends(double offset, bool include_boundary) const noexcept {
    auto index = std::size_t{0};
    auto prefix = 0.0;
    for (auto step = std::bit_floor(extents_.size()); step != 0; step >>= 1) {
        const auto next = index + step;
        if (next > extents_.size()) {
            continue;
        }
        const auto candidate = prefix + fenwick_[next];
        const auto within_offset = include_boundary ? candidate <= offset : candidate < offset;
        if (within_offset) {
            index = next;
            prefix = candidate;
        }
    }
    return include_boundary ? index : std::min(extents_.size(), index + 1);
}

}
