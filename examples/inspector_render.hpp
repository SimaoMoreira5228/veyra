#ifndef VEYRA_EXAMPLES_INSPECTOR_RENDER_HPP
#define VEYRA_EXAMPLES_INSPECTOR_RENDER_HPP

#include "veyra/core/semantics/semantics.hpp"
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
    NodeId focused{};
    bool window_focused{};
};

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

    constexpr auto header_height = 24.0F;
    auto commands = std::array<DisplayCommand, 96>{};
    auto placed = std::size_t{};
    auto push = [&](DisplayCommand command) noexcept {
        if (placed < commands.size()) {
            commands[placed] = command;
            ++placed;
        }
    };

    auto clips = std::array<SoftwareClipRect, 1>{};
    auto translations = std::array<LayoutPoint, 1>{};
    auto flush = [&]() noexcept {
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
            std::span<const DisplayCommand>(commands.data(), placed),
            surface,
            workspace
        );
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

    if (view.nodes.empty()) {
        return flush();
    }

    const auto& root = view.nodes.front();
    if (root.bounds.width <= 0.0F || root.bounds.height <= 0.0F) {
        return flush();
    }

    const auto content_height = static_cast<float>(height) - header_height;
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
            std::min(node.bounds.height * scale, static_cast<float>(height) - y);
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

    return flush();
}

}

#endif
