#include "veyra/layout/virtual_list.hpp"

#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

void require(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

bool near(double actual, double expected) {
    return std::abs(actual - expected) < 0.001;
}

struct ItemIdLookupContext {
    std::span<const std::uint64_t> item_ids;
};

bool find_item_index(void* context, std::uint64_t item_id, std::size_t& item_index) noexcept {
    const auto& ids = static_cast<ItemIdLookupContext*>(context)->item_ids;
    for (std::size_t index = 0; index < ids.size(); ++index) {
        if (ids[index] == item_id) {
            item_index = index;
            return true;
        }
    }
    return false;
}

void check_variable_extent_queries() {
    std::array extents{10.0F, 20.0F, 0.0F, 15.0F, 5.0F};
    std::array<double, 6> fenwick{};
    veyra::VirtualListIndex index;
    require(index.reset(extents, fenwick), "valid extents initialize the index");
    require(index.size() == extents.size(), "index reports item count");
    require(near(index.total_extent(), 50.0), "index reports total extent");

    double offset = -1.0;
    require(index.offset_of(3, offset), "item offset is available");
    require(near(offset, 30.0), "offset includes preceding measured extents");
    require(!index.offset_of(extents.size() + 1, offset), "past-end offset is rejected");

    veyra::VirtualListRange range;
    require(index.visible_range(10.0, 20.0, 1, range), "visible range query succeeds");
    require(range.first_item == 0 && range.last_item == 3, "overscan includes surrounding rows");
    require(
        near(range.first_offset, 0.0) && near(range.total_extent, 50.0),
        "range reports geometry"
    );

    require(index.visible_range(30.0, 15.0, 0, range), "boundary range query succeeds");
    require(range.first_item == 3 && range.last_item == 4, "exact boundaries exclude prior rows");
    require(near(range.first_offset, 30.0), "boundary range begins at its first row");

    require(index.visible_range(50.0, 10.0, 2, range), "end-of-list query succeeds");
    require(range.first_item == 5 && range.last_item == 5, "end offset returns an empty range");
}

void check_measurement_updates_and_failures() {
    std::array extents{10.0F, 20.0F, 30.0F};
    std::array<double, 4> fenwick{};
    veyra::VirtualListIndex index;
    require(index.reset(extents, fenwick), "valid extents initialize the index");

    double delta = 0.0;
    require(index.set_extent(0, 14.0F, delta), "measured height updates the index");
    require(
        near(delta, 4.0) && near(index.total_extent(), 64.0),
        "update exposes anchor correction"
    );

    double offset = 0.0;
    require(index.offset_of(2, offset), "updated offset is available");
    require(near(offset, 34.0), "updated height changes later offsets");

    const auto previous_total = index.total_extent();
    require(
        !index.set_extent(1, std::numeric_limits<float>::infinity(), delta),
        "non-finite measurement is rejected"
    );
    require(
        near(delta, 0.0) && near(index.total_extent(), previous_total),
        "failed update is atomic"
    );

    veyra::VirtualListRange range{.first_item = 7};
    require(!index.visible_range(-1.0, 10.0, 0, range), "negative scroll is rejected");
    require(range.first_item == 7, "failed query leaves range unchanged");
}

void check_scroll_anchor_after_measurement_changes() {
    std::array extents{10.0F, 20.0F, 0.0F, 15.0F, 5.0F};
    std::array<double, 6> fenwick{};
    veyra::VirtualListIndex index;
    require(index.reset(extents, fenwick), "anchor extents initialize the index");

    veyra::VirtualListAnchor anchor;
    require(index.capture_anchor(35.0, 0.0, anchor), "visible row anchor is captured");
    require(
        anchor.item_index == 3 && near(anchor.item_offset, 5.0),
        "anchor stores the first visible row and its local offset"
    );

    veyra::VirtualListAnchor boundary_anchor;
    require(
        index.capture_anchor(30.0, 0.0, boundary_anchor),
        "anchor captures exact row boundaries"
    );
    require(
        boundary_anchor.item_index == 3 && near(boundary_anchor.item_offset, 0.0),
        "zero-height rows at a boundary are skipped"
    );

    const auto previous_anchor = anchor;
    require(
        !index.capture_anchor(index.total_extent() + 1.0, 0.0, anchor),
        "scroll offsets past the list are rejected"
    );
    require(
        anchor.item_index == previous_anchor.item_index &&
            near(anchor.item_offset, previous_anchor.item_offset) &&
            anchor.pinned_to_end == previous_anchor.pinned_to_end,
        "failed anchor capture leaves output unchanged"
    );

    double delta = 0.0;
    require(index.set_extent(0, 14.0F, delta), "row before anchor can be remeasured");
    require(index.set_extent(3, 8.0F, delta), "anchored row can be remeasured");
    double restored_offset = -1.0;
    require(
        index.restore_anchor(anchor, 0.0, restored_offset),
        "anchor restores after measurements"
    );
    require(
        near(restored_offset, 39.0),
        "restore keeps the same row offset after earlier rows change"
    );

    require(index.set_extent(3, 2.0F, delta), "anchored row can shrink below its old offset");
    require(index.restore_anchor(anchor, 0.0, restored_offset), "shrunk anchor still restores");
    require(near(restored_offset, 36.0), "local offset clamps to the remeasured row extent");

    veyra::VirtualListAnchor pinned_anchor;
    const auto bottom_scroll_offset = index.total_extent() - 10.0;
    require(
        index.capture_anchor(bottom_scroll_offset, 10.0, pinned_anchor),
        "bottom-pinned scroll position can be captured"
    );
    require(pinned_anchor.pinned_to_end, "anchor records the pinned-to-end state");

    std::array appended_extents{14.0F, 20.0F, 0.0F, 2.0F, 5.0F, 9.0F};
    std::array<double, 7> appended_fenwick{};
    require(index.reset(appended_extents, appended_fenwick), "appended rows rebuild the index");
    require(index.restore_anchor(pinned_anchor, 10.0, restored_offset), "bottom anchor restores");
    require(
        near(restored_offset, index.total_extent() - 10.0),
        "appending a row keeps a bottom-pinned list at the end"
    );

    auto invalid_anchor = anchor;
    invalid_anchor.item_index = index.size() + 1;
    const auto previous_offset = restored_offset;
    require(
        !index.restore_anchor(invalid_anchor, 10.0, restored_offset),
        "past-end anchors are rejected"
    );
    require(
        near(restored_offset, previous_offset),
        "failed anchor restore leaves output unchanged"
    );
    require(
        !index.restore_anchor(anchor, -1.0, restored_offset),
        "negative viewports are rejected"
    );
    require(near(restored_offset, previous_offset), "rejected viewports leave output unchanged");
}

void check_restore_anchor_clamps_after_content_shrinks() {
    std::array extents{10.0F, 20.0F, 30.0F, 40.0F};
    std::array<double, 5> fenwick{};
    veyra::VirtualListIndex index;
    require(index.reset(extents, fenwick), "scroll range extents initialize the index");

    veyra::VirtualListAnchor anchor;
    require(index.capture_anchor(45.0, 20.0, anchor), "anchor within the list is captured");
    require(!anchor.pinned_to_end, "anchor away from the end remains unpinned");

    std::array shorter_extents{10.0F, 20.0F, 25.0F};
    std::array<double, 4> shorter_fenwick{};
    require(index.reset(shorter_extents, shorter_fenwick), "shorter list rebuilds the index");

    double restored_offset = -1.0;
    require(index.restore_anchor(anchor, 20.0, restored_offset), "anchor restores after removal");
    require(
        near(restored_offset, 35.0),
        "restored offset stays within the shorter list's scrollable range"
    );
}

void check_stable_anchor_survives_prepend() {
    std::array extents{10.0F, 20.0F, 30.0F, 40.0F};
    std::array<double, 5> fenwick{};
    std::array<std::uint64_t, 4> item_ids{101, 102, 103, 104};
    veyra::VirtualListIndex index;
    require(index.reset(extents, fenwick), "stable-anchor extents initialize the index");

    veyra::VirtualListAnchor anchor;
    require(
        index.capture_anchor(item_ids, 35.0, 20.0, anchor),
        "stable anchor captures its message ID"
    );
    require(
        anchor.item_index == 2 && anchor.item_id == 103 && near(anchor.item_offset, 5.0),
        "stable anchor stores identity and local row offset"
    );

    std::array prepended_extents{5.0F, 10.0F, 20.0F, 30.0F, 40.0F};
    std::array<double, 6> prepended_fenwick{};
    std::array<std::uint64_t, 5> prepended_ids{99, 101, 102, 103, 104};
    require(index.reset(prepended_extents, prepended_fenwick), "prepended rows rebuild the index");

    auto lookup_context = ItemIdLookupContext{prepended_ids};
    auto restored_offset = -1.0;
    require(
        index.restore_anchor(anchor, 20.0, find_item_index, &lookup_context, restored_offset),
        "stable anchor resolves after prepend"
    );
    require(
        near(restored_offset, 40.0),
        "prepend keeps the anchored row at the same viewport position"
    );

    const auto previous_offset = restored_offset;
    std::array<std::uint64_t, 2> ids_without_anchor{99, 101};
    lookup_context.item_ids = ids_without_anchor;
    require(
        !index.restore_anchor(anchor, 20.0, find_item_index, &lookup_context, restored_offset),
        "a removed anchor ID fails lookup"
    );
    require(near(restored_offset, previous_offset), "missing IDs leave scroll output unchanged");
    require(
        !index.restore_anchor(anchor, 20.0, nullptr, &lookup_context, restored_offset),
        "a null lookup cannot resolve rows"
    );
    require(near(restored_offset, previous_offset), "null lookups leave scroll output unchanged");

    auto anonymous = anchor;
    anonymous.item_id = 0;
    require(
        !index.restore_anchor(anonymous, 20.0, find_item_index, &lookup_context, restored_offset),
        "an anonymous anchor cannot resolve rows"
    );
    require(near(restored_offset, previous_offset), "anonymous anchors leave output unchanged");
}

void check_stable_anchor_rejects_invalid_ids() {
    std::array extents{10.0F, 20.0F};
    std::array<double, 3> fenwick{};
    std::array<std::uint64_t, 2> item_ids{0, 202};
    veyra::VirtualListIndex index;
    require(index.reset(extents, fenwick), "invalid-ID extents initialize the index");

    veyra::VirtualListAnchor anchor{.item_id = 777};
    require(!index.capture_anchor(item_ids, 5.0, 0.0, anchor), "zero IDs cannot anchor rows");
    require(anchor.item_id == 777, "failed stable capture leaves its output unchanged");

    std::array<std::uint64_t, 1> short_ids{201};
    require(
        !index.capture_anchor(short_ids, 5.0, 0.0, anchor),
        "identity storage must match the row count"
    );
    require(anchor.item_id == 777, "mismatched identity storage leaves output unchanged");

    std::array<std::uint64_t, 2> full_ids{201, 202};
    require(
        !index.capture_anchor(full_ids, index.total_extent() + 1.0, 0.0, anchor),
        "scroll offsets past the list cannot anchor rows"
    );
    require(anchor.item_id == 777, "out-of-range capture leaves output unchanged");
}

void check_scroll_item_into_view() {
    std::array extents{10.0F, 20.0F, 30.0F, 50.0F, 0.0F};
    std::array<double, 6> fenwick{};
    veyra::VirtualListIndex index;
    require(index.reset(extents, fenwick), "scroll target extents initialize the index");

    auto scroll_offset = -1.0;
    require(
        index.scroll_item_into_view(1, 25.0, 0.0, scroll_offset),
        "partly clipped rows can be revealed"
    );
    require(near(scroll_offset, 5.0), "a row below the viewport aligns its bottom edge");

    require(
        index.scroll_item_into_view(0, 25.0, 30.0, scroll_offset),
        "rows above the viewport can be revealed"
    );
    require(near(scroll_offset, 0.0), "a row above the viewport aligns its top edge");

    require(
        index.scroll_item_into_view(2, 25.0, 0.0, scroll_offset),
        "rows larger than the viewport can be revealed"
    );
    require(near(scroll_offset, 30.0), "an offscreen oversized row aligns at its start");
    require(
        index.scroll_item_into_view(3, 20.0, 65.0, scroll_offset),
        "visible oversized rows keep their current position"
    );
    require(near(scroll_offset, 65.0), "an oversized visible row is not repeatedly moved");
    require(
        index.scroll_item_into_view(3, 0.0, 0.0, scroll_offset),
        "zero-sized viewports can still target rows"
    );
    require(near(scroll_offset, 60.0), "zero-sized viewports align a row to its start");

    require(
        index.scroll_item_into_view(4, 20.0, 65.0, scroll_offset),
        "collapsed rows can be queried"
    );
    require(near(scroll_offset, 65.0), "collapsed rows do not move the viewport");

    require(
        index.scroll_item_into_view(4, 20.0, 1'000.0, scroll_offset),
        "stale offsets can be corrected after content changes"
    );
    require(near(scroll_offset, 90.0), "stale offsets clamp to the current scroll range");

    const auto previous_offset = scroll_offset;
    require(
        !index.scroll_item_into_view(index.size(), 20.0, 0.0, scroll_offset),
        "past-end item indices are rejected"
    );
    require(
        !index.scroll_item_into_view(0, -1.0, 0.0, scroll_offset),
        "negative viewport extents are rejected"
    );
    require(
        !index.scroll_item_into_view(
            0,
            20.0,
            std::numeric_limits<double>::infinity(),
            scroll_offset
        ),
        "non-finite current offsets are rejected"
    );
    require(near(scroll_offset, previous_offset), "invalid requests leave output unchanged");
}

void check_reset_rejects_invalid_storage_and_extents() {
    veyra::VirtualListIndex index;
    std::array extents{5.0F, 10.0F};
    std::array<double, 2> short_fenwick{17.0, 19.0};
    require(!index.reset(extents, short_fenwick), "short index storage is rejected");
    require(
        near(short_fenwick[0], 17.0) && near(short_fenwick[1], 19.0),
        "short storage is unchanged"
    );

    std::array<double, 3> fenwick{23.0, 29.0, 31.0};
    extents[1] = -1.0F;
    require(!index.reset(extents, fenwick), "negative extents are rejected");
    require(
        near(fenwick[0], 23.0) && near(fenwick[1], 29.0),
        "invalid extents leave storage unchanged"
    );
    require(index.size() == 0, "failed reset leaves index empty");
}

void check_ranges_match_linear_reference() {
    std::array extents{0.0F, 4.0F, 7.0F, 0.0F, 3.0F, 8.0F};
    std::array<double, 7> fenwick{};
    veyra::VirtualListIndex index;
    require(index.reset(extents, fenwick), "reference extents initialize the index");

    for (std::size_t scroll_tick = 0; scroll_tick <= 46; ++scroll_tick) {
        const auto scroll = static_cast<double>(scroll_tick) * 0.5;
        for (std::size_t viewport_tick = 0; viewport_tick <= 20; ++viewport_tick) {
            const auto viewport = static_cast<double>(viewport_tick) * 0.5;
            for (std::size_t overscan = 0; overscan <= 2; ++overscan) {
                veyra::VirtualListRange range;
                require(
                    index.visible_range(scroll, viewport, overscan, range),
                    "range query succeeds"
                );

                auto visible_first = std::size_t{0};
                auto prefix = 0.0;
                while (visible_first < extents.size()) {
                    prefix += extents[visible_first];
                    if (prefix > scroll) {
                        break;
                    }
                    ++visible_first;
                }

                auto visible_last = visible_first;
                const bool has_viewport = viewport > 0.0 && scroll < index.total_extent();
                if (has_viewport) {
                    const auto end = std::min(index.total_extent(), scroll + viewport);
                    auto ended_before = std::size_t{0};
                    prefix = 0.0;
                    for (const auto extent : extents) {
                        prefix += extent;
                        if (prefix >= end) {
                            break;
                        }
                        ++ended_before;
                    }
                    visible_last = std::min(extents.size(), ended_before + 1);
                }

                const auto expected_first = has_viewport
                    ? (visible_first > overscan ? visible_first - overscan : 0)
                    : visible_first;
                const auto expected_last = has_viewport
                    ? std::min(
                          extents.size(),
                          visible_last + std::min(overscan, extents.size() - visible_last)
                      )
                    : visible_last;
                auto expected_offset = 0.0;
                for (std::size_t item = 0; item < expected_first; ++item) {
                    expected_offset += extents[item];
                }

                if (range.first_item != expected_first || range.last_item != expected_last) {
                    throw std::runtime_error(
                        "range mismatch at scroll=" + std::to_string(scroll) + " viewport=" +
                        std::to_string(viewport) + " overscan=" + std::to_string(overscan) +
                        " actual=" + std::to_string(range.first_item) + ":" +
                        std::to_string(range.last_item) + " expected=" +
                        std::to_string(expected_first) + ":" + std::to_string(expected_last)
                    );
                }
                require(
                    near(range.first_offset, expected_offset),
                    "indexed offset matches reference"
                );
            }
        }
    }
}

}

int main() {
    try {
        check_variable_extent_queries();
        check_measurement_updates_and_failures();
        check_scroll_anchor_after_measurement_changes();
        check_restore_anchor_clamps_after_content_shrinks();
        check_stable_anchor_survives_prepend();
        check_stable_anchor_rejects_invalid_ids();
        check_scroll_item_into_view();
        check_reset_rejects_invalid_storage_and_extents();
        check_ranges_match_linear_reference();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
