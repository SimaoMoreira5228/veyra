#include "allocation_counter.hpp"
#include "veyra/chat/message_change_queue.hpp"
#include "veyra/chat/row_height_cache.hpp"
#include "veyra/files/file_list.hpp"
#include "veyra/layout/scroll/virtual_list.hpp"
#ifdef VEYRA_AUDIT_FILESYSTEM
#include "veyra/files/file_thumbnail_queue.hpp"
#endif

#include <array>
#include <atomic>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <new>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>
#ifdef VEYRA_AUDIT_FILESYSTEM
#include <filesystem>
#include <stop_token>
#endif

namespace {

void require(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

std::uint64_t mix_hash(std::uint64_t hash, std::uint64_t value) noexcept {
    hash ^= value;
    hash *= 1099511628211ULL;
    return hash;
}

std::uint64_t hash_bytes(std::uint64_t hash, std::string_view bytes) noexcept {
    for (const auto byte : bytes) {
        hash ^= static_cast<std::uint64_t>(static_cast<unsigned char>(byte));
        hash *= 1099511628211ULL;
    }
    return hash;
}

constexpr auto kChatWidth = 640.0F;
constexpr auto kChatGeneration = std::uint64_t{1};
constexpr auto kViewport = 200.0;
constexpr auto kScrollGeneration = std::uint64_t{7};

struct ChatModel {
    std::vector<std::uint64_t> ids;
    std::vector<float> extents;
    std::vector<double> fenwick;
    veyra::VirtualListIndex list;
};

struct RowLookup {
    std::span<const std::uint64_t> ids;
};

bool find_chat_row(void* context, std::uint64_t message_id, std::size_t& item_index) noexcept {
    const auto& ids = static_cast<RowLookup*>(context)->ids;
    for (std::size_t index = 0; index < ids.size(); ++index) {
        if (ids[index] == message_id) {
            item_index = index;
            return true;
        }
    }
    return false;
}

float chat_height(std::uint64_t message_id) {
    const auto band = static_cast<float>(message_id % 5) * 8.0F;
    return 16.0F + band + 2.0F;
}

veyra::ChatRowHeightKey chat_key(std::uint64_t message_id) {
    return veyra::ChatRowHeightKey{
        .message_id = message_id,
        .available_width = kChatWidth,
        .style_generation = kChatGeneration,
    };
}

std::uint64_t chat_checksum(const ChatModel& model) noexcept {
    auto hash = std::uint64_t{14695981039346656037ULL};
    for (const auto extent : model.extents) {
        hash = mix_hash(hash, std::bit_cast<std::uint32_t>(extent));
    }
    return mix_hash(hash, std::bit_cast<std::uint64_t>(model.list.total_extent()));
}

std::size_t find_chat_index(const ChatModel& model, std::uint64_t message_id) {
    for (std::size_t index = 0; index < model.ids.size(); ++index) {
        if (model.ids[index] == message_id) {
            return index;
        }
    }
    return model.ids.size();
}

void apply_chat_edit(ChatModel& model, veyra::ChatRowHeightCache& cache, std::uint64_t id) {
    const auto row = find_chat_index(model, id);
    require(row != model.ids.size(), "steady edit resolves to a seeded row");
    const auto cleared = cache.invalidate_message(id);
    require(cleared == 1, "steady edit clears its own cached height");
    const auto height = chat_height(id) + 12.0F;
    const auto stored = cache.insert(chat_key(id), height);
    require(
        stored == veyra::ChatRowHeightCacheResult::inserted ||
            stored == veyra::ChatRowHeightCacheResult::updated,
        "steady edit height stays cached"
    );
    model.extents[row] = height;
    auto delta = 0.0;
    require(model.list.set_extent(row, height, delta), "steady edit moves its extent");
}

std::uint64_t run_chat_steady_cycle(
    ChatModel& model,
    veyra::ChatRowHeightCache& cache,
    veyra::ChatMessageChangeQueue& queue
) {
    constexpr std::uint64_t kEdited[] = {7, 14, 21, 28, 35, 42, 49, 56, 63};
    for (const auto id : kEdited) {
        const auto pushed = queue.try_push({veyra::ChatMessageChangeKind::edited, id});
        require(pushed == veyra::ChatMessageChangeQueueResult::queued, "steady edit is queued");
    }

    auto batch = std::array<veyra::ChatMessageChange, 64>{};
    auto batch_count = queue.try_pop_batch(batch);
    while (batch_count != 0) {
        for (std::size_t index = 0; index < batch_count; ++index) {
            apply_chat_edit(model, cache, batch[index].message_id);
        }
        batch_count = queue.try_pop_batch(batch);
    }
    require(queue.try_pop_batch(batch) == 0, "steady cycle drains the queue");

    auto measured = 0.0F;
    for (const auto id : kEdited) {
        require(
            cache.find(chat_key(id), measured) == veyra::ChatRowHeightCacheResult::hit,
            "steady edit keeps its new height"
        );
        require(measured == chat_height(id) + 12.0F, "steady edit height is exact");
    }
    require(
        cache.find(chat_key(3), measured) == veyra::ChatRowHeightCacheResult::hit,
        "untouched row survives the steady cycle"
    );

    auto offset = 0.0;
    require(model.list.offset_of(9, offset), "steady cycle reads an offset");
    auto range = veyra::VirtualListRange{};
    require(model.list.visible_range(0.0, kViewport, 2, range), "steady cycle reads a range");
    require(range.first_item == 0, "steady range starts at its first row");

    auto lookup = RowLookup{model.ids};
    auto anchor = veyra::VirtualListAnchor{};
    require(
        model.list.capture_anchor(model.ids, offset, kViewport, kScrollGeneration, anchor),
        "steady cycle captures an anchor"
    );
    auto restored = 0.0;
    require(
        model.list
            .restore_anchor(anchor, kViewport, kScrollGeneration, find_chat_row, &lookup, restored),
        "steady cycle restores its anchor"
    );
    return chat_checksum(model);
}

void check_chat_steady_repeat_allocates_nothing() {
    auto cache_slots = std::vector<veyra::ChatRowHeightCacheSlot>(512);
    auto cache = veyra::ChatRowHeightCache(cache_slots);
    auto queue_storage = std::vector<veyra::ChatMessageChange>(512);
    auto queue = veyra::ChatMessageChangeQueue(queue_storage);
    auto model = ChatModel{};
    require(cache.valid() && queue.valid(), "chat audit workspace starts valid");

    model.ids.reserve(64);
    model.extents.reserve(64);
    for (std::uint64_t id = 1; id <= 64; ++id) {
        const auto inserted = cache.insert(chat_key(id), chat_height(id));
        require(inserted == veyra::ChatRowHeightCacheResult::inserted, "seeded height is cached");
        model.ids.push_back(id);
        model.extents.push_back(chat_height(id));
    }
    model.fenwick.assign(model.extents.size() + 1, 0.0);
    require(model.list.reset(model.extents, model.fenwick), "seeded rows build the list");

    const auto baseline = run_chat_steady_cycle(model, cache, queue);

    auto allocations = std::size_t{0};
    {
        auto measure = Measure{};
        for (auto repeat = 0; repeat < 64; ++repeat) {
            const auto checksum = run_chat_steady_cycle(model, cache, queue);
            require(checksum == baseline, "steady chat repeat keeps its checksum");
        }
        allocations = measure.count();
    }
    require(allocations == 0, "steady chat repeats allocate nothing");
    require(model.ids.size() == 64, "steady chat repeat grows no rows");
}

struct AuditFile {
    const char8_t* name{};
    std::uint64_t size_bytes{};
    std::int64_t modified_nanoseconds{};
    bool is_directory{};
};

constexpr AuditFile kAuditFiles[] = {
    {u8"alpha.png", 120, 20, false},
    {u8"assets", 0, 10, true},
    {u8"beta.txt", 40, 30, false},
    {u8"delta.png", 300, 40, false},
    {u8"docs", 0, 50, true},
    {u8"epsilon.txt", 55, 60, false},
    {u8"gamma.png", 200, 70, false},
    {u8"zeta.txt", 10, 80, false},
};

struct FileAuditState {
    std::vector<std::u8string> names;
    std::vector<veyra::FileListEntry> entries;
    std::vector<std::u8string> refreshed_names;
    std::vector<veyra::FileListEntry> refreshed_entries;
    std::vector<veyra::FileListEntry> by_id;
    std::uint64_t next_id{9};
    std::array<std::size_t, 8> name_order{};
    std::array<std::size_t, 8> size_order{};
    std::size_t name_count{};
    std::size_t size_count{};
    std::array<std::uint64_t, 8> selection_storage{};
    veyra::FileListSelection selection{selection_storage};
};

void append_audit_file(FileAuditState& state, const AuditFile& file, std::uint64_t id) {
    state.names.emplace_back(file.name);
    state.entries.push_back(
        veyra::FileListEntry{
            .id = id,
            .name = state.names.back(),
            .size_bytes = file.size_bytes,
            .modified_nanoseconds = file.modified_nanoseconds,
            .is_directory = file.is_directory,
        }
    );
}

void append_refreshed_file(FileAuditState& state, const AuditFile& file) {
    state.refreshed_names.emplace_back(file.name);
    state.refreshed_entries.push_back(
        veyra::FileListEntry{
            .id = 0,
            .name = state.refreshed_names.back(),
            .size_bytes = file.size_bytes,
            .modified_nanoseconds = file.modified_nanoseconds,
            .is_directory = file.is_directory,
        }
    );
}

std::uint64_t run_file_steady_cycle(FileAuditState& state) {
    require(
        veyra::build_file_list_order(state.entries, u8"", {}, state.name_order, state.name_count) ==
            veyra::FileListResult::complete,
        "steady order rebuilds"
    );
    require(state.name_count == 8, "steady order shows every row");
    require(
        veyra::build_file_list_order(
            state.entries,
            u8"",
            {
                .key = veyra::FileListSortKey::size,
                .direction = veyra::FileListSortDirection::descending,
                .directories_first = false,
            },
            state.size_order,
            state.size_count
        ) == veyra::FileListResult::complete,
        "steady size order rebuilds"
    );
    require(state.size_count == 8, "steady size order shows every row");

    state.selection.clear();
    const auto visible = std::span<const std::size_t>{state.name_order.data(), state.name_count};
    require(
        state.selection.update(state.entries, visible, 2) ==
            veyra::FileListSelectionResult::complete,
        "steady plain click lands"
    );
    require(
        state.selection.update(state.entries, visible, 5, {.toggle = true}) ==
            veyra::FileListSelectionResult::complete,
        "steady toggle lands"
    );
    require(
        state.selection.update(state.entries, visible, 4, {.toggle = true}) ==
            veyra::FileListSelectionResult::complete,
        "steady second toggle lands"
    );
    require(state.selection.ids().size() == 3, "steady selection holds three rows");

    require(
        veyra::reconcile_file_list_row_ids(state.entries, state.refreshed_entries, state.next_id) ==
            veyra::FileListRowIdResult::complete,
        "steady refresh reconciles survivor IDs"
    );
    require(state.next_id == 9, "steady refresh mints no new IDs");

    require(
        state.selection.reconcile(state.by_id) == veyra::FileListSelectionResult::complete,
        "steady selection reconciles"
    );
    require(state.selection.ids().size() == 3, "steady selection survives the refresh");
    require(
        state.selection.anchor_id().has_value() && *state.selection.anchor_id() == 4,
        "steady anchor survives on its row"
    );

    auto hash = std::uint64_t{14695981039346656037ULL};
    for (const auto position : state.name_order) {
        hash = mix_hash(hash, position);
    }
    for (const auto position : state.size_order) {
        hash = mix_hash(hash, position);
    }
    for (const auto id : state.selection.ids()) {
        hash = mix_hash(hash, id);
    }
    return mix_hash(hash, *state.selection.anchor_id());
}

void check_file_steady_repeat_allocates_nothing() {
    auto state = FileAuditState{};
    state.names.reserve(8);
    state.entries.reserve(8);
    state.refreshed_names.reserve(8);
    state.refreshed_entries.reserve(8);
    auto id = std::uint64_t{1};
    for (const auto& file : kAuditFiles) {
        append_audit_file(state, file, id);
        append_refreshed_file(state, file);
        ++id;
    }
    state.by_id = state.entries;

    const auto baseline = run_file_steady_cycle(state);

    auto allocations = std::size_t{0};
    {
        auto measure = Measure{};
        for (auto repeat = 0; repeat < 200; ++repeat) {
            const auto checksum = run_file_steady_cycle(state);
            require(checksum == baseline, "steady file repeat keeps its order and selection");
        }
        allocations = measure.count();
    }
    require(allocations == 0, "steady file repeats allocate nothing");
}

#ifdef VEYRA_AUDIT_FILESYSTEM
void check_thumbnail_refresh_allocates_nothing() {
    const auto base = std::filesystem::current_path();
    constexpr const char* kThumbnails[] =
        {"thumb-a.png", "thumb-b.png", "thumb-c.png", "thumb-d.png"};
    auto held = std::vector<std::filesystem::path>{};
    held.reserve(4);
    for (const auto* name : kThumbnails) {
        held.push_back(base / name);
    }
    auto keys = std::array<veyra::FileThumbnailKey, 4>{};
    for (std::size_t index = 0; index < keys.size(); ++index) {
        keys[index] = veyra::FileThumbnailKey{
            .file_id = veyra::FileId{1, static_cast<std::uint64_t>(index + 1)},
            .size_bytes = 100 + static_cast<std::uint64_t>(index),
            .modified_nanoseconds = 1000 + static_cast<std::int64_t>(index),
        };
    }
    auto storage = std::array<veyra::FileThumbnailRequest, 4>{};
    auto queue = veyra::FileThumbnailQueue(storage);
    const auto generation = queue.reset();
    const auto token = std::stop_token{};

    auto round = [&queue, &keys, &held, generation, &token]() {
        for (std::size_t index = 0; index < keys.size(); ++index) {
            const auto submitted = queue.submit(
                keys[index],
                std::move(held[index]),
                static_cast<std::uint32_t>(index)
            );
            require(
                submitted == veyra::FileThumbnailQueueResult::queued,
                "steady thumbnail work is queued"
            );
        }
        auto hash = std::uint64_t{14695981039346656037ULL};
        for (std::size_t taken = 0; taken < keys.size(); ++taken) {
            auto request = queue.wait_next(token);
            require(request.has_value(), "steady thumbnail work is handed out");
            require(request->generation == generation, "steady work carries its generation");
            hash = mix_hash(hash, request->key.file_id.object);
            hash = mix_hash(hash, request->priority);
            held[request->key.file_id.object - 1] = std::move(request->path);
        }
        require(queue.pending() == 0, "steady round drains the queue");
        return hash;
    };

    const auto baseline = round();

    auto allocations = std::size_t{0};
    {
        auto measure = Measure{};
        for (auto repeat = 0; repeat < 100; ++repeat) {
            const auto checksum = round();
            require(checksum == baseline, "steady thumbnail repeat keeps its order");
        }
        allocations = measure.count();
    }
    require(allocations == 0, "steady thumbnail repeats allocate nothing");
}
#endif

}

int main() {
    try {
        check_chat_steady_repeat_allocates_nothing();
        check_file_steady_repeat_allocates_nothing();
#ifdef VEYRA_AUDIT_FILESYSTEM
        check_thumbnail_refresh_allocates_nothing();
#endif
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
