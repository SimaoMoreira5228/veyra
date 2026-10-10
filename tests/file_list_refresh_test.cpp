#include "veyra/files/file_list.hpp"
#include "veyra/files/file_thumbnail_queue.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <optional>
#include <span>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <string_view>
#include <vector>

namespace {

struct SyntheticFile {
    const char8_t* name{};
    std::uint64_t size_bytes{};
    std::int64_t modified_nanoseconds{};
    bool is_directory{};
};

constexpr SyntheticFile kPreviousFiles[] = {
    {u8"alpha.png", 120, 20, false},
    {u8"assets", 0, 10, true},
    {u8"beta.txt", 40, 30, false},
    {u8"delta.png", 300, 40, false},
    {u8"docs", 0, 50, true},
    {u8"epsilon.txt", 55, 60, false},
    {u8"gamma.png", 200, 70, false},
    {u8"zeta.txt", 10, 80, false},
};

constexpr SyntheticFile kRefreshedFiles[] = {
    {u8"alpha.png", 120, 20, false},
    {u8"assets", 0, 10, true},
    {u8"delta.png", 300, 40, false},
    {u8"docs", 0, 50, true},
    {u8"epsilon.txt", 55, 60, false},
    {u8"eta.png", 150, 90, false},
    {u8"gamma-wide.png", 200, 70, false},
    {u8"zeta.txt", 10, 80, false},
};

struct RefreshContext {
    std::vector<std::u8string> previous_names;
    std::vector<veyra::FileListEntry> previous_entries;
    std::vector<std::u8string> refreshed_names;
    std::vector<veyra::FileListEntry> refreshed_entries;
    std::uint64_t next_id{1};
    std::array<std::uint64_t, 8> selection_storage{};
    veyra::FileListSelection selection{selection_storage};
    std::array<veyra::FileThumbnailRequest, 8> thumbnail_storage{};
    veyra::FileThumbnailQueue thumbnails{thumbnail_storage};
    std::vector<std::size_t> previous_order;
    std::vector<std::size_t> visible_order;
    std::size_t visible_count{};
    std::uint64_t first_generation{};
    std::uint64_t second_generation{};
    std::optional<veyra::FileThumbnailRequest> in_flight;
};

void require(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

void append_entry(
    std::vector<std::u8string>& names,
    std::vector<veyra::FileListEntry>& entries,
    const SyntheticFile& file,
    std::uint64_t id
) {
    names.emplace_back(file.name);
    entries.push_back(
        veyra::FileListEntry{
            .id = id,
            .name = names.back(),
            .size_bytes = file.size_bytes,
            .modified_nanoseconds = file.modified_nanoseconds,
            .is_directory = file.is_directory,
        }
    );
}

veyra::FileThumbnailKey thumbnail_key(const veyra::FileListEntry& entry) {
    return veyra::FileThumbnailKey{
        .file_id = veyra::FileId{1, entry.id},
        .size_bytes = entry.size_bytes,
        .modified_nanoseconds = entry.modified_nanoseconds,
    };
}

std::filesystem::path thumbnail_path(std::u8string_view name) {
    const auto* bytes = reinterpret_cast<const char*>(name.data());

    return std::filesystem::path("/thumbs") / std::string(bytes, name.size());
}

void seed_previous(RefreshContext& context) {
    context.previous_names.reserve(8);
    context.previous_entries.reserve(8);

    auto id = std::uint64_t{1};
    for (const auto& file : kPreviousFiles) {
        append_entry(context.previous_names, context.previous_entries, file, id);
        ++id;
    }
    context.next_id = id;
}

void select_previous_rows(RefreshContext& context) {
    context.previous_order.assign(context.previous_entries.size(), 0);

    auto built_count = std::size_t{0};
    require(
        veyra::build_file_list_order(
            context.previous_entries,
            {},
            {},
            context.previous_order,
            built_count
        ) == veyra::FileListResult::complete,
        "previous view builds before the refresh"
    );
    require(built_count == context.previous_entries.size(), "previous view shows every entry");
    const std::array<std::size_t, 8> expected_previous{1, 4, 0, 2, 3, 5, 6, 7};
    require(
        std::equal(
            context.previous_order.begin(),
            context.previous_order.end(),
            expected_previous.begin()
        ),
        "previous view groups directories first"
    );

    const auto visible = std::span<const std::size_t>{context.previous_order};
    require(
        context.selection.update(context.previous_entries, visible, 2) ==
            veyra::FileListSelectionResult::complete,
        "plain click selects the first file"
    );
    require(
        context.selection.update(context.previous_entries, visible, 6, {.toggle = true}) ==
            veyra::FileListSelectionResult::complete,
        "control click adds a row that the refresh renames"
    );
    require(
        context.selection.update(context.previous_entries, visible, 4, {.toggle = true}) ==
            veyra::FileListSelectionResult::complete,
        "control click anchors a row that survives the refresh"
    );
    require(context.selection.ids().size() == 3, "three rows stay selected before the refresh");
}

void queue_previous_thumbnails(RefreshContext& context) {
    context.first_generation = context.thumbnails.reset();
    require(context.thumbnails.pending() == 0, "generation reset starts with an empty queue");

    auto priority = std::uint32_t{0};
    for (const auto& entry : context.previous_entries) {
        require(
            context.thumbnails.submit(thumbnail_key(entry), thumbnail_path(entry.name), priority) ==
                veyra::FileThumbnailQueueResult::queued,
            "previous rows enter the thumbnail queue"
        );
        ++priority;
    }
    require(
        context.thumbnails.pending() == context.previous_entries.size(),
        "every previous row has queued thumbnail work"
    );

    require(context.thumbnails.pending() != 0, "decoder work waits before the refresh");
    context.in_flight = context.thumbnails.wait_next({});
    require(
        context.in_flight && context.in_flight->key.file_id == veyra::FileId{1, 1},
        "decoder takes the most visible row first"
    );
    require(
        context.in_flight->generation == context.first_generation,
        "queued work carries its view generation"
    );
}

void refresh_directory(RefreshContext& context) {
    context.refreshed_names.reserve(8);
    context.refreshed_entries.reserve(8);
    for (const auto& file : kRefreshedFiles) {
        append_entry(context.refreshed_names, context.refreshed_entries, file, 0);
    }

    require(
        veyra::reconcile_file_list_row_ids(
            context.previous_entries,
            context.refreshed_entries,
            context.next_id
        ) == veyra::FileListRowIdResult::complete,
        "refresh reconciles survivor row IDs"
    );

    const std::array<std::uint64_t, 8> expected_ids{1, 2, 4, 5, 6, 9, 10, 8};
    for (std::size_t index = 0; index < expected_ids.size(); ++index) {
        require(
            context.refreshed_entries[index].id == expected_ids[index],
            "survivors keep row IDs"
        );
    }
    for (const auto& entry : context.refreshed_entries) {
        require(entry.id != 3 && entry.id != 7, "removed rows leave no IDs behind");
    }
    require(context.next_id == 11, "refresh advances past the new rows");
}

void rebuild_visible_order(RefreshContext& context) {
    context.visible_order.assign(context.refreshed_entries.size(), 0);

    require(
        veyra::build_file_list_order(
            context.refreshed_entries,
            u8".png",
            {},
            context.visible_order,
            context.visible_count
        ) == veyra::FileListResult::complete,
        "filtered view rebuilds after the refresh"
    );

    const std::array<std::size_t, 4> expected_visible{0, 2, 5, 6};
    require(context.visible_count == expected_visible.size(), "filter keeps only matching rows");
    for (std::size_t position = 0; position < context.visible_count; ++position) {
        require(
            context.visible_order[position] == expected_visible[position],
            "filtered rows sort by name"
        );
    }

    auto size_order = std::vector<std::size_t>(context.refreshed_entries.size(), 0);
    auto size_count = std::size_t{0};
    require(
        veyra::build_file_list_order(
            context.refreshed_entries,
            {},
            {
                .key = veyra::FileListSortKey::size,
                .direction = veyra::FileListSortDirection::descending,
                .directories_first = false,
            },
            size_order,
            size_count
        ) == veyra::FileListResult::complete,
        "size view rebuilds after the refresh"
    );

    const std::array<std::size_t, 8> expected_by_size{2, 6, 5, 0, 4, 7, 1, 3};
    require(size_count == expected_by_size.size(), "size view shows every refreshed row");
    require(
        std::equal(size_order.begin(), size_order.end(), expected_by_size.begin()),
        "size view follows refreshed sizes"
    );
}

std::u8string_view refreshed_name(const RefreshContext& context, std::uint64_t id) {
    for (const auto& entry : context.refreshed_entries) {
        if (entry.id == id) {
            return entry.name;
        }
    }

    throw std::runtime_error("selected row id has no refreshed entry");
}

void reconcile_selection(RefreshContext& context) {
    auto by_id = context.refreshed_entries;
    std::sort(by_id.begin(), by_id.end(), [](const auto& left, const auto& right) {
        return left.id < right.id;
    });

    require(
        context.selection.reconcile(by_id) == veyra::FileListSelectionResult::complete,
        "selection reconciles against refreshed rows"
    );
    require(context.selection.ids().size() == 2, "selection drops the renamed row");
    require(
        context.selection.contains(1) && context.selection.contains(4),
        "selection holds survivors"
    );
    require(!context.selection.contains(7), "renamed row loses its old selection");
    require(context.selection.anchor_id() == 4, "anchor survives on its refreshed row");
    require(
        refreshed_name(context, 1).compare(u8"alpha.png") == 0,
        "survivor selection still names its file"
    );
    require(
        refreshed_name(context, 4).compare(u8"delta.png") == 0,
        "second survivor still names its file"
    );

    const auto visible = std::span<const std::size_t>{
        context.visible_order.data(),
        context.visible_count,
    };
    require(visible[0] == 0 && visible[1] == 2, "filtered view leads with the selected images");
    require(
        context.refreshed_entries[visible[0]].id == 1 &&
            context.refreshed_entries[visible[1]].id == 4,
        "visible order and selection agree on the same rows"
    );
}

void fence_thumbnails_across_refresh(RefreshContext& context) {
    require(context.thumbnails.cancel(veyra::FileId{1, 3}), "deleted file leaves the queue");
    require(
        context.thumbnails.cancel(veyra::FileId{1, 7}),
        "renamed file leaves under its old identity"
    );
    require(!context.thumbnails.cancel(veyra::FileId{1, 77}), "absent work reports no change");
    require(context.thumbnails.pending() == 5, "survivor work stays queued");

    context.second_generation = context.thumbnails.reset();
    require(
        context.second_generation != context.first_generation,
        "refresh advances the generation"
    );
    require(context.thumbnails.pending() == 0, "reset drops work queued for the old view");
    require(
        !context.thumbnails.is_current(context.first_generation),
        "old view is no longer current"
    );
    require(
        context.in_flight && !context.thumbnails.is_current(context.in_flight->generation),
        "in-flight decode from the old view stays fenced"
    );

    auto priority = std::uint32_t{0};
    for (std::size_t position = 0; position < context.visible_count; ++position) {
        const auto& entry = context.refreshed_entries[context.visible_order[position]];
        require(
            context.thumbnails.submit(thumbnail_key(entry), thumbnail_path(entry.name), priority) ==
                veyra::FileThumbnailQueueResult::queued,
            "refreshed visible rows requeue"
        );
        ++priority;
    }
    require(
        context.thumbnails.pending() == context.visible_count,
        "visible window fills the new view"
    );

    const std::array<std::uint64_t, 4> expected_pops{1, 4, 9, 10};
    for (const auto expected_id : expected_pops) {
        require(context.thumbnails.pending() != 0, "new view has work to hand out");

        const auto request = context.thumbnails.wait_next({});
        require(request.has_value(), "queued thumbnail work can be taken");
        require(
            request->key.file_id == veyra::FileId{1, expected_id},
            "visible rows decode in priority order"
        );
        require(
            request->generation == context.second_generation,
            "new work carries the new generation"
        );
    }
    require(context.thumbnails.pending() == 0, "drained view holds no pending work");

    auto stop_source = std::stop_source{};
    stop_source.request_stop();
    require(
        !context.thumbnails.wait_next(stop_source.get_token()),
        "stopped worker leaves an empty queue alone"
    );
}

}

int main() {
    try {
        auto context = RefreshContext{};
        seed_previous(context);
        select_previous_rows(context);
        queue_previous_thumbnails(context);
        refresh_directory(context);
        rebuild_visible_order(context);
        reconcile_selection(context);
        fence_thumbnails_across_refresh(context);
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
