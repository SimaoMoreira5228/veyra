#include "veyra/chat/message_change_queue.hpp"
#include "veyra/chat/row_height_cache.hpp"
#include "veyra/layout/scroll/virtual_list.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

constexpr auto kBurstWidth = 640.0F;
constexpr auto kBurstGeneration = std::uint64_t{1};
constexpr auto kNarrowWidth = 480.0F;
constexpr auto kNextGeneration = std::uint64_t{2};
constexpr auto kViewport = 200.0;
constexpr auto kScrollGeneration = std::uint64_t{7};

void require(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

bool near(double actual, double expected) {
    return std::abs(actual - expected) < 0.001;
}

struct ChatListModel {
    std::vector<std::uint64_t> ids;
    std::vector<float> extents;
    std::vector<double> fenwick;
    veyra::VirtualListIndex list;
};

struct RowLookupContext {
    std::span<const std::uint64_t> ids;
};

bool find_row_index(void* context, std::uint64_t message_id, std::size_t& item_index) noexcept {
    const auto& ids = static_cast<RowLookupContext*>(context)->ids;
    for (std::size_t index = 0; index < ids.size(); ++index) {
        if (ids[index] == message_id) {
            item_index = index;
            return true;
        }
    }
    return false;
}

float message_height(
    std::uint64_t message_id,
    float available_width,
    std::uint64_t style_generation
) {
    const auto band = message_id % 5;
    const auto base = 16.0F + static_cast<float>(band * 8);
    auto width_delta = 0.0F;
    if (available_width < 600.0F) {
        width_delta = 8.0F;
    }
    return base + width_delta + static_cast<float>(style_generation * 2);
}

veyra::ChatRowHeightKey
height_key(std::uint64_t message_id, float available_width, std::uint64_t style_generation) {
    return veyra::ChatRowHeightKey{
        .message_id = message_id,
        .available_width = available_width,
        .style_generation = style_generation,
    };
}

bool rebuild_index(ChatListModel& model) {
    model.fenwick.assign(model.extents.size() + 1, 0.0);
    return model.list.reset(model.extents, model.fenwick);
}

double total_of(const std::vector<float>& extents) {
    auto total = 0.0;
    for (const auto extent : extents) {
        total += static_cast<double>(extent);
    }
    return total;
}

std::size_t find_row(const ChatListModel& model, std::uint64_t message_id) {
    for (std::size_t index = 0; index < model.ids.size(); ++index) {
        if (model.ids[index] == message_id) {
            return index;
        }
    }
    return model.ids.size();
}

void push_change(
    veyra::ChatMessageChangeQueue& queue,
    veyra::ChatMessageChangeKind kind,
    std::uint64_t message_id
) {
    const auto result = queue.try_push({kind, message_id});
    require(result == veyra::ChatMessageChangeQueueResult::queued, "burst change enters the queue");
}

void seed_messages(
    ChatListModel& model,
    veyra::ChatRowHeightCache& cache,
    std::uint64_t first_id,
    std::size_t count,
    float available_width,
    std::uint64_t style_generation
) {
    for (std::size_t step = 0; step < count; ++step) {
        const auto message_id = first_id + static_cast<std::uint64_t>(step);
        const auto height = message_height(message_id, available_width, style_generation);
        const auto inserted =
            cache.insert(height_key(message_id, available_width, style_generation), height);
        require(inserted == veyra::ChatRowHeightCacheResult::inserted, "seeded height is cached");
        model.ids.push_back(message_id);
        model.extents.push_back(height);
    }
    require(rebuild_index(model), "seeded rows initialize the virtual list");
}

void prepend_messages(
    ChatListModel& model,
    veyra::ChatRowHeightCache& cache,
    std::uint64_t first_id,
    std::size_t count,
    float available_width,
    std::uint64_t style_generation
) {
    auto fresh_ids = std::vector<std::uint64_t>{};
    auto fresh_extents = std::vector<float>{};
    for (std::size_t step = 0; step < count; ++step) {
        const auto message_id = first_id + static_cast<std::uint64_t>(step);
        const auto height = message_height(message_id, available_width, style_generation);
        const auto inserted =
            cache.insert(height_key(message_id, available_width, style_generation), height);
        require(inserted == veyra::ChatRowHeightCacheResult::inserted, "history height is cached");
        fresh_ids.push_back(message_id);
        fresh_extents.push_back(height);
    }
    model.ids.insert(model.ids.begin(), fresh_ids.begin(), fresh_ids.end());
    model.extents.insert(model.extents.begin(), fresh_extents.begin(), fresh_extents.end());
    require(rebuild_index(model), "prepended history rebuilds the virtual list");
}

void apply_burst_change(
    ChatListModel& model,
    veyra::ChatRowHeightCache& cache,
    veyra::ChatMessageChange change,
    float available_width,
    std::uint64_t style_generation
) {
    if (change.kind == veyra::ChatMessageChangeKind::appended) {
        const auto height = message_height(change.message_id, available_width, style_generation);
        const auto inserted =
            cache.insert(height_key(change.message_id, available_width, style_generation), height);
        require(inserted == veyra::ChatRowHeightCacheResult::inserted, "appended height is cached");
        model.ids.push_back(change.message_id);
        model.extents.push_back(height);
        return;
    }
    const auto row = find_row(model, change.message_id);
    require(row != model.ids.size(), "queued change resolves to a known row");
    const auto cleared = cache.invalidate_message(change.message_id);
    require(cleared <= 1, "change clears at most its own stale height");
    if (change.kind == veyra::ChatMessageChangeKind::edited) {
        const auto height =
            message_height(change.message_id, available_width, style_generation) + 12.0F;
        const auto inserted =
            cache.insert(height_key(change.message_id, available_width, style_generation), height);
        require(inserted == veyra::ChatRowHeightCacheResult::inserted, "edited height is cached");
        model.extents[row] = height;
        return;
    }
    require(
        change.kind == veyra::ChatMessageChangeKind::deleted,
        "queued change is a known burst kind"
    );
    model.extents[row] = 0.0F;
}

void check_burst_absorption() {
    auto cache_slots = std::vector<veyra::ChatRowHeightCacheSlot>(2048);
    auto cache = veyra::ChatRowHeightCache(cache_slots);
    auto queue_storage = std::vector<veyra::ChatMessageChange>(2048);
    auto queue = veyra::ChatMessageChangeQueue(queue_storage);
    auto model = ChatListModel{};
    require(cache.valid() && queue.valid(), "burst workspace starts valid");

    seed_messages(model, cache, 1, 200, kBurstWidth, kBurstGeneration);

    auto edited_ids = std::vector<std::uint64_t>{};
    for (std::uint64_t message_id = 1; message_id <= 200; ++message_id) {
        if (message_id % 7 == 0) {
            edited_ids.push_back(message_id);
        }
    }
    auto deleted_ids = std::vector<std::uint64_t>{};
    for (std::uint64_t message_id = 1; message_id <= 200; ++message_id) {
        if (message_id % 11 == 0 && message_id % 7 != 0) {
            deleted_ids.push_back(message_id);
        }
    }

    auto anchor_scroll = 0.0;
    require(model.list.offset_of(149, anchor_scroll), "anchor row offset is available");
    anchor_scroll += 3.0;
    auto anchor = veyra::VirtualListAnchor{};
    require(
        model.list.capture_anchor(model.ids, anchor_scroll, kViewport, kScrollGeneration, anchor),
        "reader anchor captures before the burst"
    );
    require(anchor.item_id == 150, "reader anchor names the visible message");

    const auto bottom_scroll = model.list.total_extent() - kViewport;
    auto pinned = veyra::VirtualListAnchor{};
    require(
        model.list.capture_anchor(model.ids, bottom_scroll, kViewport, kScrollGeneration, pinned),
        "pinned anchor captures before the burst"
    );
    require(pinned.pinned_to_end, "bottom scroll position pins to the end");

    auto edit_cursor = std::size_t{0};
    auto delete_cursor = std::size_t{0};
    for (std::uint64_t next_id = 201; next_id <= 1200; ++next_id) {
        push_change(queue, veyra::ChatMessageChangeKind::appended, next_id);
        if (next_id % 25 == 0 && edit_cursor < edited_ids.size()) {
            push_change(queue, veyra::ChatMessageChangeKind::edited, edited_ids[edit_cursor]);
            ++edit_cursor;
        }
        if (next_id % 40 == 0 && delete_cursor < deleted_ids.size()) {
            push_change(queue, veyra::ChatMessageChangeKind::deleted, deleted_ids[delete_cursor]);
            ++delete_cursor;
        }
    }
    require(edit_cursor == edited_ids.size(), "every queued edit entered the burst");
    require(delete_cursor == deleted_ids.size(), "every queued deletion entered the burst");

    auto batch = std::array<veyra::ChatMessageChange, 64>{};
    auto batch_count = queue.try_pop_batch(batch);
    while (batch_count != 0) {
        for (std::size_t index = 0; index < batch_count; ++index) {
            apply_burst_change(model, cache, batch[index], kBurstWidth, kBurstGeneration);
        }
        batch_count = queue.try_pop_batch(batch);
    }
    require(queue.try_pop_batch(batch) == 0, "burst drain empties the queue");
    require(model.ids.size() == 1200, "burst appends all land in the model");
    require(rebuild_index(model), "drained burst rebuilds the virtual list");

    require(
        near(model.list.total_extent(), total_of(model.extents)),
        "list total matches the drained row heights"
    );

    auto lookup = RowLookupContext{model.ids};
    auto restored = 0.0;
    require(
        model.list.restore_anchor(
            anchor,
            kViewport,
            kScrollGeneration,
            find_row_index,
            &lookup,
            restored
        ),
        "reader anchor restores after the burst"
    );
    auto anchor_top = 0.0;
    require(model.list.offset_of(149, anchor_top), "anchored row offset is available");
    require(near(restored, anchor_top + 3.0), "burst keeps the reader on the same message");

    auto end_scroll = 0.0;
    require(
        model.list.restore_anchor(pinned, kViewport, kScrollGeneration, end_scroll),
        "pinned anchor restores after the burst"
    );
    require(
        near(end_scroll, model.list.total_extent() - kViewport),
        "burst keeps a pinned list at the end"
    );

    auto measured = 0.0F;
    const auto edited_key = height_key(7, kBurstWidth, kBurstGeneration);
    require(
        cache.find(edited_key, measured) == veyra::ChatRowHeightCacheResult::hit,
        "edited row keeps its new height"
    );
    require(
        near(measured, message_height(7, kBurstWidth, kBurstGeneration) + 12.0),
        "edited row height reflects the remeasurement"
    );
    const auto deleted_key = height_key(11, kBurstWidth, kBurstGeneration);
    require(
        cache.find(deleted_key, measured) == veyra::ChatRowHeightCacheResult::miss,
        "deleted row loses its cached height"
    );
    require(model.extents[find_row(model, 11)] == 0.0F, "deleted row collapses its extent");
    const auto intact_key = height_key(3, kBurstWidth, kBurstGeneration);
    require(
        cache.find(intact_key, measured) == veyra::ChatRowHeightCacheResult::hit,
        "untouched row survives the burst"
    );
    require(
        near(measured, message_height(3, kBurstWidth, kBurstGeneration)),
        "untouched row keeps its measured height"
    );

    auto range = veyra::VirtualListRange{};
    require(model.list.visible_range(0.0, kViewport, 2, range), "top range query succeeds");
    require(range.first_item == 0, "burst list starts at its first row");
    require(
        model.list.visible_range(end_scroll, kViewport, 2, range),
        "bottom range query succeeds"
    );
    require(range.last_item == model.ids.size(), "bottom range reaches the newest row");
}

void check_pinned_anchor_holds_across_prepend_and_append() {
    auto cache_slots = std::vector<veyra::ChatRowHeightCacheSlot>(1024);
    auto cache = veyra::ChatRowHeightCache(cache_slots);
    auto model = ChatListModel{};

    seed_messages(model, cache, 5001, 120, kBurstWidth, kBurstGeneration);

    auto scroll_offset = model.list.total_extent() - kViewport;
    auto pinned = veyra::VirtualListAnchor{};
    require(
        model.list.capture_anchor(model.ids, scroll_offset, kViewport, kScrollGeneration, pinned),
        "pinned scroll position is captured"
    );
    require(pinned.pinned_to_end, "bottom scroll position pins to the end");

    prepend_messages(model, cache, 900001, 50, kBurstWidth, kBurstGeneration);
    seed_messages(model, cache, 6001, 20, kBurstWidth, kBurstGeneration);

    require(
        model.list.restore_anchor(pinned, kViewport, kScrollGeneration, scroll_offset),
        "pinned anchor restores without an ID lookup"
    );
    require(
        near(scroll_offset, model.list.total_extent() - kViewport),
        "history plus arrivals keep a pinned list at the end"
    );

    auto range = veyra::VirtualListRange{};
    require(
        model.list.visible_range(scroll_offset, kViewport, 2, range),
        "pinned range query succeeds"
    );
    require(range.last_item == model.ids.size(), "pinned range reaches the newest row");
}

void check_midlist_anchor_holds_across_prepend() {
    auto cache_slots = std::vector<veyra::ChatRowHeightCacheSlot>(1024);
    auto cache = veyra::ChatRowHeightCache(cache_slots);
    auto model = ChatListModel{};

    seed_messages(model, cache, 7001, 150, kBurstWidth, kBurstGeneration);

    auto scroll_offset = 0.0;
    require(model.list.offset_of(70, scroll_offset), "history row offset is available");
    scroll_offset += 4.0;
    auto anchor = veyra::VirtualListAnchor{};
    require(
        model.list.capture_anchor(model.ids, scroll_offset, kViewport, kScrollGeneration, anchor),
        "history anchor captures its message ID"
    );
    require(!anchor.pinned_to_end, "mid-list anchor does not pin to the end");
    require(anchor.item_id == 7071, "history anchor names the visible message");

    prepend_messages(model, cache, 800001, 50, kBurstWidth, kBurstGeneration);

    auto lookup = RowLookupContext{model.ids};
    require(
        model.list.restore_anchor(
            anchor,
            kViewport,
            kScrollGeneration,
            find_row_index,
            &lookup,
            scroll_offset
        ),
        "history anchor resolves through its message ID"
    );
    auto anchor_top = 0.0;
    require(model.list.offset_of(120, anchor_top), "shifted anchor offset is available");
    require(near(scroll_offset, anchor_top + 4.0), "prepend keeps the reader on the same message");

    auto range = veyra::VirtualListRange{};
    require(
        model.list.visible_range(scroll_offset, kViewport, 2, range),
        "history range query succeeds"
    );
    require(
        range.first_item <= 120 && 120 < range.last_item,
        "history range still shows the anchored message"
    );
}

void check_width_and_style_change_moves_extents() {
    auto cache_slots = std::vector<veyra::ChatRowHeightCacheSlot>(1024);
    auto cache = veyra::ChatRowHeightCache(cache_slots);
    auto model = ChatListModel{};

    seed_messages(model, cache, 9001, 120, kBurstWidth, kBurstGeneration);

    auto scroll_offset = 0.0;
    require(model.list.offset_of(60, scroll_offset), "restyle row offset is available");
    scroll_offset += 5.0;
    auto anchor = veyra::VirtualListAnchor{};
    require(
        model.list.capture_anchor(model.ids, scroll_offset, kViewport, kScrollGeneration, anchor),
        "restyle anchor captures its message ID"
    );

    const auto previous_total = model.list.total_extent();
    auto accumulated = 0.0;
    for (std::size_t index = 0; index < model.ids.size(); ++index) {
        const auto message_id = model.ids[index];
        auto measured = 0.0F;
        require(
            cache.find(height_key(message_id, kBurstWidth, kBurstGeneration), measured) ==
                veyra::ChatRowHeightCacheResult::hit,
            "old layout keeps its cached height"
        );
        require(
            cache.find(height_key(message_id, kNarrowWidth, kNextGeneration), measured) ==
                veyra::ChatRowHeightCacheResult::miss,
            "new layout misses before remeasurement"
        );
        const auto height = message_height(message_id, kNarrowWidth, kNextGeneration);
        const auto stored =
            cache.insert(height_key(message_id, kNarrowWidth, kNextGeneration), height);
        require(stored == veyra::ChatRowHeightCacheResult::inserted, "new layout height is cached");
        auto delta = 0.0;
        require(model.list.set_extent(index, height, delta), "new height updates the list");
        accumulated += delta;
    }

    require(
        near(model.list.total_extent(), previous_total + accumulated),
        "list total follows the accumulated remeasurements"
    );
    require(
        near(model.list.total_extent(), total_of(model.extents)),
        "list total matches the restyled row heights"
    );

    auto lookup = RowLookupContext{model.ids};
    require(
        model.list.restore_anchor(
            anchor,
            kViewport,
            kScrollGeneration,
            find_row_index,
            &lookup,
            scroll_offset
        ),
        "restyle anchor resolves through its message ID"
    );
    auto anchor_top = 0.0;
    require(model.list.offset_of(60, anchor_top), "restyled anchor offset is available");
    require(near(scroll_offset, anchor_top + 5.0), "restyle keeps the reader on the same message");

    auto range = veyra::VirtualListRange{};
    require(
        model.list.visible_range(scroll_offset, kViewport, 2, range),
        "restyle range query succeeds"
    );
    require(
        range.first_item <= 60 && 60 < range.last_item,
        "restyle range still shows the anchored message"
    );
}

}

int main() {
    try {
        check_burst_absorption();
        check_pinned_anchor_holds_across_prepend_and_append();
        check_midlist_anchor_holds_across_prepend();
        check_width_and_style_change_moves_extents();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
