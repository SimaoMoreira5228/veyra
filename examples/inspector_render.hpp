#ifndef VEYRA_EXAMPLES_INSPECTOR_RENDER_HPP
#define VEYRA_EXAMPLES_INSPECTOR_RENDER_HPP

#include "debug_text.hpp"
#include "veyra/core/semantics/semantics.hpp"
#include "veyra/inspector/trace/trace_recorder.hpp"
#include "veyra/platform/core/window.hpp"
#include "veyra/platform/core/window_router.hpp"
#include "veyra/render/cpu/software_renderer.hpp"
#include "veyra/style/theme/theme.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <span>
#include <system_error>

namespace veyra {

struct InspectorView {
    std::span<const SemanticNode> nodes;
    std::span<const TraceEvent> trace{};
    std::span<const TokenProvenance> provenance{};
    std::size_t event_scroll{};
    NodeId focused{};
    bool window_focused{};
};

struct InspectorDisplay {
    std::array<DisplayCommand, 512> commands{};
    std::size_t count{};
    bool overflowed{};
};

inline Srgba8 trace_color(const Theme& theme, TraceEventKind kind) noexcept {
    switch (kind) {
    case TraceEventKind::route:
        return theme.color(tokens::color_action_primary);
    case TraceEventKind::provenance:
        return theme.color(tokens::color_action_hovered);
    case TraceEventKind::focus:
        return theme.color(tokens::color_text_primary);
    case TraceEventKind::invalidation:
    case TraceEventKind::damage:
        return theme.color(tokens::color_error);
    case TraceEventKind::layout:
        return theme.color(tokens::color_text_disabled);
    case TraceEventKind::cache:
        return theme.color(tokens::color_action_on_primary);
    case TraceEventKind::frame_begin:
    case TraceEventKind::frame_end:
        return theme.color(tokens::color_action_disabled);
    }
    return theme.color(tokens::color_text_disabled);
}

inline Srgba8 provenance_layer_color(const Theme& theme, ThemeLayer layer) noexcept {
    switch (layer) {
    case ThemeLayer::defaults:
        return theme.color(tokens::color_action_disabled);
    case ThemeLayer::preset:
        return theme.color(tokens::color_text_disabled);
    case ThemeLayer::file:
        return theme.color(tokens::color_action_primary);
    case ThemeLayer::override:
        return theme.color(tokens::color_action_hovered);
    }
    return theme.color(tokens::color_text_disabled);
}

inline Srgba8 token_fill(const Theme& theme, const TokenProvenance& provenance) noexcept {
    switch (provenance.kind) {
    case TokenKind::color:
        return theme.color(static_cast<ColorToken>(provenance.token));
    case TokenKind::space: {
        const auto value = theme.space(static_cast<SpaceToken>(provenance.token)).value;
        const auto shade =
            static_cast<std::uint8_t>(std::clamp(value / 24.0F * 255.0F, 0.0F, 255.0F));
        return {.red = shade, .green = shade, .blue = shade};
    }
    case TokenKind::radius: {
        const auto value = theme.radius(static_cast<RadiusToken>(provenance.token)).value;
        const auto shade =
            static_cast<std::uint8_t>(std::clamp(value / 8.0F * 255.0F, 0.0F, 255.0F));
        return {.red = shade, .green = shade, .blue = shade};
    }
    case TokenKind::motion: {
        const auto value = theme.motion(static_cast<MotionToken>(provenance.token)).value;
        const auto shade = static_cast<std::uint8_t>(
            std::clamp(static_cast<float>(value) / 300.0F * 255.0F, 0.0F, 255.0F)
        );
        return {.red = shade, .green = shade, .blue = shade};
    }
    }
    return theme.color(tokens::color_surface);
}

inline std::size_t
frame_durations(std::span<const TraceEvent> trace, std::span<std::uint32_t> output) noexcept {
    auto count = std::size_t{};
    auto pending = false;
    auto pending_frame = std::uint64_t{};
    auto start = std::uint64_t{};

    for (const auto& event : trace) {
        if (event.kind == TraceEventKind::frame_begin) {
            pending = true;
            pending_frame = event.payload.frame.frame;
            start = event.time_ms;
            continue;
        }
        if (event.kind != TraceEventKind::frame_end || !pending ||
            event.payload.frame.frame != pending_frame) {
            continue;
        }

        const auto duration =
            event.time_ms > start ? static_cast<std::uint32_t>(event.time_ms - start) : 0;
        if (count < output.size()) {
            output[count] = duration;
            ++count;
        } else {
            for (auto index = std::size_t{1}; index < output.size(); ++index) {
                output[index - 1] = output[index];
            }
            output[output.size() - 1] = duration;
        }
        pending = false;
    }

    return count;
}

inline InspectorDisplay build_inspector_display(
    const Theme& theme,
    const InspectorView& view,
    std::size_t width,
    std::size_t height
) noexcept {
    auto display = InspectorDisplay{};
    if (width == 0 || height == 0) {
        return display;
    }

    constexpr auto header_height = 20.0F;
    const auto content_top = header_height;
    const auto content_height = static_cast<float>(height) - header_height;
    const auto tree_height = content_height * 0.40F;
    const auto provenance_height = content_height * 0.24F;
    const auto event_height = content_height * 0.20F;
    const auto timing_height = content_height - tree_height - provenance_height - event_height;

    auto& commands = display.commands;
    auto placed = std::size_t{};
    auto push = [&](DisplayCommand command) noexcept {
        if (placed < commands.size()) {
            commands[placed] = command;
            ++placed;
            return;
        }
        display.overflowed = true;
    };
    auto fill = [&](LayoutRect bounds, Srgba8 color) noexcept {
        push(FillRect{.bounds = bounds, .color = color});
    };
    auto stroke = [&](LayoutRect bounds, float thickness, Srgba8 color) noexcept {
        push(StrokeRect{.bounds = bounds, .thickness = thickness, .color = color});
    };

    fill(
        {.width = static_cast<float>(width), .height = static_cast<float>(height)},
        theme.color(tokens::color_surface)
    );
    fill(
        {.width = static_cast<float>(width), .height = header_height},
        theme.color(tokens::color_action_primary)
    );
    stroke(
        {.x = 0.5F,
         .y = 0.5F,
         .width = static_cast<float>(width) - 1.0F,
         .height = static_cast<float>(height) - 1.0F},
        1.0F,
        view.window_focused ? theme.color(tokens::color_action_hovered)
                            : theme.color(tokens::color_action_disabled)
    );

    const auto section_top = content_top + tree_height;
    const auto event_top = section_top + provenance_height;
    const auto timing_top = event_top + event_height;
    const auto separator = theme.color(tokens::color_action_disabled);
    fill({.y = section_top, .width = static_cast<float>(width), .height = 1.0F}, separator);
    fill({.y = event_top, .width = static_cast<float>(width), .height = 1.0F}, separator);
    fill({.y = timing_top, .width = static_cast<float>(width), .height = 1.0F}, separator);

    if (!view.nodes.empty()) {
        const auto& root = view.nodes.front();
        if (root.bounds.width > 0.0F && root.bounds.height > 0.0F) {
            const auto scale = std::min(
                static_cast<float>(width) / root.bounds.width,
                tree_height / root.bounds.height
            );
            for (const auto& node : view.nodes) {
                if (node.bounds.width <= 0.0F || node.bounds.height <= 0.0F) {
                    continue;
                }

                const auto x = (node.bounds.x - root.bounds.x) * scale;
                const auto y = content_top + (node.bounds.y - root.bounds.y) * scale;
                const auto node_width =
                    std::min(node.bounds.width * scale, static_cast<float>(width) - x);
                const auto node_height =
                    std::min(node.bounds.height * scale, content_top + tree_height - y);
                if (node_width <= 0.0F || node_height <= 0.0F) {
                    continue;
                }

                const auto focused = node.id == view.focused;
                const auto bounds =
                    LayoutRect{.x = x, .y = y, .width = node_width, .height = node_height};
                stroke(
                    bounds,
                    1.0F,
                    focused ? theme.color(tokens::color_action_hovered)
                            : theme.color(tokens::color_action_disabled)
                );
                if (node.id != root.id) {
                    constexpr auto inset = 2.0F;
                    fill(
                        {.x = x + inset,
                         .y = y + inset,
                         .width = std::max(0.0F, node_width - inset * 2.0F),
                         .height = std::max(0.0F, node_height - inset * 2.0F)},
                        focused ? theme.color(tokens::color_action_primary)
                                : theme.color(tokens::color_text_disabled)
                    );
                }
            }
        }
    }

    auto text = [&](std::string_view value, float x, float y, Srgba8 color) noexcept {
        draw_debug_text(value, x, y, color, push);
    };
    auto number = [&](std::uint64_t value, float x, float y, Srgba8 color) noexcept {
        auto buffer = std::array<char, 20>{};
        const auto result = std::to_chars(buffer.data(), buffer.data() + buffer.size(), value);
        if (result.ec == std::errc{}) {
            text(
                std::string_view(
                    buffer.data(),
                    static_cast<std::size_t>(result.ptr - buffer.data())
                ),
                x,
                y,
                color
            );
        }
    };

    constexpr auto provenance_row = 14.0F;
    const auto provenance_space = provenance_height > 4.0F ? provenance_height - 4.0F : 0.0F;
    const auto provenance_rows =
        std::max(std::size_t{1}, static_cast<std::size_t>(provenance_space / provenance_row));
    const auto shown_provenance = std::min(view.provenance.size(), provenance_rows);
    for (auto index = std::size_t{}; index < shown_provenance; ++index) {
        const auto y = section_top + 2.0F + static_cast<float>(index) * provenance_row;
        const auto& entry = view.provenance[index];
        const auto layer_color = provenance_layer_color(theme, entry.layer);
        fill({.x = 5.0F, .y = y + 1.0F, .width = 8.0F, .height = 8.0F}, token_fill(theme, entry));
        stroke({.x = 4.0F, .y = y, .width = 10.0F, .height = 10.0F}, 1.0F, layer_color);

        const auto name = token_name(entry.kind, entry.token);
        const auto layer = theme_layer_name(entry.layer).substr(0, 3);
        const auto text_x = 18.0F;
        if (text_x + debug_text_width(name) + 4.0F + debug_text_width(layer) <=
            static_cast<float>(width)) {
            text(name, text_x, y, theme.color(tokens::color_text_primary));
            text(layer, text_x + debug_text_width(name) + 4.0F, y, layer_color);
        }
    }

    auto digit_count = [](std::uint32_t value) noexcept {
        auto digits = std::size_t{1};
        while (value >= 10) {
            value /= 10;
            ++digits;
        }
        return digits;
    };

    auto route_total = std::size_t{};
    for (const auto& event : view.trace) {
        if (event.kind == TraceEventKind::route) {
            ++route_total;
        }
    }

    const auto visible_rows =
        std::max(std::size_t{1}, static_cast<std::size_t>((event_height - 18.0F) / 14.0F));
    const auto max_scroll = route_total > visible_rows ? route_total - visible_rows : 0;
    const auto scroll = std::min(view.event_scroll, max_scroll);
    const auto route_end = route_total - scroll;
    const auto route_start = route_end > visible_rows ? route_end - visible_rows : 0;

    auto header_x = 4.0F;
    text("EVENTS", header_x, event_top + 1.0F, theme.color(tokens::color_text_disabled));
    header_x += debug_text_width("EVENTS") + 4.0F;
    number(route_start + 1, header_x, event_top + 1.0F, theme.color(tokens::color_text_disabled));
    header_x +=
        12.0F * static_cast<float>(digit_count(static_cast<std::uint32_t>(route_start + 1)));
    text("-", header_x, event_top + 1.0F, theme.color(tokens::color_text_disabled));
    header_x += debug_text_width("-");
    number(route_end, header_x, event_top + 1.0F, theme.color(tokens::color_text_disabled));
    header_x += 12.0F * static_cast<float>(digit_count(static_cast<std::uint32_t>(route_end)));
    text("/", header_x, event_top + 1.0F, theme.color(tokens::color_text_disabled));
    header_x += debug_text_width("/");
    number(route_total, header_x, event_top + 1.0F, theme.color(tokens::color_text_disabled));

    auto route_index = std::size_t{};
    auto row = std::size_t{};
    for (const auto& event : view.trace) {
        if (event.kind != TraceEventKind::route) {
            continue;
        }
        const auto current = route_index;
        ++route_index;
        if (current < route_start || current >= route_end) {
            continue;
        }

        const auto y = event_top + 17.0F + static_cast<float>(row) * 14.0F;
        ++row;
        auto x = 4.0F;
        const auto kind_color = theme.color(tokens::color_action_primary);
        const auto result_color = event.payload.route.route_result == 0
            ? theme.color(tokens::color_text_primary)
            : theme.color(tokens::color_error);
        const auto detail_color = theme.color(tokens::color_text_disabled);

        const auto kind_name =
            window_event_kind_name(static_cast<WindowEventKind>(event.payload.route.event_kind));
        text(kind_name, x, y, kind_color);
        x += debug_text_width(kind_name) + 4.0F;

        const auto result_name = window_route_result_name(
            static_cast<WindowRouteResult>(event.payload.route.route_result)
        );
        text(result_name, x, y, result_color);
        x += debug_text_width(result_name) + 4.0F;

        text("W", x, y, detail_color);
        x += debug_text_width("W");
        number(event.payload.route.window, x, y, detail_color);
        x += 12.0F *
            static_cast<float>(digit_count(static_cast<std::uint32_t>(event.payload.route.window)));
        x += 4.0F;

        const auto add_label = [&](std::string_view value) noexcept {
            text(value, x, y, detail_color);
            x += debug_text_width(value);
        };
        const auto add_value = [&](std::uint32_t value) noexcept {
            number(value, x, y, detail_color);
            x += 12.0F * static_cast<float>(digit_count(value));
        };
        const auto add_signed = [&](std::uint32_t bits) noexcept {
            const auto as_signed = static_cast<std::int32_t>(bits);
            if (as_signed < 0) {
                text("-", x, y, detail_color);
                x += debug_text_width("-");
                add_value(static_cast<std::uint32_t>(-static_cast<std::int64_t>(as_signed)));
            } else {
                add_value(static_cast<std::uint32_t>(as_signed));
            }
        };

        switch (static_cast<WindowEventKind>(event.payload.route.event_kind)) {
        case WindowEventKind::resize:
            add_label("W");
            add_value(event.payload.route.data1);
            add_label("H");
            add_value(event.payload.route.data2);
            break;
        case WindowEventKind::scale_change:
            add_label("SX");
            add_value(event.payload.route.data1);
            add_label("SY");
            add_value(event.payload.route.data2);
            break;
        case WindowEventKind::pointer_down:
        case WindowEventKind::pointer_up:
        case WindowEventKind::pointer_move:
        case WindowEventKind::pointer_cancel:
            add_label("X");
            add_signed(event.payload.route.data1);
            add_label("Y");
            add_signed(event.payload.route.data2);
            break;
        case WindowEventKind::wheel:
            add_label("DX");
            add_signed(event.payload.route.data1);
            add_label("DY");
            add_signed(event.payload.route.data2);
            break;
        case WindowEventKind::key_down:
        case WindowEventKind::key_up:
            add_label("K");
            add_value(event.payload.route.data1);
            add_label("M");
            add_value(event.payload.route.data2);
            break;
        case WindowEventKind::state_change:
            add_label("ST");
            add_value(event.payload.route.data1);
            break;
        case WindowEventKind::text_commit:
        case WindowEventKind::text_editing:
            add_label("N");
            add_value(event.payload.route.data1);
            break;
        default:
            break;
        }
    }

    if (route_total == 0) {
        text("NO EVENTS", 4.0F, event_top + 17.0F, theme.color(tokens::color_text_disabled));
    }

    if (max_scroll > 0) {
        const auto track_x = static_cast<float>(width) - 4.0F;
        const auto track_top = event_top + 17.0F;
        const auto track_height = std::max(4.0F, event_height - 20.0F);
        fill(
            {.x = track_x, .y = track_top, .width = 2.0F, .height = track_height},
            theme.color(tokens::color_action_disabled)
        );
        const auto thumb_height = std::max(
            4.0F,
            track_height * static_cast<float>(visible_rows) / static_cast<float>(route_total)
        );
        const auto thumb_ratio =
            static_cast<float>(max_scroll - scroll) / static_cast<float>(max_scroll);
        fill(
            {.x = track_x,
             .y = track_top + (track_height - thumb_height) * thumb_ratio,
             .width = 2.0F,
             .height = thumb_height},
            theme.color(tokens::color_action_primary)
        );
    }

    auto durations = std::array<std::uint32_t, 8>{};
    const auto duration_count = frame_durations(view.trace, durations);
    auto longest = std::uint32_t{1};
    for (auto index = std::size_t{}; index < duration_count; ++index) {
        longest = std::max(longest, durations[index]);
    }
    text(
        "MS",
        static_cast<float>(width) - 24.0F,
        timing_top + 1.0F,
        theme.color(tokens::color_text_disabled)
    );
    if (duration_count == 0) {
        text("NO FRAMES", 4.0F, timing_top + 1.0F, theme.color(tokens::color_text_disabled));
    } else {
        const auto bar_width = (static_cast<float>(width) - 8.0F) / 8.0F;
        const auto bar_area = std::max(1.0F, timing_height - 18.0F);
        for (auto index = std::size_t{}; index < duration_count; ++index) {
            const auto ratio = static_cast<float>(durations[index]) / static_cast<float>(longest);
            const auto bar_height = std::max(1.0F, bar_area * ratio);
            const auto x = 4.0F + static_cast<float>(index) * bar_width;
            fill(
                {.x = x,
                 .y = timing_top + timing_height - bar_height,
                 .width = std::max(1.0F, bar_width - 2.0F),
                 .height = bar_height},
                theme.color(tokens::color_action_primary)
            );
            if (bar_width >= 24.0F) {
                number(
                    durations[index],
                    x + 1.0F,
                    timing_top + 1.0F,
                    theme.color(tokens::color_text_disabled)
                );
            }
        }
    }

    display.count = placed;
    return display;
}

inline SoftwareRenderResult render_inspector(
    const Theme& theme,
    const InspectorView& view,
    std::span<SoftwarePixel> pixels,
    std::size_t width,
    std::size_t height
) noexcept {
    if (pixels.size() < width * height || width == 0 || height == 0) {
        return SoftwareRenderResult::invalid_surface;
    }

    const auto display = build_inspector_display(theme, view, width, height);
    if (display.count == 0 || display.overflowed) {
        return SoftwareRenderResult::invalid_input;
    }

    auto clips = std::array<SoftwareClipRect, 1>{};
    auto translations = std::array<LayoutPoint, 1>{};
    const SoftwareSurface surface{
        .width = width,
        .height = height,
        .stride = width,
        .pixels = pixels,
    };
    const SoftwareRenderWorkspace workspace{
        .clips = clips,
        .translations = translations,
    };
    return render_software(
        std::span<const DisplayCommand>(display.commands.data(), display.count),
        surface,
        workspace
    );
}

}

#endif
