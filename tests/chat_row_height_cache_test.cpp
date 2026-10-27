#include "veyra/chat/message/message_change_queue.hpp"
#include "veyra/chat/rows/row_height_cache.hpp"
#include "veyra/layout/scroll/virtual_list.hpp"

#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

struct MessageIndexContext {
    std::span<const std::uint64_t> message_ids;
};

void require(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

bool find_message_index(void* context, std::uint64_t message_id, std::size_t& item_index) noexcept {
    const auto& message_ids = static_cast<MessageIndexContext*>(context)->message_ids;
    for (std::size_t index = 0; index < message_ids.size(); ++index) {
        if (message_ids[index] == message_id) {
            item_index = index;
            return true;
        }
    }
    return false;
}

void check_layout_keys_and_message_invalidation() {
    std::array<veyra::ChatRowHeightCacheSlot, 8> slots{};
    auto cache = veyra::ChatRowHeightCache(slots);
    const auto key = veyra::ChatRowHeightKey{
        .message_id = 44,
        .available_width = 640.0F,
        .style_generation = 7,
    };
    auto height = 0.0F;

    require(cache.valid(), "caller slots configure the row height cache");
    require(
        cache.find(key, height) == veyra::ChatRowHeightCacheResult::miss,
        "unmeasured row height misses"
    );
    require(
        cache.insert(key, 56.0F) == veyra::ChatRowHeightCacheResult::inserted,
        "measured row height inserts"
    );

    require(
        cache.find(key, height) == veyra::ChatRowHeightCacheResult::hit && height == 56.0F,
        "matching layout retrieves the measured height"
    );
    require(
        cache.insert(key, 60.0F) == veyra::ChatRowHeightCacheResult::updated,
        "remeasuring a layout updates its existing slot"
    );
    require(
        cache.find(key, height) == veyra::ChatRowHeightCacheResult::hit && height == 60.0F,
        "updated measurement replaces the old height"
    );

    auto changed_width = key;
    changed_width.available_width = 480.0F;
    require(
        cache.insert(changed_width, 72.0F) == veyra::ChatRowHeightCacheResult::inserted,
        "different widths retain separate measurements"
    );

    auto changed_style = key;
    ++changed_style.style_generation;
    require(
        cache.insert(changed_style, 64.0F) == veyra::ChatRowHeightCacheResult::inserted,
        "style changes retain separate measurements"
    );

    const auto other_message = veyra::ChatRowHeightKey{
        .message_id = 45,
        .available_width = 640.0F,
        .style_generation = 7,
    };
    require(
        cache.insert(other_message, 24.0F) == veyra::ChatRowHeightCacheResult::inserted,
        "other messages share the bounded cache"
    );
    require(cache.invalidate_message(44) == 3, "an edit invalidates every cached layout of a row");
    require(
        cache.find(key, height) == veyra::ChatRowHeightCacheResult::miss,
        "invalidated row layout misses"
    );
    require(
        cache.find(other_message, height) == veyra::ChatRowHeightCacheResult::hit &&
            height == 24.0F,
        "invalidation preserves other rows"
    );
}

void check_least_recently_used_eviction() {
    std::array<veyra::ChatRowHeightCacheSlot, 4> slots{};
    auto cache = veyra::ChatRowHeightCache(slots);
    auto keys = std::array<veyra::ChatRowHeightKey, 5>{};
    auto height = 0.0F;

    for (std::size_t index = 0; index < 4; ++index) {
        keys[index] = {
            .message_id = index + 1,
            .available_width = 320.0F,
            .style_generation = 1,
        };
        require(
            cache.insert(keys[index], static_cast<float>(index + 20)) ==
                veyra::ChatRowHeightCacheResult::inserted,
            "four colliding rows fill a bucket"
        );
    }

    require(
        cache.find(keys[0], height) == veyra::ChatRowHeightCacheResult::hit,
        "a hit refreshes row recency"
    );
    keys[4] = {
        .message_id = 5,
        .available_width = 320.0F,
        .style_generation = 1,
    };
    require(
        cache.insert(keys[4], 24.0F) == veyra::ChatRowHeightCacheResult::inserted,
        "a colliding row replaces the least-recent entry"
    );
    require(
        cache.find(keys[1], height) == veyra::ChatRowHeightCacheResult::miss,
        "the least-recent row is evicted"
    );
    require(
        cache.find(keys[0], height) == veyra::ChatRowHeightCacheResult::hit,
        "the recently used row survives eviction"
    );
    require(
        cache.find(keys[4], height) == veyra::ChatRowHeightCacheResult::hit,
        "the new measurement is the most recent"
    );
}

void check_invalid_inputs_and_storage() {
    std::array<veyra::ChatRowHeightCacheSlot, 3> invalid_slots{};
    auto invalid_cache = veyra::ChatRowHeightCache(invalid_slots);
    auto height = 17.0F;
    const auto key = veyra::ChatRowHeightKey{
        .message_id = 1,
        .available_width = 100.0F,
    };
    require(!invalid_cache.valid(), "slot count must fill complete four-way buckets");
    require(
        invalid_cache.find(key, height) == veyra::ChatRowHeightCacheResult::invalid_workspace,
        "lookup reports invalid caller storage"
    );
    require(
        invalid_cache.insert(key, 20.0F) == veyra::ChatRowHeightCacheResult::invalid_workspace,
        "insertion reports invalid caller storage"
    );
    require(height == 17.0F, "failed lookup leaves output unchanged");

    std::array<veyra::ChatRowHeightCacheSlot, 4> slots{};
    auto cache = veyra::ChatRowHeightCache(slots);
    require(
        cache.insert({.available_width = 100.0F}, 20.0F) ==
            veyra::ChatRowHeightCacheResult::invalid_key,
        "zero message IDs are rejected"
    );
    require(
        cache.insert(
            {
                .message_id = 1,
                .available_width = std::numeric_limits<float>::infinity(),
            },
            20.0F
        ) == veyra::ChatRowHeightCacheResult::invalid_key,
        "non-finite layout widths are rejected"
    );
    require(
        cache.insert(key, -1.0F) == veyra::ChatRowHeightCacheResult::invalid_height,
        "negative row heights are rejected"
    );
    require(
        cache.insert(key, std::numeric_limits<float>::quiet_NaN()) ==
            veyra::ChatRowHeightCacheResult::invalid_height,
        "non-finite row heights are rejected"
    );
    require(cache.invalidate_message(0) == 0, "zero message ID invalidates nothing");
    require(cache.invalidate_message(1) == 0, "absent messages invalidate nothing");
    cache.clear();
    require(
        cache.find(key, height) == veyra::ChatRowHeightCacheResult::miss,
        "clear removes cached measurements"
    );
}

void check_edit_remeasurement_preserves_scroll_anchor() {
    std::array<veyra::ChatRowHeightCacheSlot, 16> slots{};
    auto cache = veyra::ChatRowHeightCache(slots);
    auto change_storage = std::array<veyra::ChatMessageChange, 4>{};
    auto changes = veyra::ChatMessageChangeQueue(change_storage);
    auto pending_changes = std::array<veyra::ChatMessageChange, 2>{};
    std::array extents{10.0F, 20.0F, 30.0F, 40.0F};
    std::array<double, 5> fenwick{};
    std::array<std::uint64_t, 4> message_ids{101, 102, 103, 104};
    auto list = veyra::VirtualListIndex{};
    require(list.reset(extents, fenwick), "chat rows initialize the virtual list");

    constexpr auto width = 640.0F;
    constexpr auto style_generation = std::uint64_t{2};
    constexpr auto scroll_generation = std::uint64_t{7};
    for (std::size_t index = 0; index < message_ids.size(); ++index) {
        require(
            cache.insert(
                {
                    .message_id = message_ids[index],
                    .available_width = width,
                    .style_generation = style_generation,
                },
                extents[index]
            ) == veyra::ChatRowHeightCacheResult::inserted,
            "initial message heights enter the cache"
        );
    }

    auto anchor = veyra::VirtualListAnchor{};
    require(
        list.capture_anchor(message_ids, 35.0, 20.0, scroll_generation, anchor),
        "chat scroll captures a stable message anchor"
    );
    require(anchor.item_id == message_ids[2], "the anchor identifies the first visible message");

    const auto edited_message_id = message_ids[0];
    require(
        changes.try_push({veyra::ChatMessageChangeKind::edited, edited_message_id}) ==
            veyra::ChatMessageChangeQueueResult::queued,
        "edit notification enters the bounded message queue"
    );
    const auto change_count = changes.try_pop_batch(pending_changes);
    require(
        change_count == 1 && pending_changes[0].kind == veyra::ChatMessageChangeKind::edited,
        "the UI drains the edit notification in its frame batch"
    );
    require(
        cache.invalidate_message(pending_changes[0].message_id) == 1,
        "the queued edit invalidates its prior row height"
    );

    const auto edited_key = veyra::ChatRowHeightKey{
        .message_id = edited_message_id,
        .available_width = width,
        .style_generation = style_generation,
    };
    auto measured_height = 0.0F;
    require(
        cache.find(edited_key, measured_height) == veyra::ChatRowHeightCacheResult::miss,
        "the edited row requires a new measurement"
    );
    measured_height = 18.0F;
    require(
        cache.insert(edited_key, measured_height) == veyra::ChatRowHeightCacheResult::inserted,
        "the edited row stores its new height"
    );

    auto delta = 0.0;
    require(list.set_extent(0, measured_height, delta), "new measurement updates list geometry");
    require(delta == 8.0, "geometry reports the height correction");

    auto lookup_context = MessageIndexContext{message_ids};
    auto scroll_offset = 35.0;
    require(
        list.restore_anchor(
            anchor,
            20.0,
            scroll_generation,
            find_message_index,
            &lookup_context,
            scroll_offset
        ),
        "the stable anchor restores after row correction"
    );

    double anchor_top = 0.0;
    require(list.offset_of(anchor.item_index, anchor_top), "anchored row offset is available");
    require(
        anchor_top - scroll_offset == -5.0,
        "remeasuring a row above the viewport leaves the anchor at the same screen position"
    );
    require(
        cache.find(
            {
                .message_id = message_ids[1],
                .available_width = width,
                .style_generation = style_generation,
            },
            measured_height
        ) == veyra::ChatRowHeightCacheResult::hit,
        "editing one message leaves other cached row heights intact"
    );
}

}

int main() {
    try {
        check_layout_keys_and_message_invalidation();
        check_least_recently_used_eviction();
        check_invalid_inputs_and_storage();
        check_edit_remeasurement_preserves_scroll_anchor();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
