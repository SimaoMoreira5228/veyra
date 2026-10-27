#include "veyra/behaviors/overlay/tooltip.hpp"

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

void check_dwell_shows() {
    auto tip = veyra::TooltipController{};
    require(
        tip.visibility() == veyra::TooltipVisibility::hidden && tip.anchor() == 0,
        "a new tooltip hides with no anchor"
    );

    require(
        tip.hide() == veyra::TooltipResult::handled,
        "hiding a hidden tooltip reports no change"
    );
    require(
        tip.advance(100) == veyra::TooltipResult::handled,
        "advancing with nothing pending reports no change"
    );
    require(
        tip.hover_start(0, 50, 100) == veyra::TooltipResult::ignored,
        "the zero anchor never arms"
    );

    require(
        tip.hover_start(9, 50, 100) == veyra::TooltipResult::changed,
        "hovering an anchor arms the dwell"
    );
    require(
        tip.visibility() == veyra::TooltipVisibility::pending && tip.anchor() == 9,
        "arming parks the tooltip pending on its anchor"
    );
    require(
        tip.advance(149) == veyra::TooltipResult::handled &&
            tip.visibility() == veyra::TooltipVisibility::pending,
        "advancing before the dwell keeps the tooltip pending"
    );
    require(
        tip.advance(150) == veyra::TooltipResult::shown &&
            tip.visibility() == veyra::TooltipVisibility::shown,
        "advancing onto the deadline shows the tooltip"
    );
    require(
        tip.advance(900) == veyra::TooltipResult::handled &&
            tip.visibility() == veyra::TooltipVisibility::shown,
        "advancing past the show keeps it shown with no second event"
    );
}

void check_repeat_hover_keeps_deadline() {
    auto tip = veyra::TooltipController{};
    tip.hover_start(4, 50, 0);

    require(
        tip.hover_start(4, 50, 30) == veyra::TooltipResult::handled,
        "re-hovering the same anchor reports no change"
    );
    require(
        tip.advance(50) == veyra::TooltipResult::shown,
        "the original deadline survives the repeat hover"
    );
    require(
        tip.hover_start(4, 50, 60) == veyra::TooltipResult::handled,
        "re-hovering a shown anchor reports no change either"
    );

    require(
        tip.hover_start(5, 50, 60) == veyra::TooltipResult::changed && tip.anchor() == 5 &&
            tip.visibility() == veyra::TooltipVisibility::pending,
        "moving to a new anchor re-arms the dwell from scratch"
    );
    require(
        tip.advance(109) == veyra::TooltipResult::handled,
        "the new dwell counts from the re-arm, not the first hover"
    );
    require(
        tip.advance(110) == veyra::TooltipResult::shown,
        "the re-armed dwell still shows on time"
    );
}

void check_zero_dwell_shows_at_once() {
    auto tip = veyra::TooltipController{};
    require(
        tip.hover_start(3, 0, 40) == veyra::TooltipResult::shown &&
            tip.visibility() == veyra::TooltipVisibility::shown,
        "a zero dwell skips pending and shows immediately"
    );
}

void check_hide_paths() {
    auto tip = veyra::TooltipController{};
    tip.hover_start(7, 50, 0);

    require(
        tip.hide() == veyra::TooltipResult::changed &&
            tip.visibility() == veyra::TooltipVisibility::hidden && tip.anchor() == 0,
        "hiding a pending tooltip clears it and its anchor"
    );

    tip.hover_start(7, 0, 10);
    require(
        tip.hide() == veyra::TooltipResult::changed &&
            tip.visibility() == veyra::TooltipVisibility::hidden,
        "hiding a shown tooltip clears it the same way"
    );
}

void check_clock_moves_forward_only() {
    auto tip = veyra::TooltipController{};
    tip.hover_start(2, 50, 200);

    require(
        tip.advance(199) == veyra::TooltipResult::rejected &&
            tip.visibility() == veyra::TooltipVisibility::pending,
        "a backward advance is rejected with the dwell untouched"
    );
    require(
        tip.hover_start(2, 50, 199) == veyra::TooltipResult::rejected,
        "a backward hover is rejected the same way"
    );
    require(
        tip.advance(250) == veyra::TooltipResult::shown,
        "the clock resumes forward and the dwell fires"
    );
}

void check_semantics_carry_no_actions() {
    auto tip = veyra::TooltipController{};
    auto node = veyra::SemanticNodeData{};

    require(!tip.write_tooltip_semantics({}, node), "an empty tooltip name is rejected");
    require(!tip.write_tooltip_semantics("Hint", node), "a hidden tooltip describes nothing");

    tip.hover_start(6, 50, 0);
    require(!tip.write_tooltip_semantics("Hint", node), "a pending tooltip describes nothing yet");

    tip.advance(50);
    require(tip.write_tooltip_semantics("Hint", node), "a shown tooltip writes semantics");
    require(
        node.role == veyra::SemanticRole::text && node.label == "Hint" &&
            node.actions == std::uint16_t{},
        "the tooltip exposes plain text with no actions, so it can never take action"
    );
}

}

int main() {
    try {
        check_dwell_shows();
        check_repeat_hover_keeps_deadline();
        check_zero_dwell_shows_at_once();
        check_hide_paths();
        check_clock_moves_forward_only();
        check_semantics_carry_no_actions();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
