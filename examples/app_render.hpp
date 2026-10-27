#ifndef VEYRA_EXAMPLES_APP_RENDER_HPP
#define VEYRA_EXAMPLES_APP_RENDER_HPP

#include "app_model.hpp"
#include "veyra/behaviors/control/pressable.hpp"
#include "veyra/core/schedule/frame_scheduler.hpp"
#include "veyra/platform/headless/null_window.hpp"
#include "veyra/platform/run_loop.hpp"
#include "veyra/render/cpu/software_renderer.hpp"
#include "veyra/widgets/basic/button.hpp"
#include "veyra/widgets/basic/textfield.hpp"
#include "veyra/widgets/data/virtual_list.hpp"
#include "veyra/widgets/overlay/popover.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace veyra {

inline std::uint64_t app_checksum(std::span<const SoftwarePixel> pixels) noexcept {
    auto hash = std::uint64_t{1469598103934665603U};
    const auto* bytes = reinterpret_cast<const unsigned char*>(pixels.data());
    const auto count = pixels.size() * sizeof(SoftwarePixel);
    for (std::size_t index = 0; index < count; ++index) {
        hash ^= static_cast<std::uint64_t>(bytes[index]);
        hash *= 1099511628211U;
    }
    return hash;
}

inline SoftwareRenderResult render_app(
    const AppModel& model,
    std::span<SoftwarePixel> pixels,
    std::size_t width,
    std::size_t height
) noexcept {
    if (pixels.size() < width * height || width != kAppWidth || height != kAppHeight) {
        return SoftwareRenderResult::invalid_surface;
    }

    const Theme& theme = model.theme_;
    std::array<DisplayCommand, 256> commands{};
    std::size_t placed = 0;
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

    std::array<VirtualListRowState, kAppRowCount> rows{};
    for (std::size_t index = 0; index < kAppRowCount; ++index) {
        rows[index] = {
            .selected = index == model.selected_,
            .current = index == model.current_,
        };
    }
    std::array<DisplayCommand, 64> list_commands{};
    const VirtualListRowsOutput list_out = emit_virtual_list_rows(
        theme,
        rows,
        {.list_bounds = {.x = 16.0F, .y = 16.0F, .width = 200.0F, .height = 240.0F},
         .row_height = kAppRowHeight,
         .sticky_header_height = 0.0F},
        list_commands
    );
    for (std::size_t index = 0; index < list_out.placed; ++index) {
        push(list_commands[index]);
    }

    const EditableTextState field_state{
        .focused = model.chain_.focused() == kAppFocusField,
    };
    const TextFieldAppearance field_look = textfield_appearance(theme, field_state);
    push(
        FillRoundedRect{
            .bounds = {.x = 232.0F, .y = 16.0F, .width = 232.0F, .height = 28.0F},
            .radius = field_look.corner_radius.value,
            .color = field_look.box_fill,
        }
    );
    push(
        StrokeRoundedRect{
            .bounds = {.x = 232.0F, .y = 16.0F, .width = 232.0F, .height = 28.0F},
            .radius = field_look.corner_radius.value,
            .thickness = field_state.focused ? 2.0F : 1.0F,
            .color = field_look.border,
        }
    );

    if (model.font_source_ != AppFontSource::none) {
        std::array<char, 128> text{};
        std::size_t length = 0;
        if (model.field_buffer_.copy_text(text, length) == TextEditResult::complete) {
            const float pad = field_look.horizontal_padding.value;
            const std::size_t visible = length > 32 ? 32 : static_cast<std::size_t>(length);
            for (std::size_t index = 0; index < visible; ++index) {
                const unsigned char byte = static_cast<unsigned char>(text[index]);
                const float tick_height = 4.0F + static_cast<float>(byte % 5U);
                push(
                    FillRect{
                        .bounds =
                            {.x = 232.0F + pad + static_cast<float>(index) * 6.0F,
                             .y = 16.0F + 14.0F - tick_height / 2.0F,
                             .width = 4.0F,
                             .height = tick_height},
                        .color = field_look.text,
                    }
                );
            }
        }
    }

    if (field_state.focused) {
        std::array<char, 128> text{};
        std::size_t length = 0;
        model.field_buffer_.copy_text(text, length);
        const float caret_x =
            232.0F + field_look.horizontal_padding.value + static_cast<float>(length) * 6.0F;
        push(
            StrokeLine{
                .start = {.x = caret_x, .y = 22.0F},
                .end = {.x = caret_x, .y = 38.0F},
                .thickness = 2.0F,
                .color = field_look.caret,
            }
        );
    }

    const PressableState button_state{
        .pressed = model.button_pressed_,
        .focused = model.chain_.focused() == kAppFocusButton,
    };
    const ButtonAppearance button_look = button_appearance(theme, button_state);
    push(
        FillRoundedRect{
            .bounds = {.x = 232.0F, .y = 60.0F, .width = 120.0F, .height = 32.0F},
            .radius = button_look.corner_radius.value,
            .color = button_look.background,
        }
    );
    if (button_state.focused) {
        push(
            StrokeRoundedRect{
                .bounds = {.x = 230.0F, .y = 58.0F, .width = 124.0F, .height = 36.0F},
                .radius = button_look.corner_radius.value + 2.0F,
                .thickness = 2.0F,
                .color = theme.color(tokens::color_action_primary),
            }
        );
    }

    if (model.palette_.expanded()) {
        std::array<std::uint64_t, AppModel::kMatchCapacity> matches{};
        const std::size_t count = model.palette_.copy_matches(matches);
        const std::size_t shown = count > 2 ? 2 : count;
        const float panel_height = 28.0F + static_cast<float>(shown) * 22.0F;
        push(
            FillRect{
                .bounds = {.x = 90.0F, .y = 100.0F, .width = 300.0F, .height = panel_height},
                .color = theme.color(tokens::color_surface),
            }
        );
        push(
            StrokeRect{
                .bounds = {.x = 90.0F, .y = 100.0F, .width = 300.0F, .height = panel_height},
                .thickness = 1.0F,
                .color = theme.color(tokens::color_text_disabled),
            }
        );
        const PaletteActive active = model.palette_.active();
        for (std::size_t index = 0; index < shown; ++index) {
            if (active.has && active.id == matches[index]) {
                push(
                    FillRect{
                        .bounds =
                            {.x = 90.0F,
                             .y = 128.0F + static_cast<float>(index) * 22.0F,
                             .width = 300.0F,
                             .height = 22.0F},
                        .color = theme.color(tokens::color_action_hovered),
                    }
                );
            }
        }
    }

    if (model.dialog_open_) {
        std::array<DisplayCommand, 4> chrome{};
        const PopoverOutput popover = emit_popover(
            theme,
            {.panel = {.x = 90.0F, .y = 70.0F, .width = 300.0F, .height = 170.0F},
             .viewport = {.width = static_cast<float>(width), .height = static_cast<float>(height)},
             .modal = true},
            chrome
        );
        for (std::size_t index = 0; index < popover.placed; ++index) {
            push(chrome[index]);
        }
        push(
            FillRoundedRect{
                .bounds = {.x = 110.0F, .y = 190.0F, .width = 80.0F, .height = 28.0F},
                .radius = 4.0F,
                .color = theme.color(tokens::color_action_primary),
            }
        );
        push(
            StrokeRoundedRect{
                .bounds = {.x = 210.0F, .y = 190.0F, .width = 80.0F, .height = 28.0F},
                .radius = 4.0F,
                .thickness = 1.0F,
                .color = theme.color(tokens::color_text_disabled),
            }
        );
        if (model.submitted_length_ > 0) {
            const std::size_t visible = model.submitted_length_ > 16 ? 16 : model.submitted_length_;
            for (std::size_t index = 0; index < visible; ++index) {
                const unsigned char byte =
                    static_cast<unsigned char>(model.submitted_storage_[index]);
                const float tick_height = 4.0F + static_cast<float>(byte % 5U);
                push(
                    FillRect{
                        .bounds =
                            {.x = 110.0F + static_cast<float>(index) * 6.0F,
                             .y = 160.0F - tick_height / 2.0F,
                             .width = 4.0F,
                             .height = tick_height},
                        .color = theme.color(tokens::color_text_primary),
                    }
                );
            }
        }
    }

    std::array<SoftwareClipRect, 8> clips{};
    std::array<LayoutPoint, 8> translations{};
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

template <WindowBackend Backend>
    requires SoftwareSurfacePresenter<Backend>
LoopResult run_app(
    Backend& backend,
    AppModel& model,
    FrameScheduler& scheduler,
    std::span<SoftwarePixel> backing,
    void (*sleep_ms)(unsigned) noexcept,
    std::uint64_t frame_cap
) noexcept {
    scheduler.request_immediate();
    auto on_event = [&](WindowEvent& event) noexcept {
        const bool quit = model.handle_event(event);
        scheduler.request_immediate();
        return quit;
    };
    auto on_frame = [&](bool) noexcept {
        auto surface = SoftwareSurface{};
        if (backend.acquire_surface(surface, backing) != WindowResult::ready) {
            return FrameAction{.failed = true};
        }
        if (render_app(model, surface.pixels, surface.width, surface.height) !=
            SoftwareRenderResult::complete) {
            return FrameAction{.failed = true};
        }
        if (backend.present() != WindowResult::ready) {
            return FrameAction{.failed = true};
        }
        return FrameAction{.rendered = true, .quit = model.quit_requested()};
    };
    auto config = RunLoopConfig{backend, scheduler, on_event, on_frame, sleep_ms};
    config.frame_cap = frame_cap;
    return run_until_quit(config);
}

}

#endif
