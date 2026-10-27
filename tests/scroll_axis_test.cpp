#include "veyra/layout/scroll/scroll_axis.hpp"

#include <cmath>
#include <cstdint>
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

bool near(double left, double right) noexcept {
    return std::abs(left - right) < 0.0001;
}

void check_extents_and_offset_clamping() {
    auto axis = veyra::ScrollAxis{};
    require(axis.set_extents(500.0, 100.0), "valid extents are accepted");
    require(near(axis.maximum_offset(), 400.0), "maximum offset is content minus viewport");
    require(axis.set_offset(450.0), "finite offsets are accepted");
    require(near(axis.offset(), 400.0), "offsets clamp to the end of the content");

    require(axis.set_extents(80.0, 100.0), "smaller content extent is accepted");
    require(near(axis.offset(), 0.0), "shrinking content clamps the existing offset");
    require(near(axis.maximum_offset(), 0.0), "short content has no scroll range");
    require(near(axis.content_extent(), 80.0), "content extent is retained");
    require(near(axis.viewport_extent(), 100.0), "viewport extent is retained");

    auto unconsumed = 0.0;
    require(axis.scroll_by(12.0, unconsumed), "motion is accepted when content fits");
    require(near(axis.offset(), 0.0), "short content stays at its leading edge");
    require(near(unconsumed, 12.0), "short content passes all motion to its parent");
}

void check_scroll_consumes_only_available_motion() {
    auto axis = veyra::ScrollAxis{};
    require(axis.set_extents(200.0, 100.0), "scroll extents are accepted");
    require(axis.set_offset(90.0), "offset near the end is accepted");

    auto unconsumed = -1.0;
    require(axis.scroll_by(25.0, unconsumed), "forward scroll delta is accepted");
    require(near(axis.offset(), 100.0), "scroll stops at the maximum offset");
    require(near(unconsumed, 15.0), "motion beyond the end remains available to a parent");

    require(axis.scroll_by(-130.0, unconsumed), "backward scroll delta is accepted");
    require(near(axis.offset(), 0.0), "scroll stops at the leading edge");
    require(near(unconsumed, -30.0), "motion beyond the leading edge remains available");
}

void check_nested_scroll_chaining() {
    auto child = veyra::ScrollAxis{};
    auto parent = veyra::ScrollAxis{};
    require(child.set_extents(500.0, 100.0), "child extents are accepted");
    require(parent.set_extents(1'000.0, 400.0), "parent extents are accepted");
    require(child.set_offset(child.maximum_offset() - 5.0), "child reaches near its end");

    auto remaining = 0.0;
    require(child.scroll_by(20.0, remaining), "child handles the incoming scroll");
    require(near(child.offset(), child.maximum_offset()), "child consumes its remaining range");
    require(near(remaining, 15.0), "child returns motion it could not consume");
    require(parent.scroll_by(remaining, remaining), "parent consumes the child's remainder");
    require(near(parent.offset(), 15.0), "parent advances by the remaining motion");
    require(near(remaining, 0.0), "fully handled motion leaves no remainder");
}

void check_generation_bumps_only_on_committed_change() {
    auto axis = veyra::ScrollAxis{};
    require(axis.generation() == 0, "a fresh axis starts at generation zero");

    require(axis.set_extents(500.0, 100.0), "initial geometry is accepted");
    require(axis.generation() == 1, "the first geometry change bumps the generation");

    require(axis.set_extents(500.0, 100.0), "identical geometry is accepted");
    require(axis.generation() == 1, "a no-op configure does not bump");

    require(axis.set_offset(0.0), "repeating the offset is accepted");
    require(axis.generation() == 1, "a no-op offset set does not bump");

    require(axis.set_offset(50.0), "a moved offset is accepted");
    require(axis.generation() == 2, "a committed offset bumps the generation");

    require(axis.set_offset(50.0), "repeating the moved offset is accepted");
    require(axis.generation() == 2, "repeating the offset does not bump again");

    auto unconsumed = 0.0;
    require(axis.scroll_by(0.0, unconsumed), "a zero delta is accepted");
    require(axis.generation() == 2, "a zero delta does not bump");

    require(axis.set_offset(axis.maximum_offset()), "the end offset is accepted");
    const auto at_end = axis.generation();
    require(axis.scroll_by(40.0, unconsumed), "pushing past the end is accepted");
    require(axis.generation() == at_end, "clamped pushing past the edge does not bump");
    require(near(unconsumed, 40.0), "the whole push past the edge stays unconsumed");

    require(axis.set_extents(80.0, 100.0), "smaller content is accepted");
    require(axis.generation() == at_end + 1, "a clamped shrink bumps exactly once");

    const auto settled = axis.generation();
    require(!axis.set_extents(-1.0, 10.0), "negative content is rejected");
    require(
        !axis.set_offset(std::numeric_limits<double>::quiet_NaN()),
        "a non-finite offset is rejected"
    );
    require(
        !axis.scroll_by(std::numeric_limits<double>::infinity(), unconsumed),
        "a non-finite delta is rejected"
    );
    require(axis.generation() == settled, "rejected input never bumps");
}

void check_generation_comparison_survives_wrap() {
    auto axis = veyra::ScrollAxis{};
    require(axis.set_extents(500.0, 100.0), "wrap geometry is accepted");

    axis.set_generation(std::numeric_limits<std::uint64_t>::max() - 1);
    require(axis.set_offset(50.0), "a moved offset is accepted");
    require(
        axis.generation() == std::numeric_limits<std::uint64_t>::max(),
        "bumps approach the maximum without wrapping early"
    );

    require(axis.set_offset(60.0), "another moved offset is accepted");
    require(axis.generation() == 0, "the counter wraps to zero past the maximum");

    require(
        veyra::same_scroll_generation(axis.generation(), 0),
        "the wrapped value matches itself"
    );
    require(
        !veyra::same_scroll_generation(
            axis.generation(),
            std::numeric_limits<std::uint64_t>::max()
        ),
        "the pre-wrap value reads as stale after wrapping"
    );
    require(
        !veyra::same_scroll_generation(
            std::numeric_limits<std::uint64_t>::max(),
            std::numeric_limits<std::uint64_t>::max() - 1
        ),
        "adjacent values never compare equal"
    );
}

void check_invalid_inputs_leave_state_and_output_unchanged() {
    auto axis = veyra::ScrollAxis{};
    require(axis.set_extents(200.0, 100.0), "initial extents are accepted");
    require(axis.set_offset(25.0), "initial offset is accepted");

    const auto previous_content = axis.content_extent();
    const auto previous_viewport = axis.viewport_extent();
    const auto previous_offset = axis.offset();
    require(!axis.set_extents(-1.0, 10.0), "negative extents are rejected");
    require(
        !axis.set_extents(10.0, std::numeric_limits<double>::infinity()),
        "non-finite extents are rejected"
    );
    require(
        !axis.set_offset(std::numeric_limits<double>::quiet_NaN()),
        "non-finite offsets are rejected"
    );

    auto unconsumed = 42.0;
    require(
        !axis.scroll_by(std::numeric_limits<double>::infinity(), unconsumed),
        "non-finite scroll deltas are rejected"
    );
    require(
        near(axis.content_extent(), previous_content) &&
            near(axis.viewport_extent(), previous_viewport) && near(axis.offset(), previous_offset),
        "invalid input leaves scroll state unchanged"
    );
    require(near(unconsumed, 42.0), "invalid deltas leave output unchanged");
}

}

int main() {
    try {
        check_extents_and_offset_clamping();
        check_scroll_consumes_only_available_motion();
        check_nested_scroll_chaining();
        check_generation_bumps_only_on_committed_change();
        check_generation_comparison_survives_wrap();
        check_invalid_inputs_leave_state_and_output_unchanged();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
