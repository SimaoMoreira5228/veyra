#include "veyra/behaviors/tooltip.hpp"

#include <limits>

namespace veyra {

TooltipController::TooltipController() noexcept = default;

TooltipVisibility TooltipController::visibility() const noexcept {
    return visibility_;
}

std::uint64_t TooltipController::anchor() const noexcept {
    return anchor_;
}

std::uint64_t TooltipController::now_ms() const noexcept {
    return now_ms_;
}

TooltipResult TooltipController::hover_start(
    std::uint64_t anchor,
    std::uint32_t dwell_ms,
    std::uint64_t now_ms
) noexcept {
    if (anchor == 0) {
        return TooltipResult::ignored;
    }

    if (now_ms < now_ms_) {
        return TooltipResult::rejected;
    }

    now_ms_ = now_ms;

    if (anchor_ == anchor && visibility_ != TooltipVisibility::hidden) {
        return TooltipResult::handled;
    }

    anchor_ = anchor;

    if (dwell_ms == 0) {
        visibility_ = TooltipVisibility::shown;
        deadline_ms_ = now_ms;

        return TooltipResult::shown;
    }

    constexpr auto kMax = std::numeric_limits<std::uint64_t>::max();

    if (dwell_ms > kMax - now_ms) {
        deadline_ms_ = kMax;
    } else {
        deadline_ms_ = now_ms + dwell_ms;
    }

    visibility_ = TooltipVisibility::pending;

    return TooltipResult::changed;
}

TooltipResult TooltipController::advance(std::uint64_t now_ms) noexcept {
    if (now_ms < now_ms_) {
        return TooltipResult::rejected;
    }

    now_ms_ = now_ms;

    if (visibility_ != TooltipVisibility::pending) {
        return TooltipResult::handled;
    }

    if (now_ms < deadline_ms_) {
        return TooltipResult::handled;
    }

    visibility_ = TooltipVisibility::shown;

    return TooltipResult::shown;
}

TooltipResult TooltipController::hide() noexcept {
    if (visibility_ == TooltipVisibility::hidden) {
        return TooltipResult::handled;
    }

    visibility_ = TooltipVisibility::hidden;
    anchor_ = 0;
    deadline_ms_ = 0;

    return TooltipResult::changed;
}

bool TooltipController::write_tooltip_semantics(
    std::string_view accessible_name,
    SemanticNodeData& output
) const noexcept {
    if (accessible_name.empty()) {
        return false;
    }

    if (visibility_ != TooltipVisibility::shown) {
        return false;
    }

    output = {
        .role = SemanticRole::text,
        .label = accessible_name,
        .states = std::uint16_t{},
        .actions = std::uint16_t{},
    };

    return true;
}

}
