#include "veyra/files/file_list.hpp"

#include <algorithm>

namespace veyra {
namespace {

bool valid_sort(FileListSort sort) noexcept {
    const auto valid_key = sort.key == FileListSortKey::name || sort.key == FileListSortKey::size ||
        sort.key == FileListSortKey::modified_time;
    const auto valid_direction = sort.direction == FileListSortDirection::ascending ||
        sort.direction == FileListSortDirection::descending;
    return valid_key && valid_direction;
}

bool matches_name_filter(std::string_view name, std::string_view filter) noexcept {
    if (filter.empty()) {
        return true;
    }
    return name.find(filter) != std::string_view::npos;
}

int compare_entries(
    const FileListEntry& left,
    const FileListEntry& right,
    FileListSortKey key
) noexcept {
    switch (key) {
    case FileListSortKey::name:
        return left.name.compare(right.name);
    case FileListSortKey::size:
        if (left.size_bytes < right.size_bytes) {
            return -1;
        }
        if (left.size_bytes > right.size_bytes) {
            return 1;
        }
        return 0;
    case FileListSortKey::modified_time:
        if (left.modified_nanoseconds < right.modified_nanoseconds) {
            return -1;
        }
        if (left.modified_nanoseconds > right.modified_nanoseconds) {
            return 1;
        }
        return 0;
    }
    return 0;
}

}

FileListResult build_file_list_order(
    std::span<const FileListEntry> entries,
    std::string_view name_filter,
    FileListSort sort,
    std::span<std::size_t> output_indices,
    std::size_t& output_count
) noexcept {
    output_count = 0;
    if (!valid_sort(sort)) {
        return FileListResult::invalid_sort;
    }

    auto matching_count = std::size_t{0};
    for (const auto& entry : entries) {
        if (matches_name_filter(entry.name, name_filter)) {
            ++matching_count;
        }
    }
    output_count = matching_count;
    if (output_indices.size() < matching_count) {
        return FileListResult::insufficient_output;
    }

    auto output_index = std::size_t{0};
    for (std::size_t index = 0; index < entries.size(); ++index) {
        if (matches_name_filter(entries[index].name, name_filter)) {
            output_indices[output_index] = index;
            ++output_index;
        }
    }

    if (matching_count > 1) {
        std::sort(
            output_indices.begin(),
            output_indices.begin() + static_cast<std::ptrdiff_t>(matching_count),
            [&entries, sort](std::size_t left_index, std::size_t right_index) noexcept {
                const auto& left = entries[left_index];
                const auto& right = entries[right_index];
                if (sort.directories_first && left.is_directory != right.is_directory) {
                    return left.is_directory;
                }

                const auto comparison = compare_entries(left, right, sort.key);
                if (comparison != 0) {
                    if (sort.direction == FileListSortDirection::ascending) {
                        return comparison < 0;
                    }
                    return comparison > 0;
                }
                if (left.id != right.id) {
                    return left.id < right.id;
                }
                return left_index < right_index;
            }
        );
    }

    return FileListResult::complete;
}

}
