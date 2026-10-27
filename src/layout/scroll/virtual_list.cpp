#include "veyra/layout/scroll/virtual_list.hpp"

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

bool VirtualListIndex::capture_anchor(
    double scroll_offset,
    double viewport_extent,
    std::uint64_t scroll_generation,
    VirtualListAnchor& anchor
) const noexcept {
    if (!std::isfinite(scroll_offset) || scroll_offset < 0.0 || scroll_offset > total_extent_ ||
        !std::isfinite(viewport_extent) || viewport_extent < 0.0) {
        return false;
    }

    const auto item_index = count_ends(scroll_offset, true);
    const auto item_offset = item_index == extents_.size()
        ? 0.0
        : std::clamp(
              scroll_offset - prefix_extent(item_index),
              0.0,
              static_cast<double>(extents_[item_index])
          );
    anchor = {
        .item_index = item_index,
        .item_offset = item_offset,
        .pinned_to_end = viewport_extent >= total_extent_ - scroll_offset,
        .generation = scroll_generation,
    };
    return true;
}

bool VirtualListIndex::capture_anchor(
    std::span<const std::uint64_t> item_ids,
    double scroll_offset,
    double viewport_extent,
    std::uint64_t scroll_generation,
    VirtualListAnchor& anchor
) const noexcept {
    if (item_ids.size() != extents_.size()) {
        return false;
    }

    auto candidate = VirtualListAnchor{};
    if (!capture_anchor(scroll_offset, viewport_extent, scroll_generation, candidate)) {
        return false;
    }
    if (!candidate.pinned_to_end) {
        if (candidate.item_index >= item_ids.size() || item_ids[candidate.item_index] == 0) {
            return false;
        }
        candidate.item_id = item_ids[candidate.item_index];
    }

    anchor = candidate;
    return true;
}

bool VirtualListIndex::restore_anchor(
    const VirtualListAnchor& anchor,
    double viewport_extent,
    std::uint64_t current_generation,
    double& scroll_offset
) const noexcept {
    if (!std::isfinite(anchor.item_offset) || anchor.item_offset < 0.0 ||
        !std::isfinite(viewport_extent) || viewport_extent < 0.0) {
        return false;
    }

    if (!same_scroll_generation(current_generation, anchor.generation)) {
        return false;
    }

    if (anchor.pinned_to_end) {
        scroll_offset = std::max(0.0, total_extent_ - viewport_extent);
        return true;
    }
    if (anchor.item_index > extents_.size() ||
        (anchor.item_index == extents_.size() && anchor.item_offset != 0.0)) {
        return false;
    }

    const auto item_offset = anchor.item_index == extents_.size()
        ? 0.0
        : std::min(anchor.item_offset, static_cast<double>(extents_[anchor.item_index]));
    const auto restored_offset = prefix_extent(anchor.item_index) + item_offset;
    const auto maximum_scroll_offset = std::max(0.0, total_extent_ - viewport_extent);
    scroll_offset = std::min(restored_offset, maximum_scroll_offset);
    return true;
}

bool VirtualListIndex::restore_anchor(
    const VirtualListAnchor& anchor,
    double viewport_extent,
    std::uint64_t current_generation,
    VirtualListItemIndexLookup lookup,
    void* context,
    double& scroll_offset
) const noexcept {
    if (anchor.pinned_to_end) {
        return restore_anchor(anchor, viewport_extent, current_generation, scroll_offset);
    }
    if (anchor.item_id == 0 || lookup == nullptr) {
        return false;
    }

    auto item_index = std::size_t{0};
    if (!lookup(context, anchor.item_id, item_index)) {
        return false;
    }

    auto resolved_anchor = anchor;
    resolved_anchor.item_index = item_index;
    return restore_anchor(resolved_anchor, viewport_extent, current_generation, scroll_offset);
}

bool VirtualListIndex::restore_pinned_end(
    const VirtualListAnchor& anchor,
    double viewport_extent,
    double& scroll_offset
) const noexcept {
    if (!anchor.pinned_to_end) {
        return false;
    }

    if (!std::isfinite(viewport_extent) || viewport_extent < 0.0) {
        return false;
    }

    scroll_offset = std::max(0.0, total_extent_ - viewport_extent);
    return true;
}

bool VirtualListIndex::scroll_item_into_view(
    std::size_t item_index,
    double viewport_extent,
    double current_scroll_offset,
    double& scroll_offset
) const noexcept {
    if (item_index >= extents_.size() || !std::isfinite(viewport_extent) || viewport_extent < 0.0 ||
        !std::isfinite(current_scroll_offset) || current_scroll_offset < 0.0) {
        return false;
    }

    const auto maximum_scroll_offset = std::max(0.0, total_extent_ - viewport_extent);
    const auto current_offset = std::min(current_scroll_offset, maximum_scroll_offset);
    const auto item_start = prefix_extent(item_index);
    const auto item_extent = static_cast<double>(extents_[item_index]);
    if (item_extent == 0.0) {
        scroll_offset = current_offset;
        return true;
    }

    const auto item_end = item_start + item_extent;
    const auto visible_end = viewport_extent >= total_extent_ - current_offset
        ? total_extent_
        : current_offset + viewport_extent;
    auto next_offset = current_offset;
    if (item_extent > viewport_extent) {
        if (viewport_extent == 0.0 || item_start >= visible_end || item_end <= current_offset) {
            next_offset = item_start;
        }
    } else if (item_start < current_offset) {
        next_offset = item_start;
    } else if (item_end > visible_end) {
        next_offset = item_end - viewport_extent;
    }

    scroll_offset = std::clamp(next_offset, 0.0, maximum_scroll_offset);
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
