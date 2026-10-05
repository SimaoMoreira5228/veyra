#ifndef VEYRA_FILES_FILE_LIST_HPP
#define VEYRA_FILES_FILE_LIST_HPP

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace veyra {

struct FileListEntry {
    std::uint64_t id{};
    std::u8string_view name;
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
    std::u8string_view name_filter,
    FileListSort sort,
    std::span<std::size_t> output_indices,
    std::size_t& output_count
) noexcept;

struct FileListSelectionModifiers {
    bool toggle{};
    bool extend_range{};
};

enum class FileListSelectionResult : std::uint8_t {
    complete,
    item_not_visible,
    invalid_order,
    insufficient_storage,
};

class FileListSelection {
  public:
    explicit FileListSelection(std::span<std::uint64_t> storage) noexcept;

    FileListSelection(const FileListSelection&) = delete;
    FileListSelection& operator=(const FileListSelection&) = delete;
    FileListSelection(FileListSelection&&) = delete;
    FileListSelection& operator=(FileListSelection&&) = delete;

    FileListSelectionResult update(
        std::span<const FileListEntry> entries,
        std::span<const std::size_t> visible_indices,
        std::size_t visible_position,
        FileListSelectionModifiers modifiers = {}
    ) noexcept;

    bool contains(std::uint64_t id) const noexcept;
    std::span<const std::uint64_t> ids() const noexcept;
    std::optional<std::uint64_t> anchor_id() const noexcept;
    void clear() noexcept;

  private:
    std::span<std::uint64_t> storage_;
    std::size_t count_{};
    std::optional<std::uint64_t> anchor_id_;
};

}

#endif
