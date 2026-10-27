#include "veyra/behaviors/control/scroll.hpp"
#include "veyra/layout/scroll/virtual_list.hpp"

#include <array>
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

void check_pinned_anchor_survives_content_growth() {
    std::array first_extents{10.0F, 10.0F, 10.0F, 10.0F};
    std::array<double, 5> first_fenwick{};
    auto index = veyra::VirtualListIndex{};
    require(index.reset(first_extents, first_fenwick), "the first rows build the index");

    auto scroller = veyra::ScrollController{};
    require(scroller.configure(index.total_extent(), 20.0), "the scroller takes the total");
    require(
        scroller.set_offset(scroller.maximum_offset()) == veyra::ScrollResult::changed,
        "the chat starts pinned to its tail"
    );

    veyra::VirtualListAnchor anchor{};
    require(
        index.capture_anchor(
            scroller.offset(),
            scroller.viewport_extent(),
            scroller.generation(),
            anchor
        ),
        "the tail position captures"
    );
    require(anchor.pinned_to_end, "a tail offset captures as pinned");

    std::array grown_extents{10.0F, 10.0F, 10.0F, 10.0F, 10.0F, 10.0F};
    std::array<double, 7> grown_fenwick{};
    require(index.reset(grown_extents, grown_fenwick), "new messages rebuild the index");
    require(scroller.configure(index.total_extent(), 20.0), "growth reconfigures the scroller");

    auto stale_offset = -1.0;
    require(
        !index.restore_anchor(
            anchor,
            scroller.viewport_extent(),
            scroller.generation(),
            stale_offset
        ),
        "the generation gate refuses the pre-growth anchor"
    );
    require(near(stale_offset, -1.0), "a refused restore leaves the output alone");
    require(!scroller.at_end(), "the scroller sits above the grown tail");

    auto tail_offset = -1.0;
    require(
        index.restore_pinned_end(anchor, scroller.viewport_extent(), tail_offset),
        "the pinned helper restores across the growth"
    );
    require(near(tail_offset, 40.0), "the pinned helper targets the grown tail");
    require(
        scroller.set_offset(tail_offset) == veyra::ScrollResult::changed,
        "the scroller follows the restored tail"
    );
    require(scroller.at_end(), "the grown chat stays pinned to its tail");
}

void check_pinned_helper_rejects_unpinned_anchors() {
    std::array extents{10.0F, 10.0F, 10.0F, 10.0F};
    std::array<double, 5> fenwick{};
    auto index = veyra::VirtualListIndex{};
    require(index.reset(extents, fenwick), "scroll range extents build the index");

    veyra::VirtualListAnchor anchor{};
    require(index.capture_anchor(5.0, 20.0, 7, anchor), "a mid-list offset captures");
    require(!anchor.pinned_to_end, "a mid-list offset stays unpinned");

    auto held_offset = 5.0;
    require(
        !index.restore_pinned_end(anchor, 20.0, held_offset),
        "an unpinned anchor cannot use the tail helper"
    );
    require(near(held_offset, 5.0), "a refused tail restore leaves the output alone");

    veyra::VirtualListAnchor pinned{};
    require(index.capture_anchor(20.0, 20.0, 7, pinned), "a tail offset captures");
    require(pinned.pinned_to_end, "a tail offset captures as pinned");
    require(
        !index.restore_pinned_end(pinned, -1.0, held_offset),
        "a negative viewport is rejected"
    );
    require(near(held_offset, 5.0), "a rejected viewport leaves the output alone");
    require(
        index.restore_pinned_end(pinned, 20.0, held_offset),
        "a pinned anchor restores without a generation"
    );
    require(near(held_offset, 20.0), "the tail helper clamps to the scrollable end");
}

}

int main() {
    try {
        check_pinned_anchor_survives_content_growth();
        check_pinned_helper_rejects_unpinned_anchors();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
