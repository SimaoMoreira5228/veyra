#include "veyra/behaviors/log_feed.hpp"
#include "veyra/behaviors/scroll.hpp"
#include "veyra/data/list_pipeline.hpp"
#include "veyra/layout/virtual_list.hpp"
#include "veyra/widgets/data/log_feed.hpp"

#include <array>
#include <cstdint>
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

bool near(double left, double right) {
    const auto gap = left > right ? left - right : right - left;
    return gap < 0.001;
}

float height_for(veyra::LogFeedKind kind) {
    switch (kind) {
    case veyra::LogFeedKind::error:
        return 32.0F;
    case veyra::LogFeedKind::warning:
        return 24.0F;
    case veyra::LogFeedKind::info:
        break;
    }

    return 16.0F;
}

bool find_feed_row(void* context, std::uint64_t item_id, std::size_t& item_index) noexcept {
    const auto* feed = static_cast<const veyra::LogFeed*>(context);
    return feed->index_of(item_id, item_index);
}

void rebuild_index(
    const veyra::LogFeed& feed,
    std::array<float, veyra::LogFeed::kCapacity>& extents,
    std::array<double, veyra::LogFeed::kCapacity + 1>& fenwick,
    veyra::VirtualListIndex& index
) {
    for (auto cursor = std::size_t{}; cursor < feed.size(); ++cursor) {
        auto record = veyra::LogFeedRecord{};
        require(feed.record_at(cursor, record), "every live row reads back for layout");

        extents[cursor] = height_for(record.kind);
    }

    require(
        index.reset(
            std::span<float>{extents.data(), feed.size()},
            std::span<double>{fenwick.data(), feed.size() + 1}
        ),
        "the burst rebuilds the variable-height index"
    );
}

void check_burst_streams_through_pipeline_anchor_and_emit() {
    auto feed = veyra::LogFeed{};
    auto pipeline = veyra::ListPipeline{};
    auto now_ms = std::uint64_t{};
    auto next_id = std::uint64_t{1};

    constexpr std::array<veyra::LogFeedKind, 5> run_kinds{
        veyra::LogFeedKind::info,
        veyra::LogFeedKind::warning,
        veyra::LogFeedKind::error,
        veyra::LogFeedKind::info,
        veyra::LogFeedKind::warning,
    };

    for (const auto kind : run_kinds) {
        for (auto burst = 0; burst < 7; ++burst) {
            feed.push(next_id, kind, now_ms);
            ++next_id;
            now_ms += 100;
        }

        now_ms += 2000;
    }

    require(feed.size() == 5, "five same-kind runs fold into five rows");

    for (auto cursor = std::size_t{}; cursor < 5; ++cursor) {
        auto record = veyra::LogFeedRecord{};
        require(feed.record_at(cursor, record), "each folded row reads back");
        require(record.count == 7, "each run folded seven events into its count");
    }

    require(
        pipeline.request(veyra::ListLoadDirection::append, feed.size(), 16) ==
            veyra::ListPipelineResult::changed,
        "the tail page load starts"
    );
    require(pipeline.is_loading_anywhere(), "the tail request loads");
    require(pipeline.set_placeholders(0, 2), "two tail placeholders stage");

    auto request = veyra::ListLoadRequest{};
    require(
        pipeline.pending_request(veyra::ListLoadDirection::append, request),
        "the tail request stays pending while rows stream in"
    );
    require(
        pipeline.resolve(veyra::ListLoadDirection::append, request.generation + 1, true) ==
            veyra::ListPipelineResult::stale,
        "a mismatched generation resolves stale and keeps loading"
    );
    require(pipeline.is_loading_anywhere(), "the stale resolve loads on");

    for (auto burst = 0; burst < 300; ++burst) {
        const auto kind = burst % 2 == 0 ? veyra::LogFeedKind::info : veyra::LogFeedKind::warning;
        const auto pushed = feed.push(next_id, kind, now_ms);
        require(pushed.result == veyra::LogFeedResult::appended, "the flood appends row by row");

        ++next_id;
        now_ms += 2000;
    }

    require(feed.size() == veyra::LogFeed::kCapacity, "the flood pins the feed at capacity");
    require(feed.generation() == 335, "all thirty-five run pushes and flood appends bumped");

    auto slot = std::size_t{};
    require(!feed.index_of(79, slot), "the flood evicted the oldest rows first");
    require(feed.index_of(80, slot) && slot == 0, "the oldest survivor shifts to the head");

    require(
        pipeline.resolve(veyra::ListLoadDirection::append, request.generation, true) ==
            veyra::ListPipelineResult::changed,
        "the matching generation lands the tail page"
    );
    require(!pipeline.is_loading_anywhere(), "the landed page idles");
    require(pipeline.set_placeholders(0, 0), "landing clears the placeholders");
    require(!pipeline.should_show_placeholders(), "no pending rows means no shimmer");
    require(
        pipeline.set_complete(veyra::ListLoadDirection::append, true),
        "the landed tail marks its edge complete"
    );

    const auto quiet = pipeline.end_reached(250, 255, feed.size(), 8);
    require(!quiet.append && !quiet.prepend, "a complete edge reaches nothing more");

    std::array<float, veyra::LogFeed::kCapacity> extents{};
    std::array<double, veyra::LogFeed::kCapacity + 1> fenwick{};
    auto index = veyra::VirtualListIndex{};
    rebuild_index(feed, extents, fenwick, index);

    auto scroller = veyra::ScrollController{};
    require(scroller.configure(index.total_extent(), 64.0), "the scroller takes the burst total");
    require(
        scroller.set_offset(scroller.maximum_offset()) == veyra::ScrollResult::changed,
        "the feed starts pinned to its tail"
    );

    std::array<std::uint64_t, veyra::LogFeed::kCapacity> ids{};
    for (auto cursor = std::size_t{}; cursor < feed.size(); ++cursor) {
        auto record = veyra::LogFeedRecord{};
        require(feed.record_at(cursor, record), "every id resolves for anchor capture");

        ids[cursor] = record.id;
    }

    veyra::VirtualListAnchor anchor{};
    require(
        index.capture_anchor(
            std::span<const std::uint64_t>{ids.data(), feed.size()},
            scroller.offset(),
            scroller.viewport_extent(),
            scroller.generation(),
            anchor
        ),
        "the tail position captures with stable feed ids"
    );
    require(anchor.pinned_to_end, "a tail offset captures as pinned");

    auto held_offset = -1.0;
    require(
        index.restore_anchor(
            anchor,
            scroller.viewport_extent(),
            scroller.generation(),
            find_feed_row,
            &feed,
            held_offset
        ),
        "the feed id lookup restores the anchor before growth"
    );
    require(near(held_offset, scroller.offset()), "the undisturbed restore holds its offset");

    constexpr std::array<veyra::LogFeedKind, 3> tail_kinds{
        veyra::LogFeedKind::error,
        veyra::LogFeedKind::error,
        veyra::LogFeedKind::error,
    };

    for (const auto kind : tail_kinds) {
        feed.push(next_id, kind, now_ms);
        ++next_id;
        now_ms += 2000;
    }

    rebuild_index(feed, extents, fenwick, index);
    require(scroller.configure(index.total_extent(), 64.0), "the growth reconfigures the scroller");

    auto stale_offset = -1.0;
    require(
        !index.restore_anchor(
            anchor,
            scroller.viewport_extent(),
            scroller.generation(),
            find_feed_row,
            &feed,
            stale_offset
        ),
        "the generation gate refuses the pre-growth anchor"
    );
    require(near(stale_offset, -1.0), "a refused restore leaves the output alone");

    auto tail_offset = -1.0;
    require(
        index.restore_pinned_end(anchor, scroller.viewport_extent(), tail_offset),
        "the pinned helper restores across the growth"
    );
    require(
        scroller.set_offset(tail_offset) == veyra::ScrollResult::changed,
        "the scroller follows the restored tail"
    );
    require(scroller.at_end(), "the grown feed stays pinned to its tail");
    require(feed.index_of(80, slot) == false, "the growth evicted the previous head");
    require(feed.index_of(83, slot) && slot == 0, "the growth shifted the survivors up");

    veyra::VirtualListRange range{};
    require(
        index.visible_range(scroller.offset(), scroller.viewport_extent(), 1, range),
        "the tail window resolves to a visible range"
    );
    require(range.first_item < range.last_item, "the tail window shows rows");

    std::array<veyra::LogFeedRowState, veyra::LogFeed::kCapacity> states{};
    std::array<veyra::LayoutRect, veyra::LogFeed::kCapacity> badges{};

    auto collapsed = std::size_t{};
    auto base_offset = 0.0;
    require(
        index.offset_of(range.first_item, base_offset),
        "the slice base offsets from the index"
    );

    for (auto cursor = range.first_item; cursor < range.last_item; ++cursor) {
        auto record = veyra::LogFeedRecord{};
        require(feed.record_at(cursor, record), "each visible row reads back");

        const auto slot_row = cursor - range.first_item;
        states[slot_row].kind = record.kind;
        states[slot_row].collapsed = record.count > 1;
        states[slot_row].placeholder = pipeline.should_show_placeholders();

        if (!states[slot_row].collapsed) {
            continue;
        }

        auto row_offset = 0.0;
        require(index.offset_of(cursor, row_offset), "each visible row offsets from the index");

        badges[slot_row] = {
            .x = 148.0F,
            .y = static_cast<float>(row_offset - base_offset) + 2.0F,
            .width = 44.0F,
            .height = 12.0F,
        };
        ++collapsed;
    }

    const auto visible = range.last_item - range.first_item;
    const veyra::VirtualListRowsGeometry geometry{
        .list_bounds = {.x = 0.0F, .y = 0.0F, .width = 200.0F, .height = 64.0F},
        .row_height = 24.0F,
    };
    std::array<veyra::DisplayCommand, veyra::LogFeed::kCapacity * 2> commands{};

    const auto theme = veyra::Theme{};
    const auto output = veyra::emit_log_feed_rows(
        theme,
        std::span<const veyra::LogFeedRowState>{states.data(), visible},
        std::span<const veyra::LayoutRect>{badges.data(), visible},
        geometry,
        commands
    );

    require(output.result == veyra::LogFeedEmitResult::complete, "the tail slice emits");
    require(output.required == output.placed, "the sized span fits the whole slice");

    auto pills = std::size_t{};
    for (auto cursor = std::size_t{}; cursor < output.placed; ++cursor) {
        const auto* pill = std::get_if<veyra::FillRoundedRect>(&commands[cursor]);

        if (pill == nullptr) {
            continue;
        }

        ++pills;
    }

    require(pills == collapsed, "one badge pill lands per collapsed row, none for plain rows");

    const auto before = feed.generation();

    auto tail_before = veyra::LogFeedRecord{};
    require(feed.record_at(feed.size() - 1, tail_before), "the tail reads back before the fold");

    const auto folded = feed.push(next_id, veyra::LogFeedKind::error, tail_before.last_ms + 100);
    require(folded.result == veyra::LogFeedResult::collapsed, "a tail repeat folds after the emit");
    require(feed.generation() == before + 1, "the fold bumps the invalidation generation");

    auto tail = veyra::LogFeedRecord{};
    require(
        feed.record_at(feed.size() - 1, tail),
        "the tail re-derives after its generation moved"
    );
    require(tail.count == 2 && tail.id == next_id, "the re-derived tail carries the folded count");
}

void check_loading_placeholders_emit_shimmer() {
    auto pipeline = veyra::ListPipeline{};

    require(
        pipeline.request(veyra::ListLoadDirection::append, 0, 2) ==
            veyra::ListPipelineResult::changed,
        "the first page load starts"
    );
    require(pipeline.set_placeholders(0, 2), "two tail placeholders stage");

    const auto theme = veyra::Theme{};
    constexpr std::array<veyra::LogFeedRowState, 2> rows{{
        {.kind = veyra::LogFeedKind::info, .placeholder = true},
        {.kind = veyra::LogFeedKind::error, .placeholder = true},
    }};
    const veyra::VirtualListRowsGeometry geometry{
        .list_bounds = {.x = 0.0F, .y = 0.0F, .width = 160.0F, .height = 48.0F},
        .row_height = 24.0F,
    };
    std::array<veyra::DisplayCommand, 8> commands{};

    const auto output = veyra::emit_log_feed_rows(theme, rows, {}, geometry, commands);

    require(output.result == veyra::LogFeedEmitResult::complete, "placeholder rows emit");
    require(output.placed == 4 && output.required == 4, "each shimmer row adds one fill");

    const auto* shimmer = std::get_if<veyra::FillRect>(&commands[0]);
    require(shimmer != nullptr, "the placeholder opens with its shimmer fill");
    require(
        shimmer->color == theme.color(veyra::tokens::color_action_disabled),
        "the shimmer fill pins the disabled color"
    );
}

}

int main() {
    try {
        check_burst_streams_through_pipeline_anchor_and_emit();
        check_loading_placeholders_emit_shimmer();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
