#include "allocation_counter.hpp"
#include "veyra/behaviors/data/log_feed.hpp"
#include "veyra/behaviors/control/scroll.hpp"
#include "veyra/chat/message_change_queue.hpp"
#include "veyra/chat/row_height_cache.hpp"
#include "veyra/layout/virtual_list.hpp"
#include "veyra/media/animation_frame_cache.hpp"
#include "veyra/media/animation_timeline.hpp"
#include "veyra/media/image_codecs.hpp"
#include "veyra/widgets/data/log_feed.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace {

void require(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

bool near(double left, double right) {
    return std::abs(left - right) < 0.001;
}

constexpr auto kChatWidth = 640.0F;
constexpr auto kNarrowWidth = 480.0F;
constexpr auto kViewport = 120.0;
constexpr auto kScrollGeneration = std::uint64_t{7};
constexpr auto kStaticEmoteId = std::uint64_t{7};
constexpr auto kAnimatedEmoteId = std::uint64_t{9};
constexpr auto kOffscreenEmoteId = std::uint64_t{11};

constexpr std::array<std::uint8_t, 70> kStaticBmp{{
    'B',  'M',  0x46, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x36, 0x00, 0x00, 0x00,
    0x28, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x01, 0x00,
    0x18, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0x00,
    0x00, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0x00, 0xFF, 0x00, 0x00, 0x00,
}};

constexpr std::array<std::uint8_t, 76> kAnimatedGif{{
    0x47, 0x49, 0x46, 0x38, 0x39, 0x61, 0x02, 0x00, 0x02, 0x00, 0xF0, 0x00, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF, 0x21, 0xF9, 0x04, 0x00, 0x0A, 0x00, 0x00, 0x00, 0x2C, 0x00, 0x00, 0x00, 0x00,
    0x02, 0x00, 0x02, 0x00, 0x00, 0x08, 0x07, 0x00, 0x01, 0x04, 0x08, 0x00, 0x20, 0x20, 0x00, 0x21,
    0xF9, 0x04, 0x00, 0x14, 0x00, 0x00, 0x00, 0x2C, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x02, 0x00,
    0x00, 0x08, 0x07, 0x00, 0x03, 0x00, 0x00, 0x10, 0x20, 0x20, 0x00, 0x3B,
}};

struct ChatRow {
    veyra::LogFeedKind kind{veyra::LogFeedKind::info};
    bool static_emote{};
    bool animated_emote{};
    std::uint64_t emote_id{};
    bool edited{};
    bool deleted{};
    std::uint32_t folds{1};
};

float row_height_for(const ChatRow& row, float width, std::uint64_t style) {
    auto height = 16.0F + static_cast<float>(style * 2) + (width < 600.0F ? 8.0F : 0.0F);
    height += row.static_emote ? 14.0F : 0.0F;
    height += row.animated_emote ? 22.0F : 0.0F;
    height += row.edited ? 12.0F : 0.0F;
    return height;
}

veyra::ChatRowHeightKey height_key(std::uint64_t id, float width, std::uint64_t style) {
    return {.message_id = id, .available_width = width, .style_generation = style};
}

bool find_chat_row(void* context, std::uint64_t id, std::size_t& index) noexcept {
    const auto* ids = static_cast<const std::vector<std::uint64_t>*>(context);
    for (std::size_t cursor = 0; cursor < ids->size(); ++cursor) {
        if ((*ids)[cursor] == id) {
            index = cursor;
            return true;
        }
    }
    return false;
}

struct ChatView {
    std::vector<veyra::ChatRowHeightCacheSlot> height_slots{2048};
    veyra::ChatRowHeightCache heights{height_slots};
    std::array<veyra::ChatMessageChange, 64> change_storage{};
    veyra::ChatMessageChangeQueue changes{change_storage};
    veyra::LogFeed feed;
    std::vector<std::uint64_t> ids;
    std::vector<ChatRow> rows;
    std::vector<float> extents;
    std::vector<double> fenwick;
    veyra::VirtualListIndex list;
    float width{kChatWidth};
    std::uint64_t style{1};
    std::uint64_t now_ms{};
    std::uint64_t next_id{1};

    ChatView() {
        ids.reserve(256);
        rows.reserve(256);
        extents.reserve(256);
        fenwick.reserve(257);
    }

    bool valid() const noexcept {
        return heights.valid() && changes.valid();
    }

    bool rebuild() {
        fenwick.assign(extents.size() + 1, 0.0);
        return list.reset(extents, fenwick);
    }

    std::size_t find_row(std::uint64_t id) const {
        for (std::size_t index = 0; index < ids.size(); ++index) {
            if (ids[index] == id) {
                return index;
            }
        }
        return ids.size();
    }

    float cache_height(std::size_t index) {
        const auto height = row_height_for(rows[index], width, style);
        const auto stored = heights.insert(height_key(ids[index], width, style), height);
        require(
            stored == veyra::ChatRowHeightCacheResult::inserted ||
                stored == veyra::ChatRowHeightCacheResult::updated,
            "caller measurement enters the row height cache"
        );
        return height;
    }

    void push_row(veyra::LogFeedKind kind, ChatRow row, std::uint64_t advance_ms) {
        const auto pushed = feed.push(next_id++, kind, now_ms);
        now_ms += advance_ms;
        require(pushed.result == veyra::LogFeedResult::appended, "spaced rows append, never fold");
        row.kind = kind;
        ids.push_back(next_id - 1);
        rows.push_back(row);
        extents.push_back(cache_height(ids.size() - 1));
    }

    void push_reaction_burst(std::size_t count) {
        for (std::size_t step = 0; step < count; ++step) {
            const auto pushed = feed.push(next_id++, veyra::LogFeedKind::error, now_ms);
            now_ms += 100;
            if (step == 0) {
                require(pushed.result == veyra::LogFeedResult::appended, "burst opens its row");
                ids.push_back(next_id - 1);
                rows.push_back(ChatRow{.kind = veyra::LogFeedKind::error});
                extents.push_back(cache_height(ids.size() - 1));
                continue;
            }
            require(pushed.result == veyra::LogFeedResult::collapsed, "burst reactions fold");
            ids.back() = pushed.id;
            rows.back().folds = pushed.count;
        }
    }

    void apply_change(veyra::ChatMessageChange change) {
        const auto row = find_row(change.message_id);
        require(row != ids.size(), "queued change resolves to a live row");
        require(
            heights.invalidate_message(change.message_id) <= 2,
            "change clears its own layouts"
        );
        if (change.kind == veyra::ChatMessageChangeKind::edited) {
            rows[row].edited = true;
            const auto height = cache_height(row);
            auto delta = 0.0;
            require(list.set_extent(row, height, delta), "edit moves its own extent");
            extents[row] = height;
            require(near(delta, 12.0), "edit correction reports the remeasured growth");
            return;
        }
        require(change.kind == veyra::ChatMessageChangeKind::deleted, "change kind is known");
        rows[row].deleted = true;
        auto delta = 0.0;
        require(list.set_extent(row, 0.0F, delta), "delete collapses its extent");
        extents[row] = 0.0F;
    }

    void drain_changes() {
        auto batch = std::array<veyra::ChatMessageChange, 64>{};
        auto count = changes.try_pop_batch(batch);
        while (count != 0) {
            for (std::size_t index = 0; index < count; ++index) {
                apply_change(batch[index]);
            }
            count = changes.try_pop_batch(batch);
        }
    }

    void queue_change(veyra::ChatMessageChangeKind kind, std::uint64_t id) {
        require(
            changes.try_push({kind, id}) == veyra::ChatMessageChangeQueueResult::queued,
            "chat change enters the bounded queue"
        );
    }

    void prepend_history(std::uint64_t first_id, std::size_t count) {
        auto fresh_ids = std::vector<std::uint64_t>{};
        auto fresh_rows = std::vector<ChatRow>{};
        auto fresh_extents = std::vector<float>{};
        for (std::size_t step = 0; step < count; ++step) {
            ChatRow row{};
            if (step % 3 == 0) {
                row.static_emote = true;
                row.emote_id = kStaticEmoteId;
            }
            const auto id = first_id + static_cast<std::uint64_t>(step);
            fresh_ids.push_back(id);
            fresh_rows.push_back(row);
            fresh_extents.push_back(row_height_for(row, width, style));
            require(
                heights.insert(height_key(id, width, style), fresh_extents.back()) ==
                    veyra::ChatRowHeightCacheResult::inserted,
                "history height is cached"
            );
        }
        ids.insert(ids.begin(), fresh_ids.begin(), fresh_ids.end());
        rows.insert(rows.begin(), fresh_rows.begin(), fresh_rows.end());
        extents.insert(extents.begin(), fresh_extents.begin(), fresh_extents.end());
        require(rebuild(), "prepended history rebuilds the virtual list");
    }
};

void seed_mixed_rows(ChatView& view, std::uint64_t count) {
    constexpr std::array<veyra::LogFeedKind, 3> kinds{
        veyra::LogFeedKind::info,
        veyra::LogFeedKind::warning,
        veyra::LogFeedKind::error,
    };
    for (std::uint64_t step = 0; step < count; ++step) {
        ChatRow row{};
        if (step % 4 == 1) {
            row.static_emote = true;
            row.emote_id = kStaticEmoteId;
        }
        if (step % 9 == 5) {
            row.animated_emote = true;
            row.emote_id = kAnimatedEmoteId;
        }
        view.push_row(kinds[step % 3], row, 2000);
    }
}

struct EmoteStore {
    std::array<veyra::AnimationFrameCacheSlot, 4> slots{};
    std::array<veyra::SoftwarePixel, 16> storage{};
    veyra::AnimationFrameCache cache{slots, storage, 2, 2};
    std::uint32_t static_decodes{};
    std::uint32_t animated_decodes{};
    std::uint64_t generation{1};
    bool decoded[12]{};

    void ensure(std::uint64_t emote_id, std::span<const std::uint8_t> bytes, std::uint32_t frames) {
        if (decoded[emote_id % 12]) {
            return;
        }
        auto info = veyra::ImageDecodeInfo{};
        require(
            veyra::probe_image(bytes, veyra::ImageDecodeBudget{}, info) ==
                veyra::ImageDecodeResult::complete,
            "emote bytes probe clean"
        );
        require(info.frames == frames, "emote probe reports its frame count");
        auto rgba = std::array<std::array<std::uint8_t, 16>, 2>{};
        auto targets = std::array<veyra::ImageFrameTarget, 2>{};
        for (std::uint32_t frame = 0; frame < frames; ++frame) {
            targets[frame].pixels = std::span<std::uint8_t>{
                rgba[frame].data(),
                static_cast<std::size_t>(info.width) * info.height * 4,
            };
        }
        auto delays = std::array<std::uint32_t, 2>{0xFFFFFFFFU, 0xFFFFFFFFU};
        auto produced = std::uint32_t{};
        require(
            veyra::decode_image(
                bytes,
                veyra::ImageDecodeBudget{},
                std::span<veyra::ImageFrameTarget>{targets.data(), frames},
                std::span<std::uint32_t>{delays.data(), frames},
                produced
            ) == veyra::ImageDecodeResult::complete,
            "emote bytes decode clean"
        );
        require(produced == frames, "emote decode produces every frame");
        for (std::uint32_t frame = 0; frame < produced; ++frame) {
            auto pixels = std::array<veyra::SoftwarePixel, 4>{};
            for (std::size_t pixel = 0; pixel < 4; ++pixel) {
                pixels[pixel] = {
                    .red = rgba[frame][pixel * 4],
                    .green = rgba[frame][pixel * 4 + 1],
                    .blue = rgba[frame][pixel * 4 + 2],
                    .alpha = rgba[frame][pixel * 4 + 3],
                };
            }
            const veyra::AnimationFrame image{
                .image = {
                    .width = 2,
                    .height = 2,
                    .stride = 2,
                    .pixels = pixels,
                    .generation = generation++
                },
            };
            const veyra::AnimationFrameKey key{.animation_id = emote_id, .frame_index = frame};
            require(
                cache.insert(key, image) == veyra::AnimationFrameCacheResult::inserted,
                "emote frame enters the bounded frame cache"
            );
        }
        if (frames == 1) {
            ++static_decodes;
        } else {
            ++animated_decodes;
        }
        decoded[emote_id % 12] = true;
    }
};

std::size_t emit_visible_slice(
    const veyra::Theme& theme,
    const ChatView& view,
    const veyra::VirtualListRange& range,
    std::span<veyra::LogFeedRowState> states,
    std::span<veyra::LayoutRect> badges,
    std::span<veyra::DisplayCommand> commands
) {
    const auto visible = range.last_item - range.first_item;
    require(
        visible <= states.size() && visible <= badges.size(),
        "visible slice fits caller spans"
    );
    auto base_offset = 0.0;
    require(view.list.offset_of(range.first_item, base_offset), "slice base offsets from index");
    for (auto cursor = range.first_item; cursor < range.last_item; ++cursor) {
        const auto slot = cursor - range.first_item;
        states[slot].kind = view.rows[cursor].kind;
        states[slot].collapsed = view.rows[cursor].folds > 1;
        if (!states[slot].collapsed) {
            badges[slot] = {};
            continue;
        }
        auto row_offset = 0.0;
        require(view.list.offset_of(cursor, row_offset), "collapsed row offsets from index");
        badges[slot] = {
            .x = 148.0F,
            .y = static_cast<float>(row_offset - base_offset) + 2.0F,
            .width = 44.0F,
            .height = 12.0F,
        };
    }
    const veyra::VirtualListRowsGeometry geometry{
        .list_bounds = {.x = 0.0F, .y = 0.0F, .width = 200.0F, .height = 120.0F},
        .row_height = 24.0F,
    };
    const auto output = veyra::emit_log_feed_rows(
        theme,
        {states.data(), visible},
        {badges.data(), visible},
        geometry,
        commands
    );
    require(output.result == veyra::LogFeedEmitResult::complete, "visible slice emits");
    require(output.required == output.placed, "sized span fits the whole slice");
    auto pills = std::size_t{};
    for (auto cursor = std::size_t{}; cursor < output.placed; ++cursor) {
        if (std::get_if<veyra::FillRoundedRect>(&commands[cursor]) != nullptr) {
            ++pills;
        }
    }
    return pills;
}

void check_burst_streams_through_dedup_window_and_emit() {
    auto view = ChatView{};
    require(view.valid(), "chat view workspace starts valid");
    view.push_row(veyra::LogFeedKind::info, {}, 2000);
    view.push_row(
        veyra::LogFeedKind::warning,
        {.static_emote = true, .emote_id = kStaticEmoteId},
        2000
    );
    view.push_row(
        veyra::LogFeedKind::error,
        {.animated_emote = true, .emote_id = kAnimatedEmoteId},
        2000
    );
    view.push_row(veyra::LogFeedKind::info, {}, 2000);
    view.push_row(
        veyra::LogFeedKind::warning,
        {.static_emote = true, .emote_id = kStaticEmoteId},
        2000
    );
    view.push_row(
        veyra::LogFeedKind::error,
        {.animated_emote = true, .emote_id = kAnimatedEmoteId},
        2000
    );
    require(view.rebuild(), "mixed burst rebuilds the virtual list");

    const auto total_before_burst = view.list.total_extent();
    view.push_reaction_burst(6);
    require(view.rebuild(), "reaction burst rebuilds the virtual list");
    require(
        near(view.list.total_extent(), total_before_burst + view.extents.back()),
        "only the new burst row moves the total, folds move nothing"
    );
    auto tail = veyra::LogFeedRecord{};
    require(view.feed.record_at(view.feed.size() - 1, tail), "burst tail reads back");
    require(tail.count == 6 && view.rows.back().folds == 6, "six reactions fold into one tail row");
    auto tail_slot = std::size_t{};
    require(
        view.feed.index_of(tail.id, tail_slot) && tail_slot == view.feed.size() - 1,
        "folded newest id resolves to the tail row"
    );

    const auto edited_target = view.ids[3];
    const auto deleted_target = view.ids[4];
    for (std::uint64_t step = 0; step < 50; ++step) {
        const auto kind = step % 2 == 0 ? veyra::LogFeedKind::info : veyra::LogFeedKind::warning;
        view.push_row(kind, {.static_emote = true, .emote_id = kStaticEmoteId}, 2000);
    }
    for (std::uint64_t step = 0; step < 3; ++step) {
        view.push_row(
            veyra::LogFeedKind::error,
            {.animated_emote = true, .emote_id = kOffscreenEmoteId},
            2000
        );
    }
    require(view.rebuild(), "shared-emote burst rebuilds the virtual list");

    view.queue_change(veyra::ChatMessageChangeKind::edited, edited_target);
    view.queue_change(veyra::ChatMessageChangeKind::deleted, deleted_target);
    view.drain_changes();
    require(view.find_row(edited_target) == 3, "edit keeps its row index");
    require(view.find_row(deleted_target) == 4, "delete keeps its row index");
    auto edited_height = 0.0F;
    require(
        view.heights.find(height_key(edited_target, kChatWidth, 1), edited_height) ==
            veyra::ChatRowHeightCacheResult::hit,
        "edited row keeps its remeasured height"
    );
    require(near(edited_height, row_height_for(view.rows[3], kChatWidth, 1)), "edit remeasures");
    auto dropped_height = 0.0F;
    require(
        view.heights.find(height_key(deleted_target, kChatWidth, 1), dropped_height) ==
            veyra::ChatRowHeightCacheResult::miss,
        "deleted row loses its cached height"
    );
    require(view.extents[4] == 0.0F, "deleted row collapses its extent");

    auto scroller = veyra::ScrollController{};
    require(scroller.configure(view.list.total_extent(), kViewport), "scroller takes the total");
    require(
        scroller.set_offset(scroller.maximum_offset()) == veyra::ScrollResult::changed,
        "chat starts pinned to its tail"
    );
    auto range = veyra::VirtualListRange{};
    require(
        view.list.visible_range(scroller.offset(), scroller.viewport_extent(), 2, range),
        "tail window resolves to a visible range"
    );
    const auto theme = veyra::Theme{};
    auto states = std::array<veyra::LogFeedRowState, 64>{};
    auto badges = std::array<veyra::LayoutRect, 64>{};
    auto commands = std::array<veyra::DisplayCommand, 256>{};
    const auto pills = emit_visible_slice(theme, view, range, states, badges, commands);
    auto collapsed = std::size_t{};
    for (auto cursor = range.first_item; cursor < range.last_item; ++cursor) {
        collapsed += view.rows[cursor].folds > 1 ? 1 : 0;
    }
    require(pills == collapsed, "one badge pill lands per collapsed row, none for plain rows");
}

void check_anchor_survives_prepend_append_resize_and_correction() {
    auto view = ChatView{};
    seed_mixed_rows(view, 60);
    require(view.rebuild(), "seeded chat rows build the virtual list");

    auto anchor_top = 0.0;
    require(view.list.offset_of(40, anchor_top), "anchor row offset is available");
    auto anchor = veyra::VirtualListAnchor{};
    require(
        view.list.capture_anchor(view.ids, anchor_top + 3.0, kViewport, kScrollGeneration, anchor),
        "reader anchor captures before growth"
    );
    const auto anchored_id = anchor.item_id;
    require(!anchor.pinned_to_end, "mid-list anchor does not pin to the end");

    auto restore_to_anchor = [&](std::string_view message) {
        auto restored = 0.0;
        require(
            view.list.restore_anchor(
                anchor,
                kViewport,
                kScrollGeneration,
                find_chat_row,
                &view.ids,
                restored
            ),
            message
        );
        auto moved_top = 0.0;
        require(view.list.offset_of(view.find_row(anchored_id), moved_top), message);
        require(near(restored, moved_top + 3.0), message);
    };

    seed_mixed_rows(view, 40);
    require(view.rebuild(), "arrivals rebuild the virtual list");
    restore_to_anchor("appends keep the reader on the same message");

    view.prepend_history(900001, 50);
    require(view.find_row(anchored_id) == 90, "prepend shifts the anchored row by history count");
    restore_to_anchor("prepend keeps the reader on the same message");

    view.width = kNarrowWidth;
    view.style = 2;
    for (std::size_t index = 0; index < view.ids.size(); ++index) {
        if (view.rows[index].deleted) {
            continue;
        }
        const auto height = view.cache_height(index);
        auto delta = 0.0;
        require(view.list.set_extent(index, height, delta), "narrow height updates the list");
        view.extents[index] = height;
    }
    restore_to_anchor("resize keeps the reader on the same message");

    const auto above_anchor = view.ids[view.find_row(anchored_id) - 2];
    view.queue_change(veyra::ChatMessageChangeKind::edited, above_anchor);
    view.drain_changes();
    restore_to_anchor("correction above keeps the reader in place");

    const auto removed_above = view.ids[view.find_row(anchored_id) - 4];
    view.queue_change(veyra::ChatMessageChangeKind::deleted, removed_above);
    view.drain_changes();
    require(view.find_row(removed_above) != view.ids.size(), "delete keeps stable row identity");
    restore_to_anchor("delete above keeps the reader on the same message");
}

void check_shared_emote_decodes_once() {
    auto store = EmoteStore{};
    require(store.cache.valid(), "caller storage configures the frame cache");
    store.ensure(kStaticEmoteId, kStaticBmp, 1);
    require(store.static_decodes == 1, "static asset decodes once for its first row");
    for (std::uint64_t row = 0; row < 50; ++row) {
        const auto found = store.cache.find({.animation_id = kStaticEmoteId, .frame_index = 0});
        require(found.has_value(), "every sharing row hits the decoded static frame");
        require(found->image.width == 2 && found->image.height == 2, "shared frame keeps size");
    }
    require(store.static_decodes == 1, "fifty sharing rows still decode the static asset once");

    store.ensure(kAnimatedEmoteId, kAnimatedGif, 2);
    require(store.animated_decodes == 1, "animated asset decodes once for all its rows");
    for (std::uint32_t frame = 0; frame < 2; ++frame) {
        require(
            store.cache.find({.animation_id = kAnimatedEmoteId, .frame_index = frame}).has_value(),
            "each animated frame is cached after one decode"
        );
    }
    require(
        !store.cache.find({.animation_id = kOffscreenEmoteId, .frame_index = 0}).has_value(),
        "frame indexes stay scoped to their animation identity"
    );
}

void check_frame_budget_evicts_least_recently_used() {
    std::array<veyra::AnimationFrameCacheSlot, 4> slots{};
    std::array<veyra::SoftwarePixel, 16> storage{};
    auto cache = veyra::AnimationFrameCache{slots, storage, 2, 2};
    require(cache.valid(), "four slots form one four-way bucket");
    auto pixels = std::array<veyra::SoftwarePixel, 5>{};
    for (std::size_t index = 0; index < pixels.size(); ++index) {
        pixels[index] = veyra::SoftwarePixel{.red = static_cast<std::uint8_t>(index + 1)};
    }
    auto keys = std::array<veyra::AnimationFrameKey, 5>{};
    for (std::size_t index = 0; index < keys.size(); ++index) {
        keys[index] = {.animation_id = 100 + static_cast<std::uint64_t>(index), .frame_index = 0};
    }
    for (std::size_t index = 0; index < 4; ++index) {
        const veyra::AnimationFrame image{
            .image = {
                .width = 2,
                .height = 2,
                .stride = 2,
                .pixels = {&pixels[index], 4},
                .generation = 1
            },
        };
        require(
            cache.insert(keys[index], image) == veyra::AnimationFrameCacheResult::inserted,
            "four emote frames fill the single bucket"
        );
    }
    require(cache.find(keys[0]).has_value(), "a hit refreshes the oldest frame");
    const veyra::AnimationFrame fifth{
        .image = {.width = 2, .height = 2, .stride = 2, .pixels = {&pixels[4], 4}, .generation = 2},
    };
    require(
        cache.insert(keys[4], fifth) == veyra::AnimationFrameCacheResult::inserted,
        "a full bucket replaces its least recently used frame"
    );
    require(!cache.find(keys[1]).has_value(), "least recent frame is evicted");
    require(cache.find(keys[0]).has_value(), "recently used frame survives eviction");
    require(cache.find(keys[4]).has_value(), "new frame is the most recent");
}

void check_visibility_gates_animation_and_decode() {
    auto store = EmoteStore{};
    store.ensure(kStaticEmoteId, kStaticBmp, 1);
    store.ensure(kAnimatedEmoteId, kAnimatedGif, 2);
    require(store.animated_decodes == 1, "visible animation decodes once up front");

    constexpr std::array<std::uint32_t, 2> durations{{100, 200}};
    auto onscreen = veyra::AnimationTimeline{durations, 0};
    auto offscreen = veyra::AnimationTimeline{durations, 0};
    require(onscreen.valid() && offscreen.valid(), "emote timelines start valid");
    require(!offscreen.advance(500, false), "hidden timeline requests no redraw");
    require(offscreen.current_frame_index() == 0, "hidden timeline never advances");
    require(store.animated_decodes == 1, "offscreen rows decode nothing");
    require(
        !store.cache.find({.animation_id = kOffscreenEmoteId, .frame_index = 0}).has_value(),
        "offscreen emote never enters the frame cache"
    );

    require(onscreen.advance(150), "visible timeline crosses its first frame");
    require(onscreen.current_frame_index() == 1, "visible timeline selects frame one");
    require(
        store.cache
            .find(
                {.animation_id = kAnimatedEmoteId,
                 .frame_index = static_cast<std::uint32_t>(onscreen.current_frame_index())}
            )
            .has_value(),
        "visible frame resolves from the shared decode"
    );
    require(store.animated_decodes == 1, "advancing a cached animation decodes nothing new");
}

void check_steady_replay_allocates_nothing() {
    auto view = ChatView{};
    seed_mixed_rows(view, 60);
    require(view.rebuild(), "steady rows build the virtual list");

    const auto theme = veyra::Theme{};
    auto states = std::array<veyra::LogFeedRowState, 64>{};
    auto badges = std::array<veyra::LayoutRect, 64>{};
    auto commands = std::array<veyra::DisplayCommand, 256>{};
    auto scroller = veyra::ScrollController{};
    require(scroller.configure(view.list.total_extent(), kViewport), "steady scroller configures");
    require(
        scroller.set_offset(scroller.maximum_offset()) == veyra::ScrollResult::changed,
        "steady replay starts pinned to the tail"
    );

    auto run_steady_frame = [&]() {
        auto anchor = veyra::VirtualListAnchor{};
        require(
            view.list.capture_anchor(
                view.ids,
                scroller.offset(),
                scroller.viewport_extent(),
                scroller.generation(),
                anchor
            ),
            "steady frame captures its anchor"
        );
        auto range = veyra::VirtualListRange{};
        require(
            view.list.visible_range(scroller.offset(), scroller.viewport_extent(), 2, range),
            "steady frame resolves its visible range"
        );
        emit_visible_slice(theme, view, range, states, badges, commands);
        auto restored = 0.0;
        require(
            view.list.restore_anchor(
                anchor,
                scroller.viewport_extent(),
                scroller.generation(),
                find_chat_row,
                &view.ids,
                restored
            ),
            "steady frame restores its anchor"
        );
        require(near(restored, scroller.offset()), "steady frame holds its scroll offset");
        auto measured = 0.0F;
        for (auto cursor = range.first_item; cursor < range.last_item; ++cursor) {
            require(
                view.heights.find(height_key(view.ids[cursor], view.width, view.style), measured) ==
                    veyra::ChatRowHeightCacheResult::hit,
                "steady frame hits every visible height"
            );
        }
    };
    run_steady_frame();

    auto allocations = std::size_t{0};
    {
        auto measure = Measure{};
        for (std::size_t repeat = 0; repeat < 64; ++repeat) {
            run_steady_frame();
        }
        allocations = measure.count();
    }
    require(allocations == 0, "steady push-window-emit replay allocates nothing");
}

}

int main() {
    try {
        check_burst_streams_through_dedup_window_and_emit();
        check_anchor_survives_prepend_append_resize_and_correction();
        check_shared_emote_decodes_once();
        check_frame_budget_evicts_least_recently_used();
        check_visibility_gates_animation_and_decode();
        check_steady_replay_allocates_nothing();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
