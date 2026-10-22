#include "veyra/widgets/data/log_feed.hpp"

#include <cmath>

namespace veyra {
namespace {

bool valid_pill(LayoutRect rect) noexcept {
    if (!std::isfinite(rect.x) || !std::isfinite(rect.y)) {
        return false;
    }

    if (!std::isfinite(rect.width) || !std::isfinite(rect.height)) {
        return false;
    }

    return rect.width > 0.0F && rect.height > 0.0F;
}

bool wants_pill(LogFeedRowState row, LayoutRect badge) noexcept {
    return row.collapsed && valid_pill(badge);
}

Srgba8 badge_for(const Theme& theme, LogFeedKind kind) noexcept {
    switch (kind) {
    case LogFeedKind::error:
        return theme.color(tokens::color_error);
    case LogFeedKind::warning:
        return theme.color(tokens::color_action_hovered);
    case LogFeedKind::info:
        break;
    }

    return theme.color(tokens::color_action_primary);
}

VirtualListRowState chrome_of(LogFeedRowState state) noexcept {
    return {
        .selected = state.selected,
        .current = state.current,
        .placeholder = state.placeholder,
    };
}

}

LogFeedRowAppearance log_feed_row_appearance(const Theme& theme, LogFeedRowState state) noexcept {
    const auto chrome = virtual_list_row_appearance(theme, chrome_of(state));

    return {
        .background = chrome.background,
        .separator = chrome.separator,
        .current_outline = chrome.current_outline,
        .badge = badge_for(theme, state.kind),
    };
}

LogFeedRowsOutput emit_log_feed_rows(
    const Theme& theme,
    std::span<const LogFeedRowState> rows,
    std::span<const LayoutRect> badges,
    const VirtualListRowsGeometry& geometry,
    std::span<DisplayCommand> commands_out
) noexcept {
    if (!badges.empty() && badges.size() != rows.size()) {
        return {
            .result = LogFeedEmitResult::invalid_input,
        };
    }

    if (rows.size() > LogFeed::kCapacity) {
        return {
            .result = LogFeedEmitResult::invalid_input,
        };
    }

    std::array<VirtualListRowState, LogFeed::kCapacity> chrome{};

    for (auto index = std::size_t{}; index < rows.size(); ++index) {
        chrome[index] = chrome_of(rows[index]);
    }

    const auto chrome_output = emit_virtual_list_rows(
        theme,
        std::span<const VirtualListRowState>{chrome.data(), rows.size()},
        geometry,
        commands_out
    );

    if (chrome_output.result == VirtualListEmitResult::invalid_input) {
        return {
            .result = LogFeedEmitResult::invalid_input,
        };
    }

    auto pills = std::size_t{};

    for (auto index = std::size_t{}; index < rows.size(); ++index) {
        if (badges.empty()) {
            break;
        }

        if (wants_pill(rows[index], badges[index])) {
            ++pills;
        }
    }

    const auto required = chrome_output.required + pills;

    if (chrome_output.result == VirtualListEmitResult::insufficient_output) {
        return {
            .placed = chrome_output.placed,
            .required = required,
            .result = LogFeedEmitResult::insufficient_output,
        };
    }

    auto placed = chrome_output.placed;

    if (badges.empty()) {
        return {
            .placed = placed,
            .required = required,
            .result = LogFeedEmitResult::complete,
        };
    }

    const auto radius = theme.radius(tokens::radius_small).value;

    for (auto index = std::size_t{}; index < rows.size(); ++index) {
        if (!wants_pill(rows[index], badges[index])) {
            continue;
        }

        if (placed >= commands_out.size()) {
            return {
                .placed = placed,
                .required = required,
                .result = LogFeedEmitResult::insufficient_output,
            };
        }

        commands_out[placed] = FillRoundedRect{
            .bounds = badges[index],
            .radius = radius,
            .color = badge_for(theme, rows[index].kind),
        };
        ++placed;
    }

    return {
        .placed = placed,
        .required = required,
        .result = LogFeedEmitResult::complete,
    };
}

}
