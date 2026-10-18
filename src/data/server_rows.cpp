#include "veyra/data/server_rows.hpp"

#include <limits>

namespace veyra {

namespace {

constexpr std::uint64_t kFnvOffsetBasis = 14695981039346656037ULL;
constexpr std::uint64_t kFnvPrime = 1099511628211ULL;

void mix_value(std::uint64_t& hash, std::uint64_t value) noexcept {
    for (std::size_t shift = 0; shift < 64; shift += 8) {
        const auto byte = static_cast<std::uint64_t>(static_cast<unsigned char>(value >> shift));
        hash ^= byte;
        hash *= kFnvPrime;
    }
}

bool valid_range(std::size_t first_index, std::size_t count) noexcept {
    return count != 0 && first_index <= std::numeric_limits<std::size_t>::max() - count;
}

}

void ServerRows::bump_generation() noexcept {
    ++generation_;
    interval_count_ = 0;
}

bool ServerRows::same_sorts(std::span<const ServerSort> sorts) const noexcept {
    if (sort_count_ != sorts.size()) {
        return false;
    }

    for (std::size_t i = 0; i < sorts.size(); ++i) {
        if (sorts_[i].column_id != sorts[i].column_id ||
            sorts_[i].descending != sorts[i].descending) {
            return false;
        }
    }

    return true;
}

bool ServerRows::same_filters(std::span<const ServerFilter> filters) const noexcept {
    if (filter_count_ != filters.size()) {
        return false;
    }

    for (std::size_t i = 0; i < filters.size(); ++i) {
        if (filters_[i].column_id != filters[i].column_id ||
            filters_[i].predicate_id != filters[i].predicate_id) {
            return false;
        }
    }

    return true;
}

bool ServerRows::same_groups(std::span<const std::uint64_t> keys) const noexcept {
    if (group_depth_ != keys.size()) {
        return false;
    }

    for (std::size_t i = 0; i < keys.size(); ++i) {
        if (group_keys_[i] != keys[i]) {
            return false;
        }
    }

    return true;
}

ServerRowsResult ServerRows::set_sorts(std::span<const ServerSort> sorts) noexcept {
    if (sorts.size() > kSortCapacity) {
        return ServerRowsResult::ignored;
    }

    if (same_sorts(sorts)) {
        return ServerRowsResult::handled;
    }

    for (std::size_t i = 0; i < sorts.size(); ++i) {
        sorts_[i] = sorts[i];
    }
    sort_count_ = sorts.size();
    bump_generation();

    return ServerRowsResult::changed;
}

ServerRowsResult ServerRows::set_filters(std::span<const ServerFilter> filters) noexcept {
    if (filters.size() > kFilterCapacity) {
        return ServerRowsResult::ignored;
    }

    if (same_filters(filters)) {
        return ServerRowsResult::handled;
    }

    for (std::size_t i = 0; i < filters.size(); ++i) {
        filters_[i] = filters[i];
    }
    filter_count_ = filters.size();
    bump_generation();

    return ServerRowsResult::changed;
}

ServerRowsResult ServerRows::set_group_keys(std::span<const std::uint64_t> keys) noexcept {
    if (keys.size() > kGroupDepth) {
        return ServerRowsResult::ignored;
    }

    if (same_groups(keys)) {
        return ServerRowsResult::handled;
    }

    for (std::size_t i = 0; i < keys.size(); ++i) {
        group_keys_[i] = keys[i];
    }
    group_depth_ = keys.size();
    bump_generation();

    return ServerRowsResult::changed;
}

ServerRowsResult ServerRows::refresh() noexcept {
    bump_generation();

    return ServerRowsResult::changed;
}

ServerRowsResult ServerRows::touch() noexcept {
    bump_generation();

    return ServerRowsResult::changed;
}

std::uint64_t ServerRows::request_key() const noexcept {
    std::uint64_t hash = kFnvOffsetBasis;

    mix_value(hash, generation_);
    mix_value(hash, static_cast<std::uint64_t>(sort_count_));

    for (std::size_t i = 0; i < sort_count_; ++i) {
        mix_value(hash, sorts_[i].column_id);
        mix_value(hash, sorts_[i].descending ? 1ULL : 0ULL);
    }

    mix_value(hash, static_cast<std::uint64_t>(filter_count_));

    for (std::size_t i = 0; i < filter_count_; ++i) {
        mix_value(hash, filters_[i].column_id);
        mix_value(hash, filters_[i].predicate_id);
    }

    mix_value(hash, static_cast<std::uint64_t>(group_depth_));

    for (std::size_t i = 0; i < group_depth_; ++i) {
        mix_value(hash, group_keys_[i]);
    }

    return hash;
}

std::uint64_t ServerRows::query_generation() const noexcept {
    return generation_;
}

bool ServerRows::is_current(std::uint64_t key) const noexcept {
    return key == request_key();
}

ServerRowsResult ServerRows::confirm(std::uint64_t key) const noexcept {
    if (!is_current(key)) {
        return ServerRowsResult::stale;
    }

    return ServerRowsResult::handled;
}

ServerRowsResult
ServerRows::note_loaded(std::size_t first_index, std::size_t count, std::uint64_t key) noexcept {
    if (!is_current(key)) {
        return ServerRowsResult::stale;
    }

    if (!valid_range(first_index, count)) {
        return ServerRowsResult::ignored;
    }

    if (is_covered(first_index, count)) {
        return ServerRowsResult::handled;
    }

    std::size_t merged_first = first_index;
    std::size_t merged_end = first_index + count;
    std::size_t write = 0;

    for (std::size_t i = 0; i < interval_count_; ++i) {
        const std::size_t start = intervals_[i].first_index;
        const std::size_t stop = start + intervals_[i].count;

        if (stop < merged_first || start > merged_end) {
            intervals_[write] = intervals_[i];
            ++write;
            continue;
        }

        if (start < merged_first) {
            merged_first = start;
        }

        if (stop > merged_end) {
            merged_end = stop;
        }
    }

    if (write == kRangeCapacity) {
        return ServerRowsResult::overflow;
    }

    std::size_t pos = 0;

    while (pos < write && intervals_[pos].first_index < merged_first) {
        ++pos;
    }

    for (std::size_t j = write; j > pos; --j) {
        intervals_[j] = intervals_[j - 1];
    }

    intervals_[pos] = ServerLoadedRange{merged_first, merged_end - merged_first};
    interval_count_ = write + 1;

    return ServerRowsResult::changed;
}

bool ServerRows::is_covered(std::size_t first_index, std::size_t count) const noexcept {
    if (count == 0) {
        return true;
    }

    if (first_index > std::numeric_limits<std::size_t>::max() - count) {
        return false;
    }

    const std::size_t end = first_index + count;

    for (std::size_t i = 0; i < interval_count_; ++i) {
        const std::size_t start = intervals_[i].first_index;

        if (start <= first_index && start + intervals_[i].count >= end) {
            return true;
        }
    }

    return false;
}

ServerRowsResult ServerRows::evict(std::size_t first_index, std::size_t count) noexcept {
    if (!valid_range(first_index, count)) {
        return ServerRowsResult::ignored;
    }

    const std::size_t end = first_index + count;
    std::array<ServerLoadedRange, kRangeCapacity + 1> staged{};
    std::size_t staged_count = 0;
    bool changed = false;

    for (std::size_t i = 0; i < interval_count_; ++i) {
        const std::size_t start = intervals_[i].first_index;
        const std::size_t stop = start + intervals_[i].count;

        if (stop <= first_index || start >= end) {
            staged[staged_count] = intervals_[i];
            ++staged_count;
            continue;
        }

        changed = true;

        if (start < first_index) {
            staged[staged_count] = ServerLoadedRange{start, first_index - start};
            ++staged_count;
        }

        if (stop > end) {
            staged[staged_count] = ServerLoadedRange{end, stop - end};
            ++staged_count;
        }
    }

    if (!changed) {
        return ServerRowsResult::handled;
    }

    interval_count_ = staged_count <= kRangeCapacity ? staged_count : kRangeCapacity;

    for (std::size_t i = 0; i < interval_count_; ++i) {
        intervals_[i] = staged[i];
    }

    return ServerRowsResult::changed;
}

bool ServerRows::clear() noexcept {
    if (interval_count_ == 0) {
        return false;
    }

    interval_count_ = 0;

    return true;
}

std::size_t ServerRows::sort_count() const noexcept {
    return sort_count_;
}

std::size_t ServerRows::copy_sorts(std::span<ServerSort> output) const noexcept {
    const std::size_t count = output.size() < sort_count_ ? output.size() : sort_count_;

    for (std::size_t i = 0; i < count; ++i) {
        output[i] = sorts_[i];
    }

    return count;
}

std::size_t ServerRows::filter_count() const noexcept {
    return filter_count_;
}

std::size_t ServerRows::copy_filters(std::span<ServerFilter> output) const noexcept {
    const std::size_t count = output.size() < filter_count_ ? output.size() : filter_count_;

    for (std::size_t i = 0; i < count; ++i) {
        output[i] = filters_[i];
    }

    return count;
}

std::size_t ServerRows::group_depth() const noexcept {
    return group_depth_;
}

std::size_t ServerRows::copy_group_keys(std::span<std::uint64_t> output) const noexcept {
    const std::size_t count = output.size() < group_depth_ ? output.size() : group_depth_;

    for (std::size_t i = 0; i < count; ++i) {
        output[i] = group_keys_[i];
    }

    return count;
}

std::size_t ServerRows::interval_count() const noexcept {
    return interval_count_;
}

std::size_t ServerRows::copy_intervals(std::span<ServerLoadedRange> output) const noexcept {
    const std::size_t count = output.size() < interval_count_ ? output.size() : interval_count_;

    for (std::size_t i = 0; i < count; ++i) {
        output[i] = intervals_[i];
    }

    return count;
}

}
