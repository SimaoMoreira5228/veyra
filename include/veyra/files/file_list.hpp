#ifndef VEYRA_FILES_FILE_LIST_HPP
#define VEYRA_FILES_FILE_LIST_HPP

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace veyra {

struct FileListEntry {
    std::uint64_t id{};
    std::string_view name;
    std::uint64_t size_bytes{};
    std::int64_t modified_nanoseconds{};
    bool is_directory{};
};

enum class FileListSortKey : std::uint8_t {
    name,
    size,
    modified_time,
};

enum class FileListSortDirection : std::uint8_t {
    ascending,
    descending,
};

struct FileListSort {
    FileListSortKey key{FileListSortKey::name};
    FileListSortDirection direction{FileListSortDirection::ascending};
    bool directories_first{true};
};

enum class FileListResult : std::uint8_t {
    complete,
    invalid_sort,
    insufficient_output,
};

FileListResult build_file_list_order(
    std::span<const FileListEntry> entries,
    std::string_view name_filter,
    FileListSort sort,
    std::span<std::size_t> output_indices,
    std::size_t& output_count
) noexcept;

}

#endif
