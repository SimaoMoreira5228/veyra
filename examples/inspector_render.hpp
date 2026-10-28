#ifndef VEYRA_EXAMPLES_INSPECTOR_RENDER_HPP
#define VEYRA_EXAMPLES_INSPECTOR_RENDER_HPP

#include "veyra/render/cpu/software_renderer.hpp"
#include "veyra/style/theme/theme.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace veyra {

struct InspectorView {
    std::size_t selected{};
    bool focused{};
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

    auto commands = std::array<DisplayCommand, 32>{};
    auto placed = std::size_t{};
    auto push = [&](DisplayCommand command) noexcept {
        if (placed < commands.size()) {
            commands[placed] = command;
            ++placed;
        }
    };

    push(
        FillRect{
            .bounds = {.width = static_cast<float>(width), .height = static_cast<float>(height)},
            .color = theme.color(tokens::color_surface),
        }
    );
    push(
        FillRect{
            .bounds = {.width = static_cast<float>(width), .height = 24.0F},
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
            .color = view.focused ? theme.color(tokens::color_action_hovered)
                                  : theme.color(tokens::color_action_disabled),
        }
    );

    const auto row_height = 18.0F;
    const auto row_top = 34.0F;
    for (auto index = std::size_t{}; index < 6; ++index) {
        const auto top = row_top + static_cast<float>(index) * row_height;
        const auto selected = index == view.selected;
        push(
            FillRect{
                .bounds =
                    {.x = 8.0F,
                     .y = top,
                     .width = static_cast<float>(width) - 16.0F,
                     .height = row_height - 3.0F},
                .color = selected ? theme.color(tokens::color_action_primary)
                                  : theme.color(tokens::color_surface),
            }
        );
        push(
            FillRect{
                .bounds =
                    {.x = 14.0F,
                     .y = top + 6.0F,
                     .width = 34.0F + static_cast<float>((index * 17U) % 96U),
                     .height = 4.0F},
                .color = selected ? theme.color(tokens::color_action_on_primary)
                                  : theme.color(tokens::color_text_disabled),
            }
        );
        push(
            FillRect{
                .bounds =
                    {.x = 64.0F,
                     .y = top + 11.0F,
                     .width = 24.0F + static_cast<float>((index * 29U) % 118U),
                     .height = 2.0F},
                .color = selected ? theme.color(tokens::color_action_on_primary)
                                  : theme.color(tokens::color_text_primary),
            }
        );
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
        std::span<const DisplayCommand>(commands.data(), placed),
        surface,
        workspace
    );
}

}

#endif
