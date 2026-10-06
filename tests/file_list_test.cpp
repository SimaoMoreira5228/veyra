#include "veyra/files/file_list.hpp"

#include <array>
#include <iostream>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

void require(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

void check_directory_grouping_and_stable_sort() {
    const std::array entries{
        veyra::FileListEntry{
            .id = 5,
            .name = u8"zeta.txt",
            .size_bytes = 100,
            .modified_nanoseconds = 30,
        },
        veyra::FileListEntry{
            .id = 4,
            .name = u8"folder",
            .is_directory = true,
        },
        veyra::FileListEntry{
            .id = 2,
            .name = u8"alpha.txt",
            .size_bytes = 10,
            .modified_nanoseconds = 20,
        },
        veyra::FileListEntry{
            .id = 1,
            .name = u8"alpha",
            .size_bytes = 10,
            .modified_nanoseconds = 40,
            .is_directory = true,
        },
        veyra::FileListEntry{
            .id = 3,
            .name = u8"beta.txt",
            .size_bytes = 10,
            .modified_nanoseconds = 20,
        },
    };
    std::array<std::size_t, 5> order{};
    auto count = std::size_t{0};

    require(
        veyra::build_file_list_order(
            entries,
            {},
            {
                .key = veyra::FileListSortKey::name,
                .direction = veyra::FileListSortDirection::descending,
            },
            order,
            count
        ) == veyra::FileListResult::complete,
        "unfiltered file order builds"
    );
    require(count == entries.size(), "unfiltered order contains every entry");
    require(
        order == std::array<std::size_t, 5>{1, 3, 0, 4, 2},
        "directories stay first during descending sort"
    );
}

void check_order_for_sorted_entries() {
    const std::array entries{
        veyra::FileListEntry{.id = 1, .name = u8"alpha"},
        veyra::FileListEntry{.id = 2, .name = u8"beta"},
        veyra::FileListEntry{.id = 3, .name = u8"gamma"},
    };
    std::array<std::size_t, 3> order{91, 92, 93};
    auto count = std::size_t{0};

    require(
        veyra::build_file_list_order(
            entries,
            {},
            {
                .entries_are_sorted = true,
            },
            order,
            count
        ) == veyra::FileListResult::complete,
        "order builds from entries already sorted by name"
    );
    require(count == entries.size(), "sorted input keeps all entries");
    require(order == std::array<std::size_t, 3>{0, 1, 2}, "sorted input keeps its index order");
}

void check_filter_and_sort_direction() {
    const std::array entries{
        veyra::FileListEntry{
            .id = 5,
            .name = u8"zeta.txt",
            .size_bytes = 100,
        },
        veyra::FileListEntry{
            .id = 2,
            .name = u8"alpha.txt",
            .size_bytes = 10,
        },
        veyra::FileListEntry{
            .id = 3,
            .name = u8"beta.txt",
            .size_bytes = 10,
        },
        veyra::FileListEntry{
            .id = 4,
            .name = u8"notes.md",
            .modified_nanoseconds = -5,
        },
    };
    std::array<std::size_t, 4> order{};
    auto count = std::size_t{0};

    require(
        veyra::build_file_list_order(
            entries,
            u8".txt",
            {
                .key = veyra::FileListSortKey::size,
                .direction = veyra::FileListSortDirection::descending,
                .directories_first = false,
            },
            order,
            count
        ) == veyra::FileListResult::complete,
        "filtered file order builds"
    );
    require(count == 3, "filter returns only matching names");
    require(
        std::array<std::size_t, 3>{order[0], order[1], order[2]} ==
            std::array<std::size_t, 3>{0, 1, 2},
        "descending size order uses ID for equal sizes"
    );

    require(
        veyra::build_file_list_order(
            entries,
            {},
            {
                .key = veyra::FileListSortKey::modified_time,
                .directories_first = false,
            },
            order,
            count
        ) == veyra::FileListResult::complete,
        "modified time order builds"
    );
    require(
        order == std::array<std::size_t, 4>{3, 1, 2, 0},
        "signed timestamps sort in ascending order with ID tie breaks"
    );

    require(
        veyra::build_file_list_order(entries, u8".TXT", {}, order, count) ==
            veyra::FileListResult::complete,
        "case-sensitive filter with no matches succeeds"
    );
    require(count == 0, "case-sensitive filter can return no entries");
}

void check_utf8_name_filter() {
    const std::array entries{
        veyra::FileListEntry{
            .id = 1,
            .name = u8"café.png",
        },
    };
    std::array<std::size_t, 1> order{};
    auto count = std::size_t{0};

    require(
        veyra::build_file_list_order(entries, u8"fé", {}, order, count) ==
            veyra::FileListResult::complete,
        "UTF-8 filter succeeds"
    );
    require(count == 1 && order[0] == 0, "UTF-8 substring matches the file name");
}

void check_row_ids_survive_refreshes() {
    const auto previous_entries = std::array{
        veyra::FileListEntry{.id = 7, .name = u8"alpha"},
        veyra::FileListEntry{.id = 11, .name = u8"beta"},
        veyra::FileListEntry{.id = 13, .name = u8"delta"},
    };
    auto refreshed_entries = std::array{
        veyra::FileListEntry{.name = u8"beta"},
        veyra::FileListEntry{.name = u8"delta"},
        veyra::FileListEntry{.name = u8"epsilon"},
        veyra::FileListEntry{.name = u8"gamma"},
    };
    auto next_id = std::uint64_t{20};

    require(
        veyra::reconcile_file_list_row_ids(previous_entries, refreshed_entries, next_id) ==
            veyra::FileListRowIdResult::complete,
        "refresh maps existing names and assigns IDs to new rows"
    );
    require(
        std::array<std::uint64_t, 4>{
            refreshed_entries[0].id,
            refreshed_entries[1].id,
            refreshed_entries[2].id,
            refreshed_entries[3].id,
        } == std::array<std::uint64_t, 4>{11, 13, 20, 21},
        "same-name rows keep their IDs while new names get distinct IDs"
    );
    require(next_id == 22, "refresh advances the caller's next row ID");
}

void check_row_id_reconciliation_failures_preserve_output() {
    const auto previous_entries = std::array{
        veyra::FileListEntry{.id = 4, .name = u8"alpha"},
        veyra::FileListEntry{.id = 9, .name = u8"beta"},
    };
    auto next_id = std::uint64_t{10};

    auto unsorted_entries = std::array{
        veyra::FileListEntry{.id = 91, .name = u8"gamma"},
        veyra::FileListEntry{.id = 92, .name = u8"beta"},
    };
    require(
        veyra::reconcile_file_list_row_ids(previous_entries, unsorted_entries, next_id) ==
            veyra::FileListRowIdResult::invalid_order,
        "unsorted refreshed names are rejected"
    );
    require(
        unsorted_entries[0].id == 91 && unsorted_entries[1].id == 92 && next_id == 10,
        "bad order preserves refreshed entries and next ID"
    );

    auto new_entry = std::array{veyra::FileListEntry{.id = 91, .name = u8"gamma"}};
    next_id = std::numeric_limits<std::uint64_t>::max();
    require(
        veyra::reconcile_file_list_row_ids(
            std::span<const veyra::FileListEntry>{},
            new_entry,
            next_id
        ) == veyra::FileListRowIdResult::ids_exhausted,
        "row ID exhaustion is reported before the counter can overflow"
    );
    require(
        new_entry[0].id == 91 && next_id == std::numeric_limits<std::uint64_t>::max(),
        "ID exhaustion preserves refreshed entries and the next ID"
    );

    const auto zero_id_entry = std::array{veyra::FileListEntry{.name = u8"alpha"}};
    auto unchanged_entry = std::array{veyra::FileListEntry{.id = 91, .name = u8"alpha"}};
    next_id = 10;
    require(
        veyra::reconcile_file_list_row_ids(zero_id_entry, unchanged_entry, next_id) ==
            veyra::FileListRowIdResult::invalid_identity,
        "previous rows need valid IDs"
    );
    require(
        unchanged_entry[0].id == 91 && next_id == 10,
        "invalid previous identity preserves the refreshed entries"
    );
}

void check_capacity_failure_preserves_output() {
    const std::array entries{
        veyra::FileListEntry{.id = 1, .name = u8"one"},
        veyra::FileListEntry{.id = 2, .name = u8"two"},
        veyra::FileListEntry{.id = 3, .name = u8"three"},
    };
    std::array<std::size_t, 2> order{91, 92};
    auto count = std::size_t{0};

    require(
        veyra::build_file_list_order(entries, {}, {}, order, count) ==
            veyra::FileListResult::insufficient_output,
        "small output storage is rejected"
    );
    require(count == entries.size(), "capacity failure reports the required index count");
    require(order == std::array<std::size_t, 2>{91, 92}, "capacity failure leaves output alone");

    count = 99;
    require(
        veyra::build_file_list_order(
            std::span<const veyra::FileListEntry>{},
            {},
            {},
            std::span<std::size_t>{},
            count
        ) == veyra::FileListResult::complete,
        "an empty file list succeeds with empty output storage"
    );
    require(count == 0, "an empty file list reports no indices");
}

void check_selection_follows_ids_across_views() {
    const std::array entries{
        veyra::FileListEntry{.id = 11, .name = u8"delta"},
        veyra::FileListEntry{.id = 22, .name = u8"alpha"},
        veyra::FileListEntry{.id = 33, .name = u8"charlie"},
        veyra::FileListEntry{.id = 44, .name = u8"bravo"},
    };
    std::array<std::size_t, 4> visible{};
    auto visible_count = std::size_t{0};
    require(
        veyra::build_file_list_order(entries, {}, {}, visible, visible_count) ==
            veyra::FileListResult::complete,
        "selection view is sorted"
    );

    std::array<std::uint64_t, 4> storage{};
    auto selection = veyra::FileListSelection(storage);
    require(
        selection.update(entries, visible, 0) == veyra::FileListSelectionResult::complete,
        "plain click selects one file"
    );
    require(selection.contains(22) && selection.anchor_id() == 22, "selection uses the file ID");

    require(
        selection.update(entries, visible, 2, {.toggle = true}) ==
            veyra::FileListSelectionResult::complete,
        "control click adds a file"
    );
    require(
        selection.ids().size() == 2 && selection.ids()[0] == 22 && selection.ids()[1] == 33,
        "selected IDs stay sorted"
    );
    require(
        selection.update(entries, visible, 2, {.toggle = true}) ==
            veyra::FileListSelectionResult::complete,
        "control click removes an already selected file"
    );
    require(selection.ids().size() == 1 && selection.ids()[0] == 22, "toggle removes only that ID");

    require(
        selection.update(entries, visible, 3, {.extend_range = true}) ==
            veyra::FileListSelectionResult::complete,
        "shift click replaces selection with the anchored range"
    );
    require(
        selection.ids().size() == 2 && selection.ids()[0] == 11 && selection.ids()[1] == 33,
        "range selection follows visible order and stores stable IDs"
    );
    require(selection.anchor_id() == 33, "range extension keeps its anchor");

    require(
        selection.update(entries, visible, 0, {.toggle = true, .extend_range = true}) ==
            veyra::FileListSelectionResult::complete,
        "control shift click adds the anchored range"
    );
    require(selection.ids().size() == 4, "additive range keeps previous selections");
    require(selection.contains(44), "reordering the view does not lose selected IDs");

    visible_count = 0;
    require(
        veyra::build_file_list_order(entries, u8"delta", {}, visible, visible_count) ==
            veyra::FileListResult::complete,
        "filtered selection view builds"
    );
    require(
        selection.update(
            entries,
            std::span<const std::size_t>(visible.data(), visible_count),
            0,
            {.extend_range = true}
        ) == veyra::FileListSelectionResult::complete,
        "a filtered-out anchor falls back to the clicked file"
    );
    require(
        selection.ids().size() == 1 && selection.ids()[0] == 11,
        "filtering keeps selection IDs coherent"
    );
    require(selection.anchor_id() == 11, "fallback range resets the anchor");

    selection.clear();
    require(selection.ids().empty() && !selection.anchor_id(), "clear resets selection and anchor");
}

void check_selection_failures_preserve_state() {
    const std::array entries{
        veyra::FileListEntry{.id = 1, .name = u8"one"},
        veyra::FileListEntry{.id = 2, .name = u8"two"},
        veyra::FileListEntry{.id = 3, .name = u8"three"},
    };
    const std::array<std::size_t, 3> visible{0, 1, 2};
    std::array<std::uint64_t, 2> storage{};
    auto selection = veyra::FileListSelection(storage);
    require(
        selection.update(entries, visible, 1) == veyra::FileListSelectionResult::complete,
        "selection begins with one ID"
    );
    require(
        selection.update(entries, visible, 0, {.toggle = true}) ==
            veyra::FileListSelectionResult::complete,
        "selection fills its caller storage"
    );

    const auto previous_anchor = selection.anchor_id();
    require(
        selection.update(entries, visible, 2, {.toggle = true}) ==
            veyra::FileListSelectionResult::insufficient_storage,
        "selection reports full storage"
    );
    require(
        selection.update(entries, visible, 2, {.extend_range = true}) ==
            veyra::FileListSelectionResult::insufficient_storage,
        "range selection checks capacity before changing state"
    );
    require(selection.ids().size() == 2, "capacity errors preserve selected IDs");
    require(selection.anchor_id() == previous_anchor, "capacity errors preserve the anchor");

    const std::array<std::size_t, 3> invalid_order{0, 9, 2};
    require(
        selection.update(entries, invalid_order, 1) ==
            veyra::FileListSelectionResult::invalid_order,
        "invalid visible indices are rejected"
    );
    require(
        selection.update(entries, invalid_order, 2, {.extend_range = true}) ==
            veyra::FileListSelectionResult::invalid_order,
        "invalid indices inside a range are rejected"
    );
    require(
        selection.update(entries, visible, visible.size()) ==
            veyra::FileListSelectionResult::item_not_visible,
        "positions outside the visible list are rejected"
    );
    require(selection.ids().size() == 2, "invalid positions preserve selected IDs");

    auto empty_selection = veyra::FileListSelection(std::span<std::uint64_t>{});
    require(
        empty_selection.update(entries, visible, 0) ==
            veyra::FileListSelectionResult::insufficient_storage,
        "an empty selection buffer fails without writing"
    );
    require(
        empty_selection.update(entries, visible, 0, {.extend_range = true}) ==
            veyra::FileListSelectionResult::insufficient_storage,
        "an empty selection buffer rejects ranges without writing"
    );
}

void check_selection_reconciles_refreshed_entries() {
    const std::array entries{
        veyra::FileListEntry{.id = 1, .name = u8"one"},
        veyra::FileListEntry{.id = 2, .name = u8"two"},
        veyra::FileListEntry{.id = 3, .name = u8"three"},
        veyra::FileListEntry{.id = 4, .name = u8"four"},
    };
    const std::array<std::size_t, 4> visible{0, 1, 2, 3};
    auto storage = std::array<std::uint64_t, 4>{};
    auto selection = veyra::FileListSelection(storage);
    require(
        selection.update(entries, visible, 1) == veyra::FileListSelectionResult::complete,
        "selection begins with one file"
    );
    require(
        selection.update(entries, visible, 2, {.toggle = true}) ==
            veyra::FileListSelectionResult::complete,
        "selection adds a second file"
    );

    const std::array refreshed_entries{
        veyra::FileListEntry{.id = 2, .name = u8"two"},
        veyra::FileListEntry{.id = 3, .name = u8"three"},
        veyra::FileListEntry{.id = 5, .name = u8"five"},
    };
    require(
        selection.reconcile(refreshed_entries) == veyra::FileListSelectionResult::complete,
        "refresh retains selected files that still exist"
    );
    require(
        selection.ids().size() == 2 && selection.ids()[0] == 2 && selection.ids()[1] == 3,
        "refresh removes selection IDs for deleted files"
    );
    require(selection.anchor_id() == 3, "refresh keeps an anchor that still exists");

    const std::array invalid_order{
        veyra::FileListEntry{.id = 5, .name = u8"five"},
        veyra::FileListEntry{.id = 2, .name = u8"two"},
    };
    require(
        selection.reconcile(invalid_order) == veyra::FileListSelectionResult::invalid_order,
        "reconciliation rejects unsorted IDs"
    );
    require(
        selection.ids().size() == 2 && selection.ids()[0] == 2 && selection.ids()[1] == 3 &&
            selection.anchor_id() == 3,
        "invalid refresh order preserves selection and anchor"
    );

    const std::array deleted_anchor_entries{
        veyra::FileListEntry{.id = 2, .name = u8"two"},
        veyra::FileListEntry{.id = 5, .name = u8"five"},
    };
    require(
        selection.reconcile(deleted_anchor_entries) == veyra::FileListSelectionResult::complete,
        "a second refresh reconciles the remaining files"
    );
    require(selection.ids().size() == 1 && selection.ids()[0] == 2, "selection keeps its survivor");
    require(!selection.anchor_id(), "refresh clears a deleted anchor");
}

void check_select_visible_entries() {
    const std::array entries{
        veyra::FileListEntry{.id = 20, .name = u8"twenty"},
        veyra::FileListEntry{.id = 30, .name = u8"thirty"},
        veyra::FileListEntry{.id = 10, .name = u8"ten"},
    };
    const std::array<std::size_t, 3> visible{2, 0, 1};
    auto storage = std::array<std::uint64_t, 3>{};
    auto selection = veyra::FileListSelection(storage);
    require(
        selection.select_visible(entries, visible) == veyra::FileListSelectionResult::complete,
        "visible entries can be selected together"
    );
    require(
        selection.ids().size() == 3 && selection.ids()[0] == 10 && selection.ids()[1] == 20 &&
            selection.ids()[2] == 30,
        "selecting a view stores sorted IDs"
    );
    require(selection.anchor_id() == 10, "the first visible item becomes the selection anchor");

    const std::array<std::size_t, 3> duplicate_visible{2, 2, 0};
    auto duplicate_storage = std::array<std::uint64_t, 3>{};
    auto duplicate_selection = veyra::FileListSelection(duplicate_storage);
    require(
        duplicate_selection.select_visible(entries, duplicate_visible) ==
            veyra::FileListSelectionResult::complete,
        "duplicate visible indices are accepted"
    );
    require(
        duplicate_selection.ids().size() == 2 && duplicate_selection.ids()[0] == 10 &&
            duplicate_selection.ids()[1] == 20,
        "duplicate visible IDs are removed from the selection"
    );

    const std::array<std::size_t, 2> reverse_visible{1, 0};
    auto reverse_storage = std::array<std::uint64_t, 2>{};
    auto reverse_selection = veyra::FileListSelection(reverse_storage);
    require(
        reverse_selection.select_visible(entries, reverse_visible) ==
            veyra::FileListSelectionResult::complete,
        "descending IDs can be selected"
    );
    require(
        reverse_selection.ids().size() == 2 && reverse_selection.ids()[0] == 20 &&
            reverse_selection.ids()[1] == 30,
        "selection IDs stay sorted when visible order is descending"
    );
    require(reverse_selection.anchor_id() == 30, "reverse order keeps its visible anchor");

    const std::array<std::size_t, 2> filtered_visible{2, 1};
    require(
        selection.select_visible(entries, filtered_visible) ==
            veyra::FileListSelectionResult::complete,
        "filtered views can select only their visible entries"
    );
    require(
        selection.ids().size() == 2 && selection.ids()[0] == 10 && selection.ids()[1] == 30,
        "selecting a filtered view replaces the previous selection"
    );

    const std::array<std::size_t, 2> invalid_order{0, 9};
    require(
        selection.select_visible(entries, invalid_order) ==
            veyra::FileListSelectionResult::invalid_order,
        "selection rejects an invalid visible order"
    );
    require(
        selection.ids().size() == 2 && selection.ids()[0] == 10 && selection.ids()[1] == 30 &&
            selection.anchor_id() == 10,
        "invalid order preserves state"
    );

    auto small_storage = std::array<std::uint64_t, 1>{};
    auto small_selection = veyra::FileListSelection(small_storage);
    require(
        small_selection.update(entries, visible, 0) == veyra::FileListSelectionResult::complete,
        "small selection starts with one item"
    );
    require(
        small_selection.select_visible(entries, visible) ==
            veyra::FileListSelectionResult::insufficient_storage,
        "selection reports insufficient storage before replacing its contents"
    );
    require(
        small_selection.ids().size() == 1 && small_selection.ids()[0] == 10 &&
            small_selection.anchor_id() == 10,
        "capacity failure preserves the current selection"
    );

    require(
        selection.select_visible(entries, std::span<const std::size_t>{}) ==
            veyra::FileListSelectionResult::complete,
        "an empty view clears selection successfully"
    );
    require(selection.ids().empty() && !selection.anchor_id(), "empty view clears its anchor");

    auto empty_selection = veyra::FileListSelection(std::span<std::uint64_t>{});
    require(
        empty_selection.select_visible(entries, std::span<const std::size_t>{}) ==
            veyra::FileListSelectionResult::complete,
        "an empty view needs no selection storage"
    );
}

}

int main() {
    try {
        check_directory_grouping_and_stable_sort();
        check_order_for_sorted_entries();
        check_filter_and_sort_direction();
        check_utf8_name_filter();
        check_row_ids_survive_refreshes();
        check_row_id_reconciliation_failures_preserve_output();
        check_capacity_failure_preserves_output();
        check_selection_follows_ids_across_views();
        check_selection_failures_preserve_state();
        check_selection_reconciles_refreshed_entries();
        check_select_visible_entries();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
