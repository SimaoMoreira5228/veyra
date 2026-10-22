#include "veyra/behaviors/log_feed.hpp"

#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

void require(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

void check_starts_empty() {
    veyra::LogFeed feed;

    require(feed.empty(), "a new feed holds no records");
    require(feed.size() == 0, "a new feed reports zero size");
    require(feed.generation() == 0, "a new feed starts at generation zero");
    require(
        feed.dedup_window_ms() == veyra::LogFeed::kDefaultDedupWindowMs,
        "a new feed uses the default dedup window"
    );
    require(
        veyra::LogFeed::kDefaultDedupWindowMs == 1000,
        "the default dedup window spans one second"
    );

    auto record = veyra::LogFeedRecord{};
    require(!feed.record_at(0, record), "reading past an empty feed fails");

    auto index = std::size_t{};
    require(!feed.index_of(1, index), "an empty feed resolves no id");
    require(!feed.index_of(0, index), "id zero never resolves");
    require(!feed.clear(), "clearing an empty feed reports nothing done");
    require(feed.generation() == 0, "clearing an empty feed bumps no generation");
}

void check_push_appends_distinct_rows() {
    veyra::LogFeed feed;

    const auto first = feed.push(1, veyra::LogFeedKind::info, 0);
    require(first.result == veyra::LogFeedResult::appended, "the first push appends");
    require(first.id == 1 && first.count == 1, "an appended row carries its id and a count of one");
    require(!first.evicted_oldest, "an append into room evicts nothing");
    require(feed.generation() == 1, "an append bumps the generation");

    const auto second = feed.push(2, veyra::LogFeedKind::warning, 100);
    require(second.result == veyra::LogFeedResult::appended, "a new kind appends a new row");
    require(feed.size() == 2, "distinct pushes grow the feed");
    require(feed.generation() == 2, "each append bumps the generation");

    auto record = veyra::LogFeedRecord{};
    require(feed.record_at(0, record), "the head row reads back");
    require(
        record.id == 1 && record.kind == veyra::LogFeedKind::info && record.count == 1,
        "the head row keeps its id, kind, and count"
    );
    require(feed.record_at(1, record), "the second row reads back");
    require(
        record.id == 2 && record.kind == veyra::LogFeedKind::warning,
        "the second row keeps its own kind"
    );
    require(!feed.record_at(2, record), "reading past the tail fails");

    auto index = std::size_t{99};
    require(feed.index_of(2, index) && index == 1, "an id resolves to its row index");
    require(!feed.index_of(9, index), "an unknown id resolves nowhere");
    require(!feed.index_of(0, index), "id zero resolves nowhere");
}

void check_id_zero_is_ignored() {
    veyra::LogFeed feed;

    require(
        feed.push(1, veyra::LogFeedKind::info, 0).result == veyra::LogFeedResult::appended,
        "a live row lands first"
    );

    const auto dropped = feed.push(0, veyra::LogFeedKind::error, 10);
    require(dropped.result == veyra::LogFeedResult::ignored, "id zero is ignored");
    require(feed.size() == 1, "an ignored push grows nothing");
    require(feed.generation() == 1, "an ignored push bumps no generation");
}

void check_same_kind_collapses_inside_window() {
    veyra::LogFeed feed;

    require(
        feed.push(1, veyra::LogFeedKind::info, 0).result == veyra::LogFeedResult::appended,
        "the run opens with an append"
    );

    const auto collapsed = feed.push(2, veyra::LogFeedKind::info, 500);
    require(
        collapsed.result == veyra::LogFeedResult::collapsed,
        "a repeat inside the window collapses"
    );
    require(
        collapsed.id == 2 && collapsed.count == 2,
        "a collapse reports the newest id and count"
    );
    require(!collapsed.evicted_oldest, "a collapse evicts nothing");
    require(feed.size() == 1, "a collapse adds no row");
    require(feed.generation() == 2, "a collapse bumps the generation");

    auto record = veyra::LogFeedRecord{};
    require(feed.record_at(0, record), "the collapsed row reads back");
    require(
        record.id == 2 && record.count == 2,
        "a collapse keeps the newest id and grows the count"
    );

    auto index = std::size_t{99};
    require(feed.index_of(2, index) && index == 0, "the newest id resolves to the collapsed row");
    require(!feed.index_of(1, index), "the absorbed id resolves nowhere");
}

void check_window_boundary_is_inclusive() {
    veyra::LogFeed feed;

    require(
        feed.push(1, veyra::LogFeedKind::error, 0).result == veyra::LogFeedResult::appended,
        "the error run opens"
    );
    require(
        feed.push(2, veyra::LogFeedKind::error, 1000).result == veyra::LogFeedResult::collapsed,
        "a repeat exactly on the window edge still collapses"
    );
    require(
        feed.push(3, veyra::LogFeedKind::error, 2001).result == veyra::LogFeedResult::appended,
        "a repeat one millisecond past the edge opens a new row"
    );
    require(feed.size() == 2, "the late repeat appends instead of collapsing");

    auto record = veyra::LogFeedRecord{};
    require(feed.record_at(0, record) && record.count == 2, "the first row folded two events");
    require(feed.record_at(1, record) && record.id == 3, "the late event starts the second row");
}

void check_window_slides_with_each_collapse() {
    veyra::LogFeed feed;

    require(
        feed.push(1, veyra::LogFeedKind::info, 0).result == veyra::LogFeedResult::appended,
        "the run opens"
    );
    require(
        feed.push(2, veyra::LogFeedKind::info, 900).result == veyra::LogFeedResult::collapsed,
        "a repeat near the edge collapses"
    );
    require(
        feed.push(3, veyra::LogFeedKind::info, 1800).result == veyra::LogFeedResult::collapsed,
        "the window measures from the last collapse, not the first push"
    );
    require(
        feed.push(4, veyra::LogFeedKind::info, 2801).result == veyra::LogFeedResult::appended,
        "a repeat past the slid window opens a new row"
    );

    auto record = veyra::LogFeedRecord{};
    require(
        feed.record_at(0, record) && record.count == 3,
        "three events folded into the first row"
    );
    require(feed.record_at(1, record) && record.count == 1, "the late event starts its own row");
}

void check_kind_change_breaks_the_run() {
    veyra::LogFeed feed;

    require(
        feed.push(1, veyra::LogFeedKind::info, 0).result == veyra::LogFeedResult::appended,
        "info opens a row"
    );
    require(
        feed.push(2, veyra::LogFeedKind::warning, 10).result == veyra::LogFeedResult::appended,
        "a kind change appends even inside the window"
    );
    require(
        feed.push(3, veyra::LogFeedKind::info, 20).result == veyra::LogFeedResult::appended,
        "returning to info appends too, only the tail can collapse"
    );
    require(feed.size() == 3, "alternating kinds never collapse");
}

void check_backwards_clock_opens_a_row() {
    veyra::LogFeed feed;

    require(
        feed.push(1, veyra::LogFeedKind::info, 500).result == veyra::LogFeedResult::appended,
        "the row anchors at its push time"
    );

    const auto skewed = feed.push(2, veyra::LogFeedKind::info, 100);
    require(skewed.result == veyra::LogFeedResult::appended, "a skewed clock opens a row");
    require(feed.size() == 2, "clock skew never folds the past into the future");
    require(feed.generation() == 2, "the skew row still bumps the generation");
}

void check_overflow_evicts_oldest_first() {
    veyra::LogFeed feed;

    for (auto id = std::uint64_t{1}; id <= veyra::LogFeed::kCapacity; ++id) {
        const auto kind = id % 2 == 0 ? veyra::LogFeedKind::warning : veyra::LogFeedKind::info;
        const auto pushed = feed.push(id, kind, id * 2000);
        require(pushed.result == veyra::LogFeedResult::appended, "the feed fills row by row");
        require(!pushed.evicted_oldest, "filling into room evicts nothing");
    }

    require(feed.size() == veyra::LogFeed::kCapacity, "the feed fills to capacity");
    require(feed.generation() == veyra::LogFeed::kCapacity, "every fill push bumps the generation");

    const auto overflow = feed.push(1001, veyra::LogFeedKind::error, 9999999);
    require(overflow.result == veyra::LogFeedResult::appended, "a full feed still appends");
    require(overflow.evicted_oldest, "a full feed reports its eviction");
    require(feed.size() == veyra::LogFeed::kCapacity, "eviction keeps the feed bounded");

    auto index = std::size_t{};
    require(!feed.index_of(1, index), "the oldest id is gone");
    require(feed.index_of(2, index) && index == 0, "the second row shifts to the head");
    require(
        feed.index_of(1001, index) && index == veyra::LogFeed::kCapacity - 1,
        "the burst lands at the tail"
    );

    auto record = veyra::LogFeedRecord{};
    require(feed.record_at(0, record) && record.id == 2, "the head row is the oldest survivor");

    const auto tail_kind = veyra::LogFeedKind::error;
    const auto folded = feed.push(1002, tail_kind, 10000000);
    require(
        folded.result == veyra::LogFeedResult::collapsed,
        "a same-kind repeat still collapses when full"
    );
    require(!folded.evicted_oldest, "a collapse evicts nothing even when full");
    require(feed.size() == veyra::LogFeed::kCapacity, "a collapse keeps the bound");
    require(feed.index_of(1001, index) == false, "the folded id supersedes the tail id");
}

void check_window_is_configurable() {
    veyra::LogFeed feed;

    require(!feed.set_dedup_window_ms(1000), "setting the current window reports no change");
    require(feed.dedup_window_ms() == 1000, "a no-op set keeps the window");
    require(feed.generation() == 0, "reconfiguring the window bumps no generation");
    require(feed.set_dedup_window_ms(0), "narrowing the window reports a change");

    require(
        feed.push(1, veyra::LogFeedKind::info, 50).result == veyra::LogFeedResult::appended,
        "the run opens under a zero window"
    );
    require(
        feed.push(2, veyra::LogFeedKind::info, 50).result == veyra::LogFeedResult::collapsed,
        "a zero window still folds a same-timestamp repeat"
    );
    require(
        feed.push(3, veyra::LogFeedKind::info, 51).result == veyra::LogFeedResult::appended,
        "a zero window splits any later timestamp"
    );

    require(feed.set_dedup_window_ms(60000), "widening the window reports a change");
    require(
        feed.push(4, veyra::LogFeedKind::info, 50000).result == veyra::LogFeedResult::collapsed,
        "a wide window folds a far repeat into the tail"
    );
}

void check_clear_resets_and_bumps_once() {
    veyra::LogFeed feed;

    require(
        feed.push(1, veyra::LogFeedKind::info, 0).result == veyra::LogFeedResult::appended,
        "a row lands"
    );
    require(
        feed.push(2, veyra::LogFeedKind::info, 10).result == veyra::LogFeedResult::collapsed,
        "a repeat folds"
    );

    const auto before = feed.generation();

    require(feed.clear(), "clearing a live feed reports work done");
    require(feed.empty() && feed.size() == 0, "clear empties the feed");
    require(feed.generation() == before + 1, "clear bumps the generation exactly once");

    auto index = std::size_t{};
    require(!feed.index_of(2, index), "cleared ids resolve nowhere");

    require(
        feed.push(3, veyra::LogFeedKind::warning, 20).result == veyra::LogFeedResult::appended,
        "a cleared feed accepts new rows"
    );
    require(feed.size() == 1, "the new row stands alone");

    auto record = veyra::LogFeedRecord{};
    require(feed.record_at(0, record) && record.count == 1, "the new row starts unfolded");
}

}

int main() {
    try {
        check_starts_empty();
        check_push_appends_distinct_rows();
        check_id_zero_is_ignored();
        check_same_kind_collapses_inside_window();
        check_window_boundary_is_inclusive();
        check_window_slides_with_each_collapse();
        check_kind_change_breaks_the_run();
        check_backwards_clock_opens_a_row();
        check_overflow_evicts_oldest_first();
        check_window_is_configurable();
        check_clear_resets_and_bumps_once();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
