#include "veyra/data/server_rows.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

void require(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

constexpr veyra::ServerRowsResult kIgnored = veyra::ServerRowsResult::ignored;
constexpr veyra::ServerRowsResult kHandled = veyra::ServerRowsResult::handled;
constexpr veyra::ServerRowsResult kChanged = veyra::ServerRowsResult::changed;
constexpr veyra::ServerRowsResult kStale = veyra::ServerRowsResult::stale;
constexpr veyra::ServerRowsResult kOverflow = veyra::ServerRowsResult::overflow;

void check_initial_state() {
    auto rows = veyra::ServerRows{};

    require(rows.sort_count() == 0, "a new behavior holds no sorts");
    require(rows.filter_count() == 0, "a new behavior holds no filters");
    require(rows.group_depth() == 0, "a new behavior holds no group keys");
    require(rows.interval_count() == 0, "a new behavior covers no ranges");
    require(rows.query_generation() == 0, "a new behavior starts at generation zero");
    require(rows.request_key() == rows.request_key(), "the request key is stable without mutation");

    const std::uint64_t key = rows.request_key();
    require(rows.is_current(key), "the fresh key is current");
    require(rows.confirm(key) == kHandled, "confirming the fresh key reports no change");
    require(!rows.is_covered(0, 50), "a new behavior covers no rows");
    require(rows.is_covered(0, 0), "an empty range needs no rows and is vacuously covered");
    require(!rows.clear(), "clearing an empty behavior reports no change");
}

void check_sorts_replace_and_feed_key() {
    auto rows = veyra::ServerRows{};
    const std::uint64_t base = rows.request_key();

    const std::array<veyra::ServerSort, 2> sorts = {
        veyra::ServerSort{3, false},
        veyra::ServerSort{7, true},
    };
    require(rows.set_sorts(sorts) == kChanged, "setting new sorts reports a change");
    require(rows.sort_count() == 2, "both sorts are stored in order");
    require(rows.request_key() != base, "a sort mutation changes the request key");
    require(!rows.is_current(base), "the pre-sort key is stale after the mutation");

    std::array<veyra::ServerSort, 2> copied{};
    require(rows.copy_sorts(copied) == 2, "stored sorts copy back verbatim");
    require(copied[0].column_id == 3 && !copied[0].descending, "the first sort keeps its shape");
    require(copied[1].column_id == 7 && copied[1].descending, "the second sort keeps its shape");

    const std::uint64_t keyed = rows.request_key();
    require(rows.set_sorts(sorts) == kHandled, "repeating identical sorts reports no change");
    require(rows.request_key() == keyed, "an identical sort set keeps the request key");

    const std::array<veyra::ServerSort, 2> flipped = {
        veyra::ServerSort{7, true},
        veyra::ServerSort{3, false},
    };
    require(rows.set_sorts(flipped) == kChanged, "reordering sorts reports a change");
    require(rows.request_key() != keyed, "sort order feeds the request key");

    const std::array<veyra::ServerSort, 9> too_many{};
    require(rows.set_sorts(too_many) == kIgnored, "a ninth sort is rejected, never truncated");
    require(rows.sort_count() == 2, "a rejected sort set leaves the stored sorts alone");

    const std::array<veyra::ServerSort, 0> empty{};
    require(
        rows.set_sorts(empty) == kChanged,
        "clearing sorts through an empty set reports a change"
    );
    require(rows.sort_count() == 0, "clearing sorts drops every stored sort");
}

void check_filters_are_opaque_caller_tokens() {
    auto rows = veyra::ServerRows{};

    const std::array<veyra::ServerFilter, 1> contains = {veyra::ServerFilter{3, 11}};
    require(rows.set_filters(contains) == kChanged, "setting a filter reports a change");
    const std::uint64_t contains_key = rows.request_key();

    std::array<veyra::ServerFilter, 1> carried{};
    require(rows.copy_filters(carried) == 1, "the stored filter copies back");
    require(
        carried[0].column_id == 3 && carried[0].predicate_id == 11,
        "the behavior carries the opaque predicate token without interpreting it"
    );

    const std::array<veyra::ServerFilter, 1> equals = {veyra::ServerFilter{3, 12}};
    auto probe = veyra::ServerRows{};
    require(probe.set_filters(equals) == kChanged, "the probe accepts its own filter");
    require(
        probe.request_key() != contains_key,
        "predicate identity alone changes the key, so the caller interprets meaning elsewhere"
    );

    require(rows.set_filters(contains) == kHandled, "repeating the filter reports no change");
    require(rows.request_key() == contains_key, "an identical filter keeps the request key");

    const std::array<veyra::ServerFilter, 9> too_many{};
    require(rows.set_filters(too_many) == kIgnored, "a ninth filter is rejected, never truncated");
    require(rows.filter_count() == 1, "a rejected filter set leaves the stored filters alone");
}

void check_group_keys_bound_depth() {
    auto rows = veyra::ServerRows{};
    const std::uint64_t base = rows.request_key();

    const std::array<std::uint64_t, 2> keys = {9, 4};
    require(rows.set_group_keys(keys) == kChanged, "setting group keys reports a change");
    require(rows.group_depth() == 2, "the group depth tracks the key count");
    require(rows.request_key() != base, "a group mutation changes the request key");

    std::array<std::uint64_t, 4> copied{};
    require(rows.copy_group_keys(copied) == 2, "the stored group keys copy back");
    require(copied[0] == 9 && copied[1] == 4, "group keys keep their order");

    require(rows.set_group_keys(keys) == kHandled, "repeating group keys reports no change");

    const std::array<std::uint64_t, 1> different = {9};
    require(rows.set_group_keys(different) == kChanged, "shortening the path reports a change");

    const std::array<std::uint64_t, 5> too_deep = {1, 2, 3, 4, 5};
    require(rows.set_group_keys(too_deep) == kIgnored, "a fifth group level is rejected");
    require(rows.group_depth() == 1, "a rejected group set leaves the stored path alone");
}

void check_every_mutation_kind_changes_key() {
    auto rows = veyra::ServerRows{};
    std::array<std::uint64_t, 5> retired{};
    std::size_t retired_count = 0;
    std::uint64_t previous = rows.request_key();

    const auto mutate = [&](std::string_view message, veyra::ServerRowsResult result) {
        require(result == kChanged, message);
        const std::uint64_t live = rows.request_key();
        require(live != previous, message);
        require(!rows.is_current(previous), message);
        retired[retired_count++] = previous;
        previous = live;
    };

    const std::array<veyra::ServerSort, 1> sort = {veyra::ServerSort{1, false}};
    mutate("a sort mutation changes the key", rows.set_sorts(sort));

    const std::array<veyra::ServerFilter, 1> filter = {veyra::ServerFilter{1, 2}};
    mutate("a filter mutation changes the key", rows.set_filters(filter));

    const std::array<std::uint64_t, 1> group = {8};
    mutate("a group mutation changes the key", rows.set_group_keys(group));

    mutate("refresh changes the key like the pipeline generation", rows.refresh());
    mutate("touch changes the key like any other invalidation", rows.touch());

    require(retired_count == 5, "every mutation kind retired exactly one key");
    for (std::size_t i = 0; i < retired_count; ++i) {
        for (std::size_t j = i + 1; j < retired_count; ++j) {
            require(retired[i] != retired[j], "retired keys never repeat across mutation kinds");
        }
        require(rows.confirm(retired[i]) == kStale, "every retired confirmation is dropped");
    }
}

void check_refresh_and_touch_clear_coverage() {
    auto rows = veyra::ServerRows{};
    const std::uint64_t key = rows.request_key();
    require(rows.note_loaded(0, 100, key) == kChanged, "loading a range reports a change");
    require(rows.is_covered(0, 100), "the loaded range is covered before refresh");

    require(rows.refresh() == kChanged, "refresh always starts a new snapshot");
    require(rows.interval_count() == 0, "refresh drops every loaded range");
    require(!rows.is_covered(0, 100), "the old range is uncovered after refresh");
    require(!rows.clear(), "refresh leaves nothing behind to clear");

    const std::uint64_t fresh = rows.request_key();
    require(rows.note_loaded(0, 100, fresh) == kChanged, "the new snapshot loads again");
    require(rows.touch() == kChanged, "touch always invalidates the snapshot");
    require(rows.interval_count() == 0, "touch drops every loaded range");
    require(!rows.is_covered(0, 100), "the old range is uncovered after touch");
    require(!rows.is_current(fresh), "the pre-touch key is stale after touch");
    require(rows.query_generation() == 2, "refresh plus touch bump the generation twice");
}

void check_stale_loads_are_dropped() {
    auto rows = veyra::ServerRows{};
    const std::uint64_t old_key = rows.request_key();

    const std::array<veyra::ServerSort, 1> sort = {veyra::ServerSort{5, true}};
    require(rows.set_sorts(sort) == kChanged, "a sort mutation retires the old key");

    require(rows.confirm(old_key) == kStale, "a stale confirmation is dropped, never applied");
    require(
        rows.note_loaded(0, 50, old_key) == kStale,
        "a stale load is dropped and keeps no coverage"
    );
    require(rows.interval_count() == 0, "a stale load stores nothing");
    require(!rows.is_covered(0, 50), "a stale load covers nothing");

    const std::uint64_t live = rows.request_key();
    require(rows.note_loaded(0, 50, live) == kChanged, "the live key still loads");
    require(rows.is_covered(0, 50), "the live load covers its range");
}

void check_note_loaded_edge_cases() {
    auto rows = veyra::ServerRows{};
    const std::uint64_t key = rows.request_key();

    require(rows.note_loaded(10, 0, key) == kIgnored, "an empty load never records");
    require(
        rows.note_loaded(std::numeric_limits<std::size_t>::max(), 1, key) == kIgnored,
        "an overflowing load never records"
    );
    require(rows.interval_count() == 0, "rejected loads store nothing");

    require(rows.note_loaded(10, 20, key + 1) == kStale, "a foreign key is dropped as stale");

    require(rows.note_loaded(10, 20, key) == kChanged, "a first load reports a change");
    require(rows.note_loaded(10, 20, key) == kHandled, "reloading covered rows reports no change");
    require(
        rows.note_loaded(15, 5, key) == kHandled,
        "loading a covered subrange reports no change"
    );
    require(rows.interval_count() == 1, "duplicate loads never duplicate intervals");
}

void check_intervals_coalesce_adjacent_and_overlap() {
    auto rows = veyra::ServerRows{};
    const std::uint64_t key = rows.request_key();

    require(rows.note_loaded(0, 10, key) == kChanged, "the first range loads");
    require(rows.note_loaded(10, 10, key) == kChanged, "an adjacent range loads");
    require(rows.interval_count() == 1, "adjacent ranges coalesce into one interval");

    std::array<veyra::ServerLoadedRange, 2> merged{};
    require(rows.copy_intervals(merged) == 1, "the merged interval copies back");
    require(
        merged[0].first_index == 0 && merged[0].count == 20,
        "adjacent ranges fuse end to start"
    );

    require(rows.note_loaded(30, 10, key) == kChanged, "a gapped range loads apart");
    require(rows.interval_count() == 2, "a gapped range stays its own interval");

    require(rows.note_loaded(15, 20, key) == kChanged, "a bridging range loads across the gap");
    require(rows.interval_count() == 1, "a bridging range fuses every interval it touches");
    require(rows.copy_intervals(merged) == 1, "the fused interval copies back");
    require(
        merged[0].first_index == 0 && merged[0].count == 40,
        "overlap plus adjacency fuse into one span"
    );
    require(rows.is_covered(0, 40), "the fused span is fully covered");

    require(rows.note_loaded(100, 10, key) == kChanged, "a far range loads apart");
    require(rows.note_loaded(60, 40, key) == kChanged, "a left-touching range loads");
    require(rows.interval_count() == 2, "touching the far range merges without joining the span");
    require(rows.is_covered(60, 50), "the far span covers its fused range");
    require(!rows.is_covered(40, 20), "the gap between spans stays uncovered");
}

void check_coverage_needs_full_containment() {
    auto rows = veyra::ServerRows{};
    const std::uint64_t key = rows.request_key();
    require(rows.note_loaded(20, 20, key) == kChanged, "the window loads");

    require(rows.is_covered(20, 20), "the exact window is covered");
    require(rows.is_covered(25, 5), "a subrange of the window is covered");
    require(!rows.is_covered(10, 10), "a range ending at the window edge is uncovered");
    require(!rows.is_covered(40, 1), "a range starting at the window end is uncovered");
    require(!rows.is_covered(10, 30), "a range overhanging the left edge is uncovered");
    require(!rows.is_covered(30, 20), "a range overhanging the right edge is uncovered");
    require(
        !rows.is_covered(std::numeric_limits<std::size_t>::max(), 1),
        "an overflowing probe is never covered"
    );
}

void check_evict_trims_splits_and_clears() {
    auto rows = veyra::ServerRows{};
    const std::uint64_t key = rows.request_key();
    require(rows.note_loaded(0, 100, key) == kChanged, "the span loads");

    require(rows.evict(0, 20) == kChanged, "evicting the head reports a change");
    require(!rows.is_covered(0, 20), "the evicted head is uncovered");
    require(rows.is_covered(20, 80), "the surviving tail stays covered");

    require(rows.evict(90, 10) == kChanged, "evicting the tail reports a change");
    require(rows.is_covered(20, 70), "trimming both ends keeps the middle");

    require(rows.evict(40, 10) == kChanged, "punching a hole reports a change");
    require(rows.interval_count() == 2, "a hole punch splits one interval into two");
    require(rows.is_covered(20, 20), "the left survivor stays covered");
    require(rows.is_covered(50, 40), "the right survivor stays covered");
    require(!rows.is_covered(30, 30), "no single interval spans the hole");

    require(rows.evict(200, 10) == kHandled, "evicting thin air reports no change");
    require(rows.evict(0, 0) == kIgnored, "an empty eviction is rejected");
    require(
        rows.evict(std::numeric_limits<std::size_t>::max(), 1) == kIgnored,
        "an overflowing eviction is rejected"
    );

    require(rows.evict(0, 100) == kChanged, "evicting everything reports a change");
    require(rows.interval_count() == 0, "evicting everything keeps no intervals");
    require(!rows.is_covered(20, 20), "nothing stays covered after a full eviction");
    require(!rows.clear(), "a fully evicted behavior has nothing left to clear");
}

void check_interval_overflow_rejects_without_dropping() {
    auto rows = veyra::ServerRows{};
    const std::uint64_t key = rows.request_key();

    for (std::size_t i = 0; i < veyra::ServerRows::kRangeCapacity; ++i) {
        require(rows.note_loaded(i * 10, 1, key) == kChanged, "each disjoint probe range loads");
    }
    require(
        rows.interval_count() == veyra::ServerRows::kRangeCapacity,
        "sixteen disjoint ranges fill the interval list"
    );

    require(
        rows.note_loaded(1000, 1, key) == kOverflow,
        "a seventeenth disjoint range overflows instead of dropping coverage"
    );
    require(
        rows.interval_count() == veyra::ServerRows::kRangeCapacity,
        "the rejected range stores nothing"
    );

    for (std::size_t i = 0; i < veyra::ServerRows::kRangeCapacity; ++i) {
        require(rows.is_covered(i * 10, 1), "kept ranges stay covered after the rejection");
    }
    require(!rows.is_covered(1000, 1), "the rejected range stays uncovered");

    require(
        rows.note_loaded(1, 1, key) == kChanged,
        "an adjacent range still merges when the list is full"
    );
    require(
        rows.interval_count() == veyra::ServerRows::kRangeCapacity,
        "merging into a full list stores no extra interval"
    );
    require(rows.is_covered(0, 2), "the merged span covers both fused ranges");

    require(rows.evict(0, 2) == kChanged, "evicting one span frees exactly one slot");
    require(rows.note_loaded(1000, 1, key) == kChanged, "a freed slot accepts the range");
    require(rows.is_covered(1000, 1), "the retried range is covered after the eviction");
}

void check_evict_split_overflow_drops_only_the_tail() {
    auto rows = veyra::ServerRows{};
    const std::uint64_t key = rows.request_key();

    require(rows.note_loaded(0, 100, key) == kChanged, "the wide span loads first");
    for (std::size_t i = 0; i + 1 < veyra::ServerRows::kRangeCapacity; ++i) {
        require(
            rows.note_loaded(200 + i * 2, 1, key) == kChanged,
            "fifteen singletons fill the remaining slots"
        );
    }
    require(
        rows.interval_count() == veyra::ServerRows::kRangeCapacity,
        "the wide span plus singletons fill the list"
    );

    require(rows.evict(40, 10) == kChanged, "splitting the wide span reports a change");
    require(
        rows.interval_count() == veyra::ServerRows::kRangeCapacity,
        "the split keeps sixteen intervals by dropping the highest piece"
    );
    require(rows.is_covered(0, 40), "the left half of the split stays covered");
    require(rows.is_covered(50, 50), "the right half of the split stays covered");
    require(rows.is_covered(226, 1), "the second-highest singleton stays covered");
    require(!rows.is_covered(228, 1), "only the highest singleton is dropped");
    require(!rows.is_covered(40, 10), "the evicted hole stays uncovered");
}

void check_clear_reports_and_keeps_query() {
    auto rows = veyra::ServerRows{};
    const std::uint64_t key = rows.request_key();
    require(rows.note_loaded(0, 10, key) == kChanged, "a range loads");

    require(rows.clear(), "clearing a loaded behavior reports a change");
    require(rows.interval_count() == 0, "clear drops every interval");
    require(!rows.is_covered(0, 10), "clear uncovers the loaded range");
    require(rows.request_key() == key, "clear keeps the query key so refetch reuses it");
    require(rows.is_current(key), "the key stays current across a clear");
}

}

int main() {
    try {
        check_initial_state();
        check_sorts_replace_and_feed_key();
        check_filters_are_opaque_caller_tokens();
        check_group_keys_bound_depth();
        check_every_mutation_kind_changes_key();
        check_refresh_and_touch_clear_coverage();
        check_stale_loads_are_dropped();
        check_note_loaded_edge_cases();
        check_intervals_coalesce_adjacent_and_overlap();
        check_coverage_needs_full_containment();
        check_evict_trims_splits_and_clears();
        check_interval_overflow_rejects_without_dropping();
        check_evict_split_overflow_drops_only_the_tail();
        check_clear_reports_and_keeps_query();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
