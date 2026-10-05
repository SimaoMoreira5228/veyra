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

FileListSelection::FileListSelection(std::span<std::uint64_t> storage) noexcept :
    storage_(storage) {
}

FileListSelectionResult FileListSelection::update(
    std::span<const FileListEntry> entries,
    std::span<const std::size_t> visible_indices,
    std::size_t visible_position,
    FileListSelectionModifiers modifiers
) noexcept {
    if (visible_position >= visible_indices.size()) {
        return FileListSelectionResult::item_not_visible;
    }

    const auto clicked_index = visible_indices[visible_position];
    if (clicked_index >= entries.size()) {
        return FileListSelectionResult::invalid_order;
    }
    const auto clicked_id = entries[clicked_index].id;

    if (!modifiers.extend_range) {
        if (storage_.empty()) {
            return FileListSelectionResult::insufficient_storage;
        }
        const auto selected_end = storage_.begin() + static_cast<std::ptrdiff_t>(count_);
        const auto selected = std::lower_bound(storage_.begin(), selected_end, clicked_id);
        if (modifiers.toggle) {
            if (selected != selected_end && *selected == clicked_id) {
                std::move(selected + 1, selected_end, selected);
                --count_;
            } else {
                if (count_ == storage_.size()) {
                    return FileListSelectionResult::insufficient_storage;
                }
                const auto insertion = selected - storage_.begin();
                std::move_backward(
                    selected,
                    selected_end,
                    storage_.begin() + static_cast<std::ptrdiff_t>(count_ + 1)
                );
                storage_[static_cast<std::size_t>(insertion)] = clicked_id;
                ++count_;
            }
        } else {
            storage_[0] = clicked_id;
            count_ = 1;
        }
        anchor_id_ = clicked_id;
        return FileListSelectionResult::complete;
    }

    auto anchor_position = visible_position;
    auto anchor_is_visible = !anchor_id_.has_value();
    if (anchor_id_) {
        for (std::size_t distance = 0; distance < visible_indices.size(); ++distance) {
            if (distance <= visible_position) {
                const auto position = visible_position - distance;
                const auto index = visible_indices[position];
                if (index >= entries.size()) {
                    return FileListSelectionResult::invalid_order;
                }
                if (entries[index].id == *anchor_id_) {
                    anchor_position = position;
                    anchor_is_visible = true;
                    break;
                }
            }

            if (distance > 0 && distance < visible_indices.size() - visible_position) {
                const auto position = visible_position + distance;
                const auto index = visible_indices[position];
                if (index >= entries.size()) {
                    return FileListSelectionResult::invalid_order;
                }
                if (entries[index].id == *anchor_id_) {
                    anchor_position = position;
                    anchor_is_visible = true;
                    break;
                }
            }
        }
    }

    if (!anchor_is_visible) {
        anchor_position = visible_position;
    }

    const auto first_position = std::min(anchor_position, visible_position);
    const auto last_position = std::max(anchor_position, visible_position);
    const auto range_size = last_position - first_position + 1;
    auto additional_count = std::size_t{0};
    const auto existing_ids = ids();
    for (auto position = first_position; position <= last_position; ++position) {
        const auto index = visible_indices[position];
        if (index >= entries.size()) {
            return FileListSelectionResult::invalid_order;
        }
        if (modifiers.toggle && !existing_ids.empty()) {
            const auto id = entries[index].id;
            const auto selected = std::lower_bound(existing_ids.begin(), existing_ids.end(), id);
            if (selected == existing_ids.end() || *selected != id) {
                ++additional_count;
            }
        } else if (modifiers.toggle) {
            ++additional_count;
        }
    }
    const auto needed_count = modifiers.toggle ? additional_count : range_size;
    const auto used_count = modifiers.toggle ? count_ : std::size_t{0};
    if (needed_count > storage_.size() - used_count) {
        return FileListSelectionResult::insufficient_storage;
    }

    if (modifiers.toggle) {
        auto candidate_count = count_;
        for (auto position = first_position; position <= last_position; ++position) {
            const auto id = entries[visible_indices[position]].id;
            if (existing_ids.empty()) {
                storage_[candidate_count] = id;
                ++candidate_count;
                continue;
            }

            const auto selected = std::lower_bound(existing_ids.begin(), existing_ids.end(), id);
            if (selected == existing_ids.end() || *selected != id) {
                storage_[candidate_count] = id;
                ++candidate_count;
            }
        }
        std::sort(
            storage_.begin(),
            storage_.begin() + static_cast<std::ptrdiff_t>(candidate_count)
        );
        count_ = static_cast<std::size_t>(
            std::unique(
                storage_.begin(),
                storage_.begin() + static_cast<std::ptrdiff_t>(candidate_count)
            ) -
            storage_.begin()
        );
    } else {
        auto candidate_count = std::size_t{0};
        for (auto position = first_position; position <= last_position; ++position) {
            storage_[candidate_count] = entries[visible_indices[position]].id;
            ++candidate_count;
        }
        std::sort(
            storage_.begin(),
            storage_.begin() + static_cast<std::ptrdiff_t>(candidate_count)
        );
        count_ = static_cast<std::size_t>(
            std::unique(
                storage_.begin(),
                storage_.begin() + static_cast<std::ptrdiff_t>(candidate_count)
            ) -
            storage_.begin()
        );
    }

    if (!anchor_id_ || !anchor_is_visible) {
        anchor_id_ = clicked_id;
    }
    return FileListSelectionResult::complete;
}

bool FileListSelection::contains(std::uint64_t id) const noexcept {
    if (count_ == 0) {
        return false;
    }
    const auto selected_end = storage_.begin() + static_cast<std::ptrdiff_t>(count_);
    const auto selected = std::lower_bound(storage_.begin(), selected_end, id);
    return selected != selected_end && *selected == id;
}

std::span<const std::uint64_t> FileListSelection::ids() const noexcept {
    return storage_.first(count_);
}

std::optional<std::uint64_t> FileListSelection::anchor_id() const noexcept {
    return anchor_id_;
}

void FileListSelection::clear() noexcept {
    count_ = 0;
    anchor_id_.reset();
}

}
