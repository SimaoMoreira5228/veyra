#include "veyra/text/editing/editor_viewport.hpp"

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

bool near(float actual, float expected) {
    return std::abs(actual - expected) < 0.001F;
}

veyra::EditorViewport make_viewport() {
    veyra::EditorViewport viewport;

    require(
        viewport.set_total_lines(100) == veyra::EditorViewportResult::changed,
        "total lines stick"
    );
    require(
        viewport.set_line_height(10.0F) == veyra::EditorViewportResult::changed,
        "line height sticks"
    );
    require(
        viewport.set_view_height(60.0F) == veyra::EditorViewportResult::changed,
        "view height sticks"
    );

    return viewport;
}

void check_defaults_and_geometry() {
    veyra::EditorViewport viewport;

    require(viewport.total_lines() == 0, "fresh viewport names no lines");
    require(viewport.first_line() == 0, "fresh viewport starts at the first line");
    require(viewport.offset_in_first() == 0.0F, "fresh viewport carries no sub-line offset");
    require(viewport.visible_count() == 0, "fresh viewport shows nothing");
    require(viewport.generation() != 0, "generation never starts at zero");

    std::size_t first = 99;
    std::size_t count = 99;
    viewport.visible_range(first, count);
    require(first == 0 && count == 0, "empty document reports an empty range");

    require(
        viewport.scroll_to_line(4) == veyra::EditorViewportResult::ignored,
        "scrolling an empty document is a no-op"
    );
    require(
        viewport.scroll_by_px(20.0F) == veyra::EditorViewportResult::ignored,
        "pixel scroll on an empty document is a no-op"
    );
    require(
        viewport.ensure_visible(4, 1) == veyra::EditorViewportResult::ignored,
        "ensure on an empty document is a no-op"
    );
}

void check_geometry_validation() {
    veyra::EditorViewport viewport = make_viewport();
    const auto generation = viewport.generation();

    require(
        viewport.set_line_height(10.0F) == veyra::EditorViewportResult::handled,
        "repeating the line height is accepted without change"
    );
    require(
        viewport.set_view_height(60.0F) == veyra::EditorViewportResult::handled,
        "repeating the view height is accepted without change"
    );
    require(
        viewport.set_total_lines(100) == veyra::EditorViewportResult::handled,
        "repeating the total is accepted without change"
    );
    require(viewport.generation() == generation, "accepted no-ops leave the generation alone");

    const float quiet_nan = std::numeric_limits<float>::quiet_NaN();
    const float infinity = std::numeric_limits<float>::infinity();

    require(
        viewport.set_line_height(0.0F) == veyra::EditorViewportResult::invalid_input,
        "zero line height is rejected"
    );
    require(
        viewport.set_line_height(-4.0F) == veyra::EditorViewportResult::invalid_input,
        "negative line height is rejected"
    );
    require(
        viewport.set_line_height(quiet_nan) == veyra::EditorViewportResult::invalid_input,
        "NaN line height is rejected"
    );
    require(
        viewport.set_line_height(infinity) == veyra::EditorViewportResult::invalid_input,
        "infinite line height is rejected"
    );
    require(
        viewport.set_view_height(0.0F) == veyra::EditorViewportResult::invalid_input,
        "zero view height is rejected"
    );
    require(
        viewport.set_view_height(quiet_nan) == veyra::EditorViewportResult::invalid_input,
        "NaN view height is rejected"
    );
    require(
        viewport.set_total_lines(90, 91) == veyra::EditorViewportResult::invalid_input,
        "prepend past the new total is rejected"
    );

    require(viewport.line_height() == 10.0F, "rejected input leaves the line height alone");
    require(viewport.view_height() == 60.0F, "rejected input leaves the view height alone");
    require(viewport.total_lines() == 100, "rejected input leaves the total alone");
    require(viewport.generation() == generation, "rejected input leaves the generation alone");
}

void check_scroll_and_pixel_offset() {
    veyra::EditorViewport viewport = make_viewport();
    const auto generation = viewport.generation();

    require(
        viewport.scroll_to_line(10) == veyra::EditorViewportResult::changed,
        "line scroll lands"
    );
    require(viewport.first_line() == 10, "line scroll names the line");
    require(near(viewport.offset_in_first(), 0.0F), "line scroll carries no sub-line offset");
    require(viewport.generation() != generation, "line scroll bumps the generation");

    require(
        viewport.scroll_to_line(10) == veyra::EditorViewportResult::ignored,
        "scrolling to the current line is a no-op"
    );

    require(
        viewport.scroll_by_px(25.0F) == veyra::EditorViewportResult::changed,
        "pixel scroll lands"
    );
    require(viewport.first_line() == 12, "pixel scroll crosses whole lines");
    require(near(viewport.offset_in_first(), 5.0F), "pixel scroll keeps the remainder");

    require(
        viewport.scroll_by_px(0.0F) == veyra::EditorViewportResult::ignored,
        "zero pixel scroll is a no-op"
    );
    require(
        viewport.scroll_by_px(std::numeric_limits<float>::quiet_NaN()) ==
            veyra::EditorViewportResult::invalid_input,
        "non-finite pixel scroll is rejected"
    );

    require(
        viewport.scroll_by_px(-100000.0F) == veyra::EditorViewportResult::changed,
        "overscroll clamps to the top"
    );
    require(viewport.first_line() == 0, "top clamp names the first line");
    require(near(viewport.offset_in_first(), 0.0F), "top clamp clears the remainder");

    require(
        viewport.scroll_to_line(9999) == veyra::EditorViewportResult::changed,
        "past-the-end scroll clamps to the bottom"
    );
    require(viewport.first_line() == 94, "bottom clamp shows the last full window");
    require(near(viewport.offset_in_first(), 0.0F), "bottom clamp carries no remainder");

    std::size_t first = 0;
    std::size_t count = 0;
    viewport.visible_range(first, count);
    require(first == 94 && count == 6, "bottom window reports its clamped range");
}

void check_ensure_visible_with_context() {
    veyra::EditorViewport viewport = make_viewport();

    require(
        viewport.scroll_to_line(20) == veyra::EditorViewportResult::changed,
        "ensure starts mid-document"
    );
    require(
        viewport.ensure_visible(2, 1) == veyra::EditorViewportResult::changed,
        "ensure above the window scrolls up"
    );
    require(viewport.first_line() == 1, "ensure keeps one context line above the target");

    require(
        viewport.ensure_visible(2, 1) == veyra::EditorViewportResult::ignored,
        "ensure inside the guarded window is a no-op"
    );

    require(
        viewport.ensure_visible(50, 1) == veyra::EditorViewportResult::changed,
        "ensure below the window scrolls down"
    );
    require(viewport.first_line() == 46, "ensure keeps one context line below the target");

    require(
        viewport.ensure_visible(46, 100) == veyra::EditorViewportResult::changed,
        "oversized context clamps to the window"
    );
    require(viewport.first_line() == 41, "clamped context stacks five lines above the target");

    require(
        viewport.ensure_visible(9999, 0) == veyra::EditorViewportResult::changed,
        "past-the-end ensure clamps to the last line"
    );
    require(viewport.first_line() == 94, "clamped ensure shows the last window");
}

void check_prepend_anchor_and_shrink_clamp() {
    veyra::EditorViewport viewport = make_viewport();

    require(
        viewport.scroll_to_line(20) == veyra::EditorViewportResult::changed,
        "anchor scroll lands"
    );

    require(
        viewport.set_total_lines(110, 10) == veyra::EditorViewportResult::changed,
        "prepend reports its inserted count"
    );
    require(viewport.first_line() == 30, "prepend shifts the anchor past the new lines");

    require(
        viewport.set_total_lines(120) == veyra::EditorViewportResult::changed,
        "append keeps the anchor"
    );
    require(viewport.first_line() == 30, "appended lines land after the window");

    require(viewport.set_total_lines(10) == veyra::EditorViewportResult::changed, "shrink clamps");
    require(viewport.first_line() == 4, "shrink keeps the last fitting window");

    require(
        viewport.set_total_lines(3) == veyra::EditorViewportResult::changed,
        "shrink below the view height resets"
    );
    require(viewport.first_line() == 0, "fitting content starts at the top");
    require(near(viewport.offset_in_first(), 0.0F), "fitting content clears the remainder");

    std::size_t first = 99;
    std::size_t count = 99;
    viewport.visible_range(first, count);
    require(first == 0 && count == 3, "short document reports every line");
}

void check_generation_moves_only_on_change() {
    veyra::EditorViewport viewport = make_viewport();
    const auto generation = viewport.generation();

    require(
        viewport.scroll_by_px(15.0F) == veyra::EditorViewportResult::changed,
        "sub-line scroll lands"
    );
    require(viewport.generation() != generation, "sub-line scroll bumps the generation");
    require(viewport.first_line() == 1, "sub-line scroll names the crossed line");
    require(near(viewport.offset_in_first(), 5.0F), "sub-line scroll keeps the remainder");

    const auto moved = viewport.generation();

    require(
        viewport.scroll_by_px(0.0F) == veyra::EditorViewportResult::ignored,
        "no-op scroll is ignored"
    );
    require(viewport.generation() == moved, "ignored scroll leaves the generation alone");
}

}

int main() {
    try {
        check_defaults_and_geometry();
        check_geometry_validation();
        check_scroll_and_pixel_offset();
        check_ensure_visible_with_context();
        check_prepend_anchor_and_shrink_clamp();
        check_generation_moves_only_on_change();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }

    return 0;
}
