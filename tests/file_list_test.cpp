#include "veyra/files/file_list.hpp"

#include <array>
#include <iostream>
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
            .name = "zeta.txt",
            .size_bytes = 100,
            .modified_nanoseconds = 30,
        },
        veyra::FileListEntry{
            .id = 4,
            .name = "folder",
            .is_directory = true,
        },
        veyra::FileListEntry{
            .id = 2,
            .name = "alpha.txt",
            .size_bytes = 10,
            .modified_nanoseconds = 20,
        },
        veyra::FileListEntry{
            .id = 1,
            .name = "alpha",
            .size_bytes = 10,
            .modified_nanoseconds = 40,
            .is_directory = true,
        },
        veyra::FileListEntry{
            .id = 3,
            .name = "beta.txt",
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

void check_filter_and_sort_direction() {
    const std::array entries{
        veyra::FileListEntry{
            .id = 5,
            .name = "zeta.txt",
            .size_bytes = 100,
        },
        veyra::FileListEntry{
            .id = 2,
            .name = "alpha.txt",
            .size_bytes = 10,
        },
        veyra::FileListEntry{
            .id = 3,
            .name = "beta.txt",
            .size_bytes = 10,
        },
        veyra::FileListEntry{
            .id = 4,
            .name = "notes.md",
            .modified_nanoseconds = -5,
        },
    };
    std::array<std::size_t, 4> order{};
    auto count = std::size_t{0};

    require(
        veyra::build_file_list_order(
            entries,
            ".txt",
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
        veyra::build_file_list_order(entries, ".TXT", {}, order, count) ==
            veyra::FileListResult::complete,
        "case-sensitive filter with no matches succeeds"
    );
    require(count == 0, "case-sensitive filter can return no entries");
}

void check_capacity_failure_preserves_output() {
    const std::array entries{
        veyra::FileListEntry{.id = 1, .name = "one"},
        veyra::FileListEntry{.id = 2, .name = "two"},
        veyra::FileListEntry{.id = 3, .name = "three"},
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
        veyra::FileListEntry{.id = 11, .name = "delta"},
        veyra::FileListEntry{.id = 22, .name = "alpha"},
        veyra::FileListEntry{.id = 33, .name = "charlie"},
        veyra::FileListEntry{.id = 44, .name = "bravo"},
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
        veyra::build_file_list_order(entries, "delta", {}, visible, visible_count) ==
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
        veyra::FileListEntry{.id = 1, .name = "one"},
        veyra::FileListEntry{.id = 2, .name = "two"},
        veyra::FileListEntry{.id = 3, .name = "three"},
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

}

int main() {
    try {
        check_directory_grouping_and_stable_sort();
        check_filter_and_sort_direction();
        check_capacity_failure_preserves_output();
        check_selection_follows_ids_across_views();
        check_selection_failures_preserve_state();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
