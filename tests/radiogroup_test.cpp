#include "veyra/behaviors/radiogroup.hpp"

#include <array>
#include <iostream>
#include <stdexcept>
#include <string_view>

namespace {

void require(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

constexpr std::array<veyra::RadioOption, 4> kOptions{{
    {.id = 11},
    {.id = 22},
    {.id = 33, .disabled = true},
    {.id = 44},
}};

veyra::RadioGroupController focused_group() {
    veyra::RadioGroupController group{};
    require(group.configure({.options = kOptions, .selected = 11}), "options configure");
    require(group.set_focused(true) == veyra::RadioResult::handled, "group focuses");
    return group;
}

void check_select_reports_transitions() {
    auto group = focused_group();
    require(group.has_selection() && group.selected() == 11, "initial selection applies");

    const auto same = group.select(11);
    require(
        same.result == veyra::RadioResult::handled && same.selected == 11 && same.has_selection,
        "reselecting reports no change"
    );

    const auto moved = group.select(44);
    require(
        moved.result == veyra::RadioResult::changed && moved.selected == 44 &&
            group.selected() == 44,
        "selecting another option changes the selection"
    );

    require(
        group.select(33).result == veyra::RadioResult::ignored && group.selected() == 44,
        "disabled options reject selection"
    );
    require(
        group.select(99).result == veyra::RadioResult::ignored && group.selected() == 44,
        "unknown ids reject selection"
    );

    require(group.set_disabled(true), "disabling reports a change");
    require(
        group.select(22).result == veyra::RadioResult::ignored && group.selected() == 44,
        "selection is rejected while disabled"
    );
}

void check_arrows_skip_disabled_and_wrap() {
    auto group = focused_group();

    const auto skipped = group.key_down(veyra::RadioKey::right);
    require(
        skipped.result == veyra::RadioResult::changed && skipped.selected == 22,
        "right moves to the next enabled option"
    );

    const auto wrapped = group.key_down(veyra::RadioKey::right);
    require(
        wrapped.result == veyra::RadioResult::changed && wrapped.selected == 44,
        "disabled options never take selection"
    );

    const auto over_edge = group.key_down(veyra::RadioKey::right);
    require(
        over_edge.result == veyra::RadioResult::changed && over_edge.selected == 11,
        "right wraps past the last option"
    );

    const auto back = group.key_down(veyra::RadioKey::left);
    require(
        back.result == veyra::RadioResult::changed && back.selected == 44,
        "left wraps past the first option"
    );

    const auto first = group.key_down(veyra::RadioKey::home);
    require(
        first.result == veyra::RadioResult::changed && first.selected == 11,
        "home jumps to the first enabled option"
    );

    const auto last = group.key_down(veyra::RadioKey::end);
    require(
        last.result == veyra::RadioResult::changed && last.selected == 44,
        "end jumps to the last enabled option"
    );

    require(group.set_focused(false) == veyra::RadioResult::handled, "blur reports handling");
    require(
        group.key_down(veyra::RadioKey::right).result == veyra::RadioResult::ignored &&
            group.selected() == 44,
        "keys are ignored while blurred"
    );
}

void check_empty_selection_needs_permission() {
    veyra::RadioGroupController group{};
    require(
        group.configure({.options = kOptions, .has_selection = false, .allow_none = true}),
        "empty initial selection configures"
    );
    require(!group.has_selection() && group.count() == 4, "empty stays empty");

    require(group.set_focused(true) == veyra::RadioResult::handled, "empty group focuses");

    const auto first = group.key_down(veyra::RadioKey::down);
    require(
        first.result == veyra::RadioResult::changed && first.selected == 11,
        "arrows select from empty"
    );
}

void check_configure_normalizes_missing_selection() {
    veyra::RadioGroupController group{};
    require(
        group.configure({.options = kOptions, .selected = 99}),
        "unknown initial selection configures"
    );
    require(group.selected() == 11, "unknown initial selection falls back to first enabled");
    require(group.has_selection(), "fallback keeps a selection");

    constexpr std::array<veyra::RadioOption, 2> all_off{{
        {.id = 1, .disabled = true},
        {.id = 2, .disabled = true},
    }};
    require(group.configure({.options = all_off, .selected = 1}), "all-disabled options configure");
    require(!group.has_selection(), "no enabled option means no selection");
    require(
        group.key_down(veyra::RadioKey::right).result == veyra::RadioResult::ignored,
        "keys are ignored with nothing selectable"
    );

    constexpr std::array<veyra::RadioOption, 0> empty{};
    require(group.configure({.options = empty}), "empty options configure");
    require(group.count() == 0 && !group.has_selection(), "empty group holds nothing");
    require(
        group.select(11).result == veyra::RadioResult::ignored,
        "selection is rejected with no options"
    );
}

void check_semantics_report_selection() {
    auto group = focused_group();
    auto node = veyra::SemanticNodeData{};

    require(group.write_semantics("second", 22, node), "known options write semantics");
    require(
        node.role == veyra::SemanticRole::radio && node.label == "second",
        "options expose the radio role and label"
    );
    require(
        !veyra::has_state(node.states, veyra::SemanticState::selected),
        "unselected options omit the selected state"
    );

    require(group.write_semantics("first", 11, node), "selected options write semantics");
    require(
        veyra::has_state(node.states, veyra::SemanticState::selected) &&
            veyra::has_state(node.states, veyra::SemanticState::focused),
        "selected options report selected and focused"
    );

    require(group.write_semantics("third", 33, node), "disabled options write semantics");
    require(
        veyra::has_state(node.states, veyra::SemanticState::disabled),
        "disabled options report disabled"
    );

    require(!group.write_semantics("ghost", 99, node), "unknown ids write nothing");
    require(!group.write_semantics("", 11, node), "empty names write nothing");
}

}

int main() {
    try {
        check_select_reports_transitions();
        check_arrows_skip_disabled_and_wrap();
        check_empty_selection_needs_permission();
        check_configure_normalizes_missing_selection();
        check_semantics_report_selection();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
