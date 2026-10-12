#include "veyra/behaviors/breadcrumb.hpp"

#include <array>
#include <cstdint>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

void require(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

std::array<veyra::BreadcrumbItem, 4> trail() {
    return {{
        {.id = 11},
        {.id = 12},
        {.id = 13},
        {.id = 14},
    }};
}

void check_select_reports_intent() {
    auto bar = veyra::BreadcrumbBar{};
    require(!bar.has_cursor(), "a new bar holds no cursor");

    auto crumbs = trail();
    veyra::BreadcrumbAction first = bar.select(13, crumbs);

    require(
        first.result == veyra::BreadcrumbResult::changed && first.id == 13,
        "selecting a crumb reports its id as the navigation intent"
    );
    require(
        bar.has_cursor() && bar.cursor() == 13,
        "a pointer select plants the focus cursor on the same crumb"
    );
    require(
        bar.select(13, crumbs).result == veyra::BreadcrumbResult::handled,
        "reselecting the focused crumb reports no change"
    );
    require(
        bar.select(99, crumbs).result == veyra::BreadcrumbResult::ignored && bar.cursor() == 13,
        "selecting an id outside the trail is ignored"
    );
    require(
        bar.select(0, crumbs).result == veyra::BreadcrumbResult::ignored,
        "the zero id never selects"
    );

    std::span<const veyra::BreadcrumbItem> empty{};
    require(
        bar.select(13, empty).result == veyra::BreadcrumbResult::ignored,
        "selecting over an empty trail is ignored"
    );
}

void check_disabled_skipped() {
    auto bar = veyra::BreadcrumbBar{};
    std::array<veyra::BreadcrumbItem, 4> crumbs{{
        {.id = 21},
        {.id = 22, .disabled = true},
        {.id = 23},
        {.id = 24, .disabled = true},
    }};

    require(
        bar.select(22, crumbs).result == veyra::BreadcrumbResult::ignored,
        "selecting a disabled crumb is ignored"
    );
    require(
        bar.move_cursor(veyra::BreadcrumbMove::next, crumbs) == veyra::BreadcrumbResult::changed &&
            bar.cursor() == 21,
        "roving starts on the first enabled crumb"
    );
    require(
        bar.move_cursor(veyra::BreadcrumbMove::next, crumbs) == veyra::BreadcrumbResult::changed &&
            bar.cursor() == 23,
        "roving forward skips disabled crumbs"
    );
    require(
        bar.move_cursor(veyra::BreadcrumbMove::prev, crumbs) == veyra::BreadcrumbResult::changed &&
            bar.cursor() == 21,
        "roving backward skips disabled crumbs too"
    );
    require(
        bar.activate(crumbs).result == veyra::BreadcrumbResult::changed,
        "activate commits the focused crumb"
    );

    bar.select(24, trail());
    std::array<veyra::BreadcrumbItem, 1> blocked{{{.id = 24, .disabled = true}}};
    require(
        bar.activate(blocked).result == veyra::BreadcrumbResult::ignored,
        "committing onto a disabled cursor is ignored"
    );
}

void check_roving_wraps() {
    auto bar = veyra::BreadcrumbBar{};
    auto crumbs = trail();

    require(
        bar.move_cursor(veyra::BreadcrumbMove::prev, crumbs) == veyra::BreadcrumbResult::changed &&
            bar.cursor() == 14,
        "prev without a cursor starts at the last crumb"
    );
    require(
        bar.move_cursor(veyra::BreadcrumbMove::next, crumbs) == veyra::BreadcrumbResult::changed &&
            bar.cursor() == 11,
        "next wraps from the last crumb to the first"
    );
    require(
        bar.move_cursor(veyra::BreadcrumbMove::last, crumbs) == veyra::BreadcrumbResult::changed &&
            bar.cursor() == 14,
        "last jumps to the final crumb"
    );
    require(
        bar.move_cursor(veyra::BreadcrumbMove::first, crumbs) == veyra::BreadcrumbResult::changed &&
            bar.cursor() == 11,
        "first jumps back to the root"
    );

    auto solo = veyra::BreadcrumbBar{};
    std::array<veyra::BreadcrumbItem, 1> one{{{.id = 7}}};
    require(
        solo.move_cursor(veyra::BreadcrumbMove::next, one) == veyra::BreadcrumbResult::changed,
        "a lone crumb still takes focus"
    );
    require(
        solo.move_cursor(veyra::BreadcrumbMove::next, one) == veyra::BreadcrumbResult::handled &&
            solo.move_cursor(veyra::BreadcrumbMove::prev, one) == veyra::BreadcrumbResult::handled,
        "roving on a single crumb stays put"
    );

    std::array<veyra::BreadcrumbItem, 2> both_blocked{{
        {.id = 31, .disabled = true},
        {.id = 32, .disabled = true},
    }};
    require(
        bar.move_cursor(veyra::BreadcrumbMove::next, both_blocked) ==
                veyra::BreadcrumbResult::ignored &&
            bar.move_cursor(veyra::BreadcrumbMove::first, both_blocked) ==
                veyra::BreadcrumbResult::ignored,
        "roving with every crumb disabled is ignored"
    );
}

void check_stale_cursor_resettles() {
    auto bar = veyra::BreadcrumbBar{};
    auto before = trail();
    bar.select(12, before);

    std::array<veyra::BreadcrumbItem, 2> after{{
        {.id = 11},
        {.id = 14},
    }};
    require(
        bar.move_cursor(veyra::BreadcrumbMove::next, after) == veyra::BreadcrumbResult::changed &&
            bar.cursor() == 11,
        "a cursor id gone from the span re-anchors from the front"
    );
    require(
        bar.activate(after).result == veyra::BreadcrumbResult::changed && bar.cursor() == 11,
        "activate commits the resettled cursor"
    );

    auto fresh = veyra::BreadcrumbBar{};
    require(
        fresh.activate(after).result == veyra::BreadcrumbResult::ignored,
        "activating with no cursor is ignored"
    );
}

void check_overflow_collapses_middle() {
    std::array<veyra::BreadcrumbItem, 7> crumbs{{
        {.id = 1},
        {.id = 2},
        {.id = 3},
        {.id = 4},
        {.id = 5},
        {.id = 6},
        {.id = 7},
    }};
    std::array<std::uint64_t, 7> visible{};

    veyra::BreadcrumbOverflow fit = veyra::resolve_breadcrumb_overflow(crumbs, 7, visible);
    require(
        fit.result == veyra::BreadcrumbOverflowResult::complete && fit.visible_count == 7 &&
            fit.collapsed_count == 0 && visible[6] == 7,
        "a trail that fits copies every id with no collapse"
    );

    veyra::BreadcrumbOverflow roomy = veyra::resolve_breadcrumb_overflow(crumbs, 20, visible);
    require(
        roomy.result == veyra::BreadcrumbOverflowResult::complete && roomy.visible_count == 7 &&
            roomy.collapsed_count == 0,
        "headroom past the trail length still reports complete"
    );

    veyra::BreadcrumbOverflow narrow = veyra::resolve_breadcrumb_overflow(crumbs, 3, visible);
    require(
        narrow.result == veyra::BreadcrumbOverflowResult::collapsed && narrow.visible_count == 3 &&
            narrow.collapsed_count == 4,
        "a long trail collapses and reports how many crumbs hid"
    );
    require(
        visible[0] == 1 && visible[1] == 6 && visible[2] == 7,
        "collapse keeps the root plus the tail and drops the middle"
    );

    std::array<std::uint64_t, 7> repeat{};
    veyra::BreadcrumbOverflow again = veyra::resolve_breadcrumb_overflow(crumbs, 3, repeat);
    require(
        again.visible_count == narrow.visible_count &&
            again.collapsed_count == narrow.collapsed_count && repeat[0] == 1 && repeat[1] == 6 &&
            repeat[2] == 7,
        "the same inputs collapse the same way every call"
    );

    veyra::BreadcrumbOverflow single = veyra::resolve_breadcrumb_overflow(crumbs, 1, visible);
    require(
        single.result == veyra::BreadcrumbOverflowResult::collapsed && single.visible_count == 1 &&
            single.collapsed_count == 6 && visible[0] == 7,
        "one slot keeps the current crumb and drops everything before it"
    );

    veyra::BreadcrumbOverflow pair = veyra::resolve_breadcrumb_overflow(crumbs, 2, visible);
    require(
        pair.visible_count == 2 && pair.collapsed_count == 5 && visible[0] == 1 && visible[1] == 7,
        "two slots keep the root and the current crumb"
    );

    require(
        veyra::resolve_breadcrumb_overflow(crumbs, 0, visible).result ==
            veyra::BreadcrumbOverflowResult::invalid_input,
        "a zero cap cannot show even the current crumb"
    );

    std::array<std::uint64_t, 2> small{};
    require(
        veyra::resolve_breadcrumb_overflow(crumbs, 3, small).result ==
            veyra::BreadcrumbOverflowResult::invalid_input,
        "an output span shorter than the trail is rejected"
    );

    std::span<const veyra::BreadcrumbItem> empty{};
    veyra::BreadcrumbOverflow hollow = veyra::resolve_breadcrumb_overflow(empty, 3, visible);
    require(
        hollow.result == veyra::BreadcrumbOverflowResult::complete && hollow.visible_count == 0 &&
            hollow.collapsed_count == 0,
        "an empty trail is complete with nothing to show"
    );
}

void check_semantics() {
    auto bar = veyra::BreadcrumbBar{};
    auto crumbs = trail();
    bar.select(12, crumbs);

    auto crumb = veyra::SemanticNodeData{};
    require(!bar.write_crumb_semantics(12, {}, false, crumb), "an empty crumb name is rejected");
    require(!bar.write_crumb_semantics(0, "Root", false, crumb), "the zero id writes no semantics");
    require(
        bar.write_crumb_semantics(12, "Settings", false, crumb),
        "a named crumb writes semantics"
    );
    require(
        crumb.role == veyra::SemanticRole::link && crumb.label == "Settings" &&
            veyra::has_state(crumb.states, veyra::SemanticState::focused) &&
            veyra::has_action(crumb.actions, veyra::SemanticAction::select),
        "the focused crumb exposes the focused state and a select action"
    );
    require(
        bar.write_crumb_semantics(13, "Profile", false, crumb) &&
            !veyra::has_state(crumb.states, veyra::SemanticState::focused),
        "an unfocused crumb writes no focused state"
    );
    require(
        bar.write_crumb_semantics(13, "Profile", true, crumb) &&
            veyra::has_state(crumb.states, veyra::SemanticState::disabled) &&
            !veyra::has_action(crumb.actions, veyra::SemanticAction::select),
        "a disabled crumb exposes the disabled state and no actions"
    );

    auto list = veyra::SemanticNodeData{};
    require(!bar.write_list_semantics({}, list), "an empty trail name is rejected");
    require(bar.write_list_semantics("Trail", list), "a named trail writes semantics");
    require(
        list.role == veyra::SemanticRole::list && list.label == "Trail",
        "the container exposes the list role"
    );
}

}

int main() {
    try {
        check_select_reports_intent();
        check_disabled_skipped();
        check_roving_wraps();
        check_stale_cursor_resettles();
        check_overflow_collapses_middle();
        check_semantics();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
