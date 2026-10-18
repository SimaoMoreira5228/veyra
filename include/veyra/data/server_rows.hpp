#ifndef VEYRA_DATA_SERVER_ROWS_HPP
#define VEYRA_DATA_SERVER_ROWS_HPP

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace veyra {

enum class ServerRowsResult : std::uint8_t {
    ignored,
    handled,
    changed,
    stale,
    overflow,
};

struct ServerSort {
    std::uint64_t column_id{};
    bool descending{};
};

struct ServerFilter {
    std::uint64_t column_id{};
    std::uint64_t predicate_id{};
};

struct ServerLoadedRange {
    std::size_t first_index{};
    std::size_t count{};
};

class ServerRows {
  public:
    static constexpr std::size_t kSortCapacity = 8;
    static constexpr std::size_t kFilterCapacity = 8;
    static constexpr std::size_t kGroupDepth = 4;
    static constexpr std::size_t kRangeCapacity = 16;

    ServerRowsResult set_sorts(std::span<const ServerSort> sorts) noexcept;
    ServerRowsResult set_filters(std::span<const ServerFilter> filters) noexcept;
    ServerRowsResult set_group_keys(std::span<const std::uint64_t> keys) noexcept;

    ServerRowsResult refresh() noexcept;
    ServerRowsResult touch() noexcept;

    std::uint64_t request_key() const noexcept;
    std::uint64_t query_generation() const noexcept;
    bool is_current(std::uint64_t key) const noexcept;
    ServerRowsResult confirm(std::uint64_t key) const noexcept;

    ServerRowsResult
    note_loaded(std::size_t first_index, std::size_t count, std::uint64_t key) noexcept;
    bool is_covered(std::size_t first_index, std::size_t count) const noexcept;
    ServerRowsResult evict(std::size_t first_index, std::size_t count) noexcept;
    bool clear() noexcept;

    std::size_t sort_count() const noexcept;
    std::size_t copy_sorts(std::span<ServerSort> output) const noexcept;
    std::size_t filter_count() const noexcept;
    std::size_t copy_filters(std::span<ServerFilter> output) const noexcept;
    std::size_t group_depth() const noexcept;
    std::size_t copy_group_keys(std::span<std::uint64_t> output) const noexcept;
    std::size_t interval_count() const noexcept;
    std::size_t copy_intervals(std::span<ServerLoadedRange> output) const noexcept;

  private:
    void bump_generation() noexcept;
    bool same_sorts(std::span<const ServerSort> sorts) const noexcept;
    bool same_filters(std::span<const ServerFilter> filters) const noexcept;
    bool same_groups(std::span<const std::uint64_t> keys) const noexcept;

    std::array<ServerSort, kSortCapacity> sorts_{};
    std::size_t sort_count_{};
    std::array<ServerFilter, kFilterCapacity> filters_{};
    std::size_t filter_count_{};
    std::array<std::uint64_t, kGroupDepth> group_keys_{};
    std::size_t group_depth_{};
    std::array<ServerLoadedRange, kRangeCapacity> intervals_{};
    std::size_t interval_count_{};
    std::uint64_t generation_{};
};

}

#endif
