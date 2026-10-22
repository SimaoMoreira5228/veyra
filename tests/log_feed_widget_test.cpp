#include "veyra/widgets/data/log_feed.hpp"

#include <array>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>

namespace {

void require(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

const veyra::FillRect& as_fill(const veyra::DisplayCommand& command, std::string_view message) {
    const auto* fill = std::get_if<veyra::FillRect>(&command);

    require(fill != nullptr, message);

    return *fill;
}

const veyra::FillRoundedRect&
as_pill(const veyra::DisplayCommand& command, std::string_view message) {
    const auto* pill = std::get_if<veyra::FillRoundedRect>(&command);

    require(pill != nullptr, message);

    return *pill;
}

bool same_rect(veyra::LayoutRect rect, float x, float y, float width, float height) {
    return rect.x == x && rect.y == y && rect.width == width && rect.height == height;
}

void require_same_chrome_command(
    const veyra::DisplayCommand& left,
    const veyra::DisplayCommand& right,
    std::string_view message
) {
    require(left.index() == right.index(), message);

    if (const auto* left_fill = std::get_if<veyra::FillRect>(&left)) {
        const auto& right_fill = as_fill(right, message);
        require(
            left_fill->bounds.x == right_fill.bounds.x &&
                left_fill->bounds.y == right_fill.bounds.y &&
                left_fill->bounds.width == right_fill.bounds.width &&
                left_fill->bounds.height == right_fill.bounds.height &&
                left_fill->color == right_fill.color,
            message
        );

        return;
    }

    const auto* left_outline = std::get_if<veyra::StrokeRect>(&left);
    const auto* right_outline = std::get_if<veyra::StrokeRect>(&right);

    require(left_outline != nullptr && right_outline != nullptr, message);
    require(
        left_outline->bounds.x == right_outline->bounds.x &&
            left_outline->bounds.y == right_outline->bounds.y &&
            left_outline->bounds.width == right_outline->bounds.width &&
            left_outline->bounds.height == right_outline->bounds.height &&
            left_outline->thickness == right_outline->thickness &&
            left_outline->color == right_outline->color,
        message
    );
}

void check_appearance_delegates_chrome() {
    const auto theme = veyra::Theme{};

    constexpr std::array<veyra::LogFeedRowState, 4> rows{{
        {},
        {.kind = veyra::LogFeedKind::warning, .selected = true},
        {.kind = veyra::LogFeedKind::error, .current = true},
        {.kind = veyra::LogFeedKind::info, .selected = true, .placeholder = true},
    }};

    for (const auto& row : rows) {
        const veyra::VirtualListRowState chrome{
            .selected = row.selected,
            .current = row.current,
            .placeholder = row.placeholder,
        };

        const auto feed_look = veyra::log_feed_row_appearance(theme, row);
        const auto list_look = veyra::virtual_list_row_appearance(theme, chrome);

        require(
            feed_look.background == list_look.background &&
                feed_look.separator == list_look.separator &&
                feed_look.current_outline == list_look.current_outline,
            "feed chrome matches the virtual list chrome for the same flags"
        );
    }

    const auto plain = veyra::log_feed_row_appearance(theme, {});
    const auto folded = veyra::log_feed_row_appearance(theme, {.collapsed = true});
    require(
        folded.background == plain.background && folded.separator == plain.separator &&
            folded.current_outline == plain.current_outline,
        "collapsing a row changes no chrome, the badge carries the fold"
    );
}

void check_badge_follows_kind() {
    const auto theme = veyra::Theme{};

    const auto info = veyra::log_feed_row_appearance(theme, {.kind = veyra::LogFeedKind::info});
    require(
        info.badge == theme.color(veyra::tokens::color_action_primary),
        "an info badge paints the primary action color"
    );

    const auto warning =
        veyra::log_feed_row_appearance(theme, {.kind = veyra::LogFeedKind::warning});
    require(
        warning.badge == theme.color(veyra::tokens::color_action_hovered),
        "a warning badge borrows the hovered blue, the set has no amber"
    );

    const auto error = veyra::log_feed_row_appearance(theme, {.kind = veyra::LogFeedKind::error});
    require(
        error.badge == theme.color(veyra::tokens::color_error),
        "an error badge paints the error color"
    );
}

void check_emit_without_badges_matches_list_emit() {
    const auto theme = veyra::Theme{};
    constexpr std::array<veyra::LogFeedRowState, 3> rows{{
        {.kind = veyra::LogFeedKind::info, .collapsed = true, .selected = true},
        {.kind = veyra::LogFeedKind::warning, .placeholder = true},
        {.kind = veyra::LogFeedKind::error, .current = true},
    }};
    constexpr std::array<veyra::VirtualListRowState, 3> chrome{{
        {.selected = true},
        {.placeholder = true},
        {.current = true},
    }};
    const veyra::VirtualListRowsGeometry geometry{
        .list_bounds = {.x = 10.0F, .y = 20.0F, .width = 200.0F, .height = 120.0F},
        .row_height = 24.0F,
        .sticky_header_height = 12.0F,
    };

    std::array<veyra::DisplayCommand, 12> feed_commands{};
    const auto feed_output = veyra::emit_log_feed_rows(theme, rows, {}, geometry, feed_commands);

    std::array<veyra::DisplayCommand, 12> list_commands{};
    const auto list_output = veyra::emit_virtual_list_rows(theme, chrome, geometry, list_commands);

    require(
        feed_output.result == veyra::LogFeedEmitResult::complete &&
            list_output.result == veyra::VirtualListEmitResult::complete,
        "both emits complete on a sized span"
    );
    require(
        feed_output.placed == list_output.placed && feed_output.required == list_output.required,
        "a badgeless feed emit reports the same placed and required counts"
    );

    for (auto index = std::size_t{}; index < feed_output.placed; ++index) {
        require_same_chrome_command(
            feed_commands[index],
            list_commands[index],
            "each feed chrome command replays the list emit in order"
        );
    }
}

void check_pills_paint_behind_caller_text() {
    const auto theme = veyra::Theme{};
    constexpr std::array<veyra::LogFeedRowState, 4> rows{{
        {.kind = veyra::LogFeedKind::info},
        {.kind = veyra::LogFeedKind::warning, .collapsed = true},
        {.kind = veyra::LogFeedKind::error, .collapsed = true},
        {.kind = veyra::LogFeedKind::info, .collapsed = false},
    }};
    constexpr veyra::LayoutRect kBadge{.x = 150.0F, .y = 30.0F, .width = 44.0F, .height = 18.0F};
    constexpr veyra::LayoutRect kEmpty{};
    constexpr std::array<veyra::LayoutRect, 4> badges{{{}, kBadge, kEmpty, kBadge}};
    const veyra::VirtualListRowsGeometry geometry{
        .list_bounds = {.x = 0.0F, .y = 0.0F, .width = 200.0F, .height = 96.0F},
        .row_height = 24.0F,
    };
    std::array<veyra::DisplayCommand, 12> commands{};

    const auto output = veyra::emit_log_feed_rows(theme, rows, badges, geometry, commands);

    require(output.result == veyra::LogFeedEmitResult::complete, "the badged emit completes");
    require(output.placed == 5 && output.required == 5, "four separators plus one pill land");

    const auto& pill = as_pill(commands[4], "the pill paints after every chrome command");
    require(
        same_rect(pill.bounds, 150.0F, 30.0F, 44.0F, 18.0F),
        "the pill keeps the caller-supplied badge rect"
    );
    require(
        pill.radius == theme.radius(veyra::tokens::radius_small).value,
        "the pill rounds with the small theme radius"
    );
    require(
        pill.color ==
            veyra::log_feed_row_appearance(theme, {.kind = veyra::LogFeedKind::warning}).badge,
        "the pill paints the collapsed row's badge color"
    );
}

void check_pill_needs_collapse_and_rect() {
    const auto theme = veyra::Theme{};
    const veyra::VirtualListRowsGeometry geometry{
        .list_bounds = {.x = 0.0F, .y = 0.0F, .width = 200.0F, .height = 48.0F},
        .row_height = 24.0F,
    };

    constexpr std::array<veyra::LogFeedRowState, 1> folded{{{.collapsed = true}}};
    constexpr std::array<veyra::LayoutRect, 1> empty_rect{{}};
    std::array<veyra::DisplayCommand, 8> commands{};

    const auto suppressed =
        veyra::emit_log_feed_rows(theme, folded, empty_rect, geometry, commands);
    require(
        suppressed.result == veyra::LogFeedEmitResult::complete && suppressed.placed == 1 &&
            suppressed.required == 1,
        "a collapsed row with an empty badge rect emits chrome only"
    );

    constexpr std::array<veyra::LogFeedRowState, 1> plain{{}};
    constexpr veyra::LayoutRect badge{.x = 8.0F, .y = 8.0F, .width = 40.0F, .height = 16.0F};
    constexpr std::array<veyra::LayoutRect, 1> stray_badge{{badge}};

    const auto uncollapsed =
        veyra::emit_log_feed_rows(theme, plain, stray_badge, geometry, commands);
    require(
        uncollapsed.result == veyra::LogFeedEmitResult::complete && uncollapsed.placed == 1 &&
            uncollapsed.required == 1,
        "a badge rect on a plain row paints no pill"
    );
}

void check_mismatched_badges_rejects() {
    const auto theme = veyra::Theme{};
    constexpr std::array<veyra::LogFeedRowState, 2> rows{{{}, {}}};
    constexpr std::array<veyra::LayoutRect, 1> badges{{}};
    const veyra::VirtualListRowsGeometry geometry{
        .list_bounds = {.x = 0.0F, .y = 0.0F, .width = 200.0F, .height = 48.0F},
        .row_height = 24.0F,
    };
    std::array<veyra::DisplayCommand, 8> commands{};

    const auto output = veyra::emit_log_feed_rows(theme, rows, badges, geometry, commands);

    require(
        output.result == veyra::LogFeedEmitResult::invalid_input && output.placed == 0 &&
            output.required == 0,
        "a badge span that matches no row rejects the emit"
    );
}

void check_overflow_reports_required_and_prefix() {
    const auto theme = veyra::Theme{};
    constexpr std::array<veyra::LogFeedRowState, 4> rows{{
        {.kind = veyra::LogFeedKind::info},
        {.kind = veyra::LogFeedKind::warning, .collapsed = true},
        {.kind = veyra::LogFeedKind::error, .collapsed = true},
        {.kind = veyra::LogFeedKind::info, .collapsed = false},
    }};
    constexpr veyra::LayoutRect kBadge{.x = 150.0F, .y = 30.0F, .width = 44.0F, .height = 18.0F};
    constexpr std::array<veyra::LayoutRect, 4> badges{{{}, kBadge, kBadge, {}}};
    const veyra::VirtualListRowsGeometry geometry{
        .list_bounds = {.x = 0.0F, .y = 0.0F, .width = 200.0F, .height = 96.0F},
        .row_height = 24.0F,
    };

    std::array<veyra::DisplayCommand, 12> full{};
    const auto complete = veyra::emit_log_feed_rows(theme, rows, badges, geometry, full);
    require(
        complete.result == veyra::LogFeedEmitResult::complete && complete.placed == 6,
        "the reference emit fits its span"
    );

    std::array<veyra::DisplayCommand, 3> tight{};
    const auto output = veyra::emit_log_feed_rows(theme, rows, badges, geometry, tight);

    require(
        output.result == veyra::LogFeedEmitResult::insufficient_output,
        "a short span reports its overflow"
    );
    require(output.placed == 3 && output.required == 6, "the report keeps placed and required");

    for (auto index = std::size_t{}; index < output.placed; ++index) {
        require_same_chrome_command(
            tight[index],
            full[index],
            "the short span keeps a whole-command prefix of the full emit"
        );
    }
}

void check_pill_overflow_reports_full_need() {
    const auto theme = veyra::Theme{};
    constexpr std::array<veyra::LogFeedRowState, 2> rows{{
        {.kind = veyra::LogFeedKind::info},
        {.kind = veyra::LogFeedKind::warning, .collapsed = true},
    }};
    constexpr veyra::LayoutRect kBadge{.x = 4.0F, .y = 26.0F, .width = 40.0F, .height = 16.0F};
    constexpr std::array<veyra::LayoutRect, 2> badges{{{}, kBadge}};
    const veyra::VirtualListRowsGeometry geometry{
        .list_bounds = {.x = 0.0F, .y = 0.0F, .width = 200.0F, .height = 48.0F},
        .row_height = 24.0F,
    };

    std::array<veyra::DisplayCommand, 2> tight{};
    const auto output = veyra::emit_log_feed_rows(theme, rows, badges, geometry, tight);

    require(
        output.result == veyra::LogFeedEmitResult::insufficient_output,
        "a span that fits chrome but not the pill still overflows"
    );
    require(
        output.placed == 2 && output.required == 3,
        "the pill counts toward the required total"
    );
}

void check_invalid_geometry_rejects() {
    const auto theme = veyra::Theme{};
    constexpr std::array<veyra::LogFeedRowState, 1> rows{{}};
    const veyra::VirtualListRowsGeometry flat{
        .list_bounds = {.x = 0.0F, .y = 0.0F, .width = 100.0F, .height = 40.0F},
        .row_height = 0.0F,
    };
    std::array<veyra::DisplayCommand, 4> commands{};

    const auto output = veyra::emit_log_feed_rows(theme, rows, {}, flat, commands);

    require(
        output.result == veyra::LogFeedEmitResult::invalid_input && output.placed == 0 &&
            output.required == 0,
        "a zero row height rejects the emit without touching the span"
    );
}

void check_empty_emit_completes() {
    const auto theme = veyra::Theme{};
    const veyra::VirtualListRowsGeometry geometry{
        .list_bounds = {.x = 0.0F, .y = 0.0F, .width = 100.0F, .height = 40.0F},
        .row_height = 24.0F,
    };
    std::array<veyra::DisplayCommand, 4> commands{};

    const auto output = veyra::emit_log_feed_rows(theme, {}, {}, geometry, commands);

    require(
        output.result == veyra::LogFeedEmitResult::complete && output.placed == 0 &&
            output.required == 0,
        "an empty row set completes with no commands"
    );
}

void check_theme_overlay_flows_through() {
    auto theme = veyra::Theme::dark();

    auto tweaks = veyra::Theme{};
    const auto accent = veyra::Srgba8{.red = 10, .green = 200, .blue = 120};
    require(
        tweaks.set(veyra::tokens::color_action_primary, accent) ==
            veyra::ThemeUpdateResult::changed,
        "the overlay takes the scoped tweak"
    );
    require(
        theme.apply(tweaks) == veyra::ThemeUpdateResult::changed,
        "layering the overlay reports a change"
    );

    const auto info = veyra::log_feed_row_appearance(theme, {.kind = veyra::LogFeedKind::info});
    require(info.badge == accent, "the info badge follows the scoped theme override");
    require(
        info.separator == theme.color(veyra::tokens::color_text_disabled),
        "untweaked tokens keep resolving through the derived theme"
    );
}

}

int main() {
    try {
        check_appearance_delegates_chrome();
        check_badge_follows_kind();
        check_emit_without_badges_matches_list_emit();
        check_pills_paint_behind_caller_text();
        check_pill_needs_collapse_and_rect();
        check_mismatched_badges_rejects();
        check_overflow_reports_required_and_prefix();
        check_pill_overflow_reports_full_need();
        check_invalid_geometry_rejects();
        check_empty_emit_completes();
        check_theme_overlay_flows_through();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
