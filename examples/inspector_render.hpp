#ifndef VEYRA_EXAMPLES_INSPECTOR_RENDER_HPP
#define VEYRA_EXAMPLES_INSPECTOR_RENDER_HPP

#include "veyra/core/semantics/semantics.hpp"
#include "veyra/inspector/trace/trace_recorder.hpp"
#include "veyra/render/cpu/software_renderer.hpp"
#include "veyra/style/theme/theme.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace veyra {

struct InspectorView {
    std::span<const SemanticNode> nodes;
    std::span<const TraceEvent> trace{};
    NodeId focused{};
    bool window_focused{};
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

struct InspectorDisplay {
    std::array<DisplayCommand, 96> commands{};
    std::size_t count{};
    bool overflowed{};
};

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

    constexpr auto header_height = 24.0F;
    constexpr auto trace_height = 18.0F;
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

    push(
        FillRect{
            .bounds = {.width = static_cast<float>(width), .height = static_cast<float>(height)},
            .color = theme.color(tokens::color_surface),
        }
    );
    push(
        FillRect{
            .bounds = {.width = static_cast<float>(width), .height = header_height},
            .color = theme.color(tokens::color_action_primary),
        }
    );
    push(
        StrokeRect{
            .bounds =
                {.x = 0.5F,
                 .y = 0.5F,
                 .width = static_cast<float>(width) - 1.0F,
                 .height = static_cast<float>(height) - 1.0F},
            .thickness = 1.0F,
            .color = view.window_focused ? theme.color(tokens::color_action_hovered)
                                         : theme.color(tokens::color_action_disabled),
        }
    );

    const auto trace_top = static_cast<float>(height) - trace_height;
    push(
        FillRect{
            .bounds = {.y = trace_top, .width = static_cast<float>(width), .height = 1.0F},
            .color = theme.color(tokens::color_action_disabled),
        }
    );

    constexpr auto block_width = 5.0F;
    constexpr auto block_gap = 1.0F;
    const auto trace_events = view.trace;
    for (auto index = std::size_t{}; index < trace_events.size(); ++index) {
        const auto x = 4.0F + static_cast<float>(index) * (block_width + block_gap);
        if (x + block_width > static_cast<float>(width)) {
            break;
        }
        push(
            FillRect{
                .bounds = {.x = x, .y = trace_top + 5.0F, .width = block_width, .height = 8.0F},
                .color = trace_color(theme, trace_events[index].kind),
            }
        );
    }

    if (view.nodes.empty()) {
        display.count = placed;
        return display;
    }

    const auto& root = view.nodes.front();
    if (root.bounds.width <= 0.0F || root.bounds.height <= 0.0F) {
        display.count = placed;
        return display;
    }

    const auto content_height = static_cast<float>(height) - header_height - trace_height;
    const auto scale = std::min(
        static_cast<float>(width) / root.bounds.width,
        content_height / root.bounds.height
    );

    for (const auto& node : view.nodes) {
        if (node.bounds.width <= 0.0F || node.bounds.height <= 0.0F) {
            continue;
        }

        const auto x = (node.bounds.x - root.bounds.x) * scale;
        const auto y = header_height + (node.bounds.y - root.bounds.y) * scale;
        const auto node_width = std::min(node.bounds.width * scale, static_cast<float>(width) - x);
        const auto node_height =
            std::min(node.bounds.height * scale, header_height + content_height - y);
        if (node_width <= 0.0F || node_height <= 0.0F) {
            continue;
        }

        const auto focused = node.id == view.focused;
        push(
            StrokeRect{
                .bounds = {.x = x, .y = y, .width = node_width, .height = node_height},
                .thickness = 1.0F,
                .color = focused ? theme.color(tokens::color_action_hovered)
                                 : theme.color(tokens::color_action_disabled),
            }
        );
        if (node.id != root.id) {
            constexpr auto inset = 2.0F;
            push(
                FillRect{
                    .bounds =
                        {.x = x + inset,
                         .y = y + inset,
                         .width = std::max(0.0F, node_width - inset * 2.0F),
                         .height = std::max(0.0F, node_height - inset * 2.0F)},
                    .color = focused ? theme.color(tokens::color_action_primary)
                                     : theme.color(tokens::color_text_disabled),
                }
            );
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
