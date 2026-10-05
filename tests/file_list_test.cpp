#include "veyra/files/file_list.hpp"

#include <array>
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

}

int main() {
    try {
        check_directory_grouping_and_stable_sort();
        check_filter_and_sort_direction();
        check_capacity_failure_preserves_output();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
