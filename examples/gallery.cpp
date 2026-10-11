#include "veyra/behaviors/datagrid.hpp"
#include "veyra/behaviors/editable_text.hpp"
#include "veyra/behaviors/menu.hpp"
#include "veyra/behaviors/pressable.hpp"
#include "veyra/behaviors/range.hpp"
#include "veyra/behaviors/tabs.hpp"
#include "veyra/behaviors/toggle.hpp"
#include "veyra/render/software_renderer.hpp"
#include "veyra/text/text_editor_buffer.hpp"
#include "veyra/widgets/basic/button.hpp"
#include "veyra/widgets/basic/textfield.hpp"
#include "veyra/widgets/basic/toggle.hpp"

#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

constexpr std::size_t kMargin = 16;
constexpr std::size_t kRowGap = 14;
constexpr std::size_t kMaxDimension = 2048;
constexpr std::size_t kSceneWidth = 528;
constexpr std::size_t kMinWidth = 560;
constexpr std::size_t kMinHeight = 416;
constexpr float kButtonWidth = 96.0F;
constexpr float kButtonHeight = 32.0F;
constexpr float kButtonStride = 104.0F;
constexpr float kFieldWidth = 120.0F;
constexpr float kFieldHeight = 28.0F;
constexpr float kFieldStride = 132.0F;
constexpr float kTrackWidth = 160.0F;
constexpr float kTrackStride = 184.0F;
constexpr float kTabWidth = 96.0F;
constexpr float kTabStride = 104.0F;

struct Options {
    const char* output{};
    std::size_t width{kMinWidth};
    std::size_t height{kMinHeight};
};

std::size_t parse_size(const char* value) {
    const std::string_view text(value);
    std::size_t parsed = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), parsed);
    if (error != std::errc{} || end != text.data() + text.size()) {
        throw std::invalid_argument("expected a non-negative integer");
    }
    return parsed;
}

void check_dimension(std::size_t value, std::size_t minimum, std::string_view name) {
    if (value < minimum || value > kMaxDimension) {
        throw std::invalid_argument(
            "gallery " + std::string(name) + " must fit the scene and stay within bounds"
        );
    }
}

Options parse_options(int argc, char** argv) {
    if (argc < 2 || argc > 4) {
        throw std::invalid_argument("usage: veyra_gallery <output.ppm> [width height]");
    }
    auto options = Options{};
    options.output = argv[1];
    if (std::string_view(options.output).empty()) {
        throw std::invalid_argument("gallery output path must not be empty");
    }
    if (argc >= 3) {
        options.width = parse_size(argv[2]);
    }
    if (argc == 4) {
        options.height = parse_size(argv[3]);
    }
    check_dimension(options.width, kMinWidth, "width");
    check_dimension(options.height, kMinHeight, "height");
    return options;
}

void push_button(
    std::vector<veyra::DisplayCommand>& commands,
    const veyra::Theme& theme,
    float x,
    float y,
    veyra::PressableState state
) {
    const veyra::ButtonAppearance appearance = veyra::button_appearance(theme, state);
    const veyra::LayoutRect bounds =
        {.x = x, .y = y, .width = kButtonWidth, .height = kButtonHeight};
    commands.push_back(
        veyra::FillRoundedRect{
            .bounds = bounds,
            .radius = appearance.corner_radius.value,
            .color = appearance.background,
        }
    );
    if (state.focused) {
        commands.push_back(
            veyra::StrokeRoundedRect{
                .bounds =
                    {.x = x - 2.0F, .y = y - 2.0F, .width = kButtonWidth + 4.0F, .height = 36.0F},
                .radius = appearance.corner_radius.value + 2.0F,
                .thickness = 2.0F,
                .color = theme.color(veyra::tokens::color_action_primary),
            }
        );
    }
}

float emit_buttons(
    std::vector<veyra::DisplayCommand>& commands,
    const veyra::Theme& theme,
    float x,
    float y
) {
    for (std::size_t index = 0; index < 5; ++index) {
        auto press = veyra::PressableController{};
        if (index == 1 || index == 2) {
            press.pointer_enter();
        }
        if (index == 2) {
            press.pointer_down(1, true);
        }
        if (index == 3) {
            press.set_disabled(true);
        }
        if (index == 4) {
            press.set_focused(true);
        }
        push_button(
            commands,
            theme,
            x + static_cast<float>(index) * kButtonStride,
            y + 2.0F,
            press.state()
        );
    }
    return y + 36.0F;
}

void push_checkbox(
    std::vector<veyra::DisplayCommand>& commands,
    const veyra::Theme& theme,
    float x,
    float y,
    veyra::ToggleState state
) {
    const veyra::CheckboxAppearance appearance = veyra::checkbox_appearance(theme, state);
    const float size = appearance.box_size.value;
    commands.push_back(
        veyra::FillRoundedRect{
            .bounds = {.x = x, .y = y, .width = size, .height = size},
            .radius = appearance.corner_radius.value,
            .color = appearance.box,
        }
    );
    if (state.value == veyra::ToggleValue::checked) {
        commands.push_back(
            veyra::FillRect{
                .bounds =
                    {.x = x + 4.0F, .y = y + 4.0F, .width = size - 8.0F, .height = size - 8.0F},
                .color = appearance.mark,
            }
        );
    }
    if (state.value == veyra::ToggleValue::mixed) {
        commands.push_back(
            veyra::FillRect{
                .bounds =
                    {.x = x + 3.0F,
                     .y = y + size / 2.0F - 2.0F,
                     .width = size - 6.0F,
                     .height = 4.0F},
                .color = appearance.mark,
            }
        );
    }
}

float emit_checkboxes(
    std::vector<veyra::DisplayCommand>& commands,
    const veyra::Theme& theme,
    float x,
    float y
) {
    for (std::size_t index = 0; index < 4; ++index) {
        auto toggle = veyra::ToggleController{};
        if (index == 1) {
            toggle.configure({.value = veyra::ToggleValue::checked});
        }
        if (index == 2) {
            toggle.configure({.value = veyra::ToggleValue::mixed, .allow_mixed = true});
        }
        if (index == 3) {
            toggle.set_disabled(true);
        }
        push_checkbox(
            commands,
            theme,
            x + static_cast<float>(index) * 32.0F,
            y + 2.0F,
            toggle.state()
        );
    }
    return y + 20.0F;
}

void push_switch(
    std::vector<veyra::DisplayCommand>& commands,
    const veyra::Theme& theme,
    float x,
    float y,
    veyra::ToggleState state
) {
    const veyra::SwitchAppearance appearance = veyra::switch_appearance(theme, state);
    const float width = appearance.track_width.value;
    const float height = appearance.track_height.value;
    const float thumb = height - 4.0F;
    const bool on =
        state.value == veyra::ToggleValue::checked || state.value == veyra::ToggleValue::mixed;
    commands.push_back(
        veyra::FillRoundedRect{
            .bounds = {.x = x, .y = y, .width = width, .height = height},
            .radius = appearance.corner_radius.value,
            .color = appearance.track,
        }
    );
    const float thumb_x = on ? x + width - 2.0F - thumb : x + 2.0F;
    commands.push_back(
        veyra::FillRoundedRect{
            .bounds = {.x = thumb_x, .y = y + 2.0F, .width = thumb, .height = thumb},
            .radius = thumb / 2.0F,
            .color = appearance.thumb,
        }
    );
}

float emit_switches(
    std::vector<veyra::DisplayCommand>& commands,
    const veyra::Theme& theme,
    float x,
    float y
) {
    for (std::size_t index = 0; index < 3; ++index) {
        auto toggle = veyra::ToggleController{};
        toggle.configure({.kind = veyra::ToggleKind::toggle_switch});
        if (index == 1) {
            toggle.configure(
                {.kind = veyra::ToggleKind::toggle_switch, .value = veyra::ToggleValue::checked}
            );
        }
        if (index == 2) {
            toggle.set_disabled(true);
        }
        push_switch(
            commands,
            theme,
            x + static_cast<float>(index) * 48.0F,
            y + 2.0F,
            toggle.state()
        );
    }
    return y + 20.0F;
}

float emit_textfields(
    std::vector<veyra::DisplayCommand>& commands,
    const veyra::Theme& theme,
    float x,
    float y
) {
    std::array<char, 256> storage{};
    veyra::TextEditorBuffer buffer(std::span<char>{storage}, "");
    for (std::size_t index = 0; index < 4; ++index) {
        auto field = veyra::EditableTextController{buffer};
        if (index == 1) {
            field.set_focused(true);
        }
        if (index == 2) {
            field.set_invalid(true);
        }
        if (index == 3) {
            field.set_disabled(true);
        }
        const veyra::TextFieldAppearance appearance =
            veyra::textfield_appearance(theme, field.state());
        const float cell_x = x + static_cast<float>(index) * kFieldStride;
        commands.push_back(
            veyra::FillRoundedRect{
                .bounds = {.x = cell_x, .y = y, .width = kFieldWidth, .height = kFieldHeight},
                .radius = appearance.corner_radius.value,
                .color = appearance.box_fill,
            }
        );
        commands.push_back(
            veyra::StrokeRoundedRect{
                .bounds = {.x = cell_x, .y = y, .width = kFieldWidth, .height = kFieldHeight},
                .radius = appearance.corner_radius.value,
                .thickness = field.focused() ? 2.0F : 1.0F,
                .color = appearance.border,
            }
        );
        if (field.focused()) {
            const float caret_x = cell_x + appearance.horizontal_padding.value;
            commands.push_back(
                veyra::StrokeLine{
                    .start = {.x = caret_x, .y = y + 7.0F},
                    .end = {.x = caret_x, .y = y + 21.0F},
                    .thickness = 2.0F,
                    .color = appearance.caret,
                }
            );
        }
    }
    return y + kFieldHeight;
}

void push_thumb(
    std::vector<veyra::DisplayCommand>& commands,
    const veyra::Theme& theme,
    float center_x,
    float y
) {
    const veyra::LayoutRect bounds =
        {.x = center_x - 6.0F, .y = y, .width = 12.0F, .height = 16.0F};
    commands.push_back(
        veyra::FillRoundedRect{
            .bounds = bounds,
            .radius = 6.0F,
            .color = theme.color(veyra::tokens::color_action_on_primary),
        }
    );
    commands.push_back(
        veyra::StrokeRoundedRect{
            .bounds = bounds,
            .radius = 6.0F,
            .thickness = 2.0F,
            .color = theme.color(veyra::tokens::color_action_primary),
        }
    );
}

float emit_sliders(
    std::vector<veyra::DisplayCommand>& commands,
    const veyra::Theme& theme,
    float x,
    float y
) {
    constexpr std::array<double, 3> values{25.0, 50.0, 75.0};
    for (std::size_t index = 0; index < values.size(); ++index) {
        auto slider = veyra::RangeController{};
        slider.configure({.minimum = 0.0, .maximum = 100.0, .step = 1.0, .value = values[index]});
        const float fraction = static_cast<float>(slider.fraction());
        const float track_x = x + static_cast<float>(index) * kTrackStride;
        commands.push_back(
            veyra::FillRoundedRect{
                .bounds = {.x = track_x, .y = y + 5.0F, .width = kTrackWidth, .height = 6.0F},
                .radius = 3.0F,
                .color = theme.color(veyra::tokens::color_action_disabled),
            }
        );
        commands.push_back(
            veyra::FillRoundedRect{
                .bounds =
                    {.x = track_x, .y = y + 5.0F, .width = kTrackWidth * fraction, .height = 6.0F},
                .radius = 3.0F,
                .color = theme.color(veyra::tokens::color_action_primary),
            }
        );
        push_thumb(commands, theme, track_x + kTrackWidth * fraction, y);
    }
    return y + 20.0F;
}

float emit_range(
    std::vector<veyra::DisplayCommand>& commands,
    const veyra::Theme& theme,
    float x,
    float y
) {
    auto band = veyra::RangeSliderController{};
    band.configure({.minimum = 0.0, .maximum = 100.0, .step = 1.0, .low = 25.0, .high = 75.0});
    const float low = static_cast<float>(band.fraction(veyra::RangeThumb::low));
    const float high = static_cast<float>(band.fraction(veyra::RangeThumb::high));
    constexpr float width = 320.0F;
    commands.push_back(
        veyra::FillRoundedRect{
            .bounds = {.x = x, .y = y + 5.0F, .width = width, .height = 6.0F},
            .radius = 3.0F,
            .color = theme.color(veyra::tokens::color_action_disabled),
        }
    );
    commands.push_back(
        veyra::FillRect{
            .bounds =
                {.x = x + width * low,
                 .y = y + 5.0F,
                 .width = width * (high - low),
                 .height = 6.0F},
            .color = theme.color(veyra::tokens::color_action_primary),
        }
    );
    push_thumb(commands, theme, x + width * low, y);
    push_thumb(commands, theme, x + width * high, y);
    return y + 20.0F;
}

float emit_tabs(
    std::vector<veyra::DisplayCommand>& commands,
    const veyra::Theme& theme,
    float x,
    float y
) {
    constexpr std::array<std::uint64_t, 3> members{1, 2, 3};
    constexpr std::array<std::uint64_t, 1> disabled{3};
    auto tabs = veyra::TabsController{};
    tabs.select(1, members, disabled);
    const bool has_selected = tabs.has_selected();
    const std::uint64_t selected = tabs.selected();
    for (std::size_t index = 0; index < members.size(); ++index) {
        const std::uint64_t id = members[index];
        const bool is_selected = has_selected && selected == id;
        const bool is_disabled = id == disabled[0];
        const float cell_x = x + static_cast<float>(index) * kTabStride;
        const veyra::LayoutRect bounds = {.x = cell_x, .y = y, .width = kTabWidth, .height = 28.0F};
        veyra::Srgba8 fill = theme.color(veyra::tokens::color_surface);
        if (is_disabled) {
            fill = theme.color(veyra::tokens::color_action_disabled);
        } else if (is_selected) {
            fill = theme.color(veyra::tokens::color_action_primary);
        }
        commands.push_back(
            veyra::FillRoundedRect{
                .bounds = bounds,
                .radius = theme.radius(veyra::tokens::radius_small).value,
                .color = fill,
            }
        );
        if (!is_selected && !is_disabled) {
            commands.push_back(
                veyra::StrokeRoundedRect{
                    .bounds = bounds,
                    .radius = theme.radius(veyra::tokens::radius_small).value,
                    .thickness = 1.0F,
                    .color = theme.color(veyra::tokens::color_text_disabled),
                }
            );
        }
    }
    return y + 28.0F;
}

float emit_grid_header(
    std::vector<veyra::DisplayCommand>& commands,
    const veyra::Theme& theme,
    float x,
    float y
) {
    constexpr std::array<veyra::DataGridColumn, 3> spec{{
        {.id = 1, .width = 100.0F, .min_width = 40.0F, .max_width = 200.0F, .sortable = true},
        {.id = 2, .width = 80.0F, .min_width = 40.0F, .max_width = 200.0F, .sortable = true},
        {.id = 3, .width = 120.0F, .min_width = 40.0F, .max_width = 200.0F},
    }};
    auto grid = veyra::DataGridController{};
    grid.set_columns(spec);
    grid.sort_by(2, veyra::SortDirection::ascending);
    const veyra::SortSpec sort = grid.sort_spec();
    float cursor = x;
    for (const auto& column : spec) {
        const float width = grid.column_width(column.id);
        commands.push_back(
            veyra::FillRect{
                .bounds = {.x = cursor, .y = y, .width = width, .height = 28.0F},
                .color = theme.color(veyra::tokens::color_surface),
            }
        );
        commands.push_back(
            veyra::StrokeRect{
                .bounds = {.x = cursor, .y = y, .width = width, .height = 28.0F},
                .thickness = 1.0F,
                .color = theme.color(veyra::tokens::color_text_disabled),
            }
        );
        if (sort.has_sort && sort.column == column.id) {
            commands.push_back(
                veyra::FillRect{
                    .bounds =
                        {.x = cursor + width - 14.0F,
                         .y = y + 10.0F,
                         .width = 8.0F,
                         .height = 8.0F},
                    .color = theme.color(veyra::tokens::color_action_primary),
                }
            );
        }
        cursor += width;
    }
    return y + 28.0F;
}

float emit_menu(
    std::vector<veyra::DisplayCommand>& commands,
    const veyra::Theme& theme,
    float x,
    float y
) {
    constexpr std::array<veyra::MenuItem, 3> items{{
        {.id = 1},
        {.id = 2},
        {.id = 3, .disabled = true},
    }};
    auto menu = veyra::MenuController{};
    menu.open_root(7, items);
    menu.hover(2, items);
    const veyra::MenuActive active = menu.active();
    constexpr float width = 160.0F;
    constexpr float height = 72.0F;
    commands.push_back(
        veyra::FillRect{
            .bounds = {.x = x, .y = y, .width = width, .height = height},
            .color = theme.color(veyra::tokens::color_surface),
        }
    );
    for (std::size_t index = 0; index < items.size(); ++index) {
        const float row_y = y + static_cast<float>(index) * 24.0F;
        if (active.has && active.id == items[index].id) {
            commands.push_back(
                veyra::FillRect{
                    .bounds = {.x = x, .y = row_y, .width = width, .height = 24.0F},
                    .color = theme.color(veyra::tokens::color_action_hovered),
                }
            );
        } else if (items[index].disabled) {
            commands.push_back(
                veyra::FillRect{
                    .bounds = {.x = x, .y = row_y, .width = width, .height = 24.0F},
                    .color = theme.color(veyra::tokens::color_action_disabled),
                }
            );
        }
    }
    commands.push_back(
        veyra::StrokeRect{
            .bounds = {.x = x, .y = y, .width = width, .height = height},
            .thickness = 1.0F,
            .color = theme.color(veyra::tokens::color_text_disabled),
        }
    );
    return y + height;
}

void build_scene(
    std::vector<veyra::DisplayCommand>& commands,
    const veyra::Theme& theme,
    std::size_t width,
    std::size_t height
) {
    commands.push_back(
        veyra::FillRect{
            .bounds = {.width = static_cast<float>(width), .height = static_cast<float>(height)},
            .color = theme.color(veyra::tokens::color_surface),
        }
    );
    const float x = static_cast<float>(kMargin);
    float y = static_cast<float>(kMargin);
    y = emit_buttons(commands, theme, x, y);
    y = emit_checkboxes(commands, theme, x, y + kRowGap);
    y = emit_switches(commands, theme, x, y + kRowGap);
    y = emit_textfields(commands, theme, x, y + kRowGap);
    y = emit_sliders(commands, theme, x, y + kRowGap);
    y = emit_range(commands, theme, x, y + kRowGap);
    y = emit_tabs(commands, theme, x, y + kRowGap);
    y = emit_grid_header(commands, theme, x, y + kRowGap);
    y = emit_menu(commands, theme, x, y + kRowGap);
    if (y > static_cast<float>(height - kMargin)) {
        throw std::runtime_error("gallery scene overflows its canvas");
    }
}

void write_ppm(
    const char* path,
    const std::vector<veyra::SoftwarePixel>& pixels,
    std::size_t width,
    std::size_t height
) {
    std::string body("P6\n");
    body += std::to_string(width);
    body += ' ';
    body += std::to_string(height);
    body += "\n255\n";
    body.reserve(body.size() + pixels.size() * 3);
    for (const auto& pixel : pixels) {
        body.push_back(static_cast<char>(pixel.red));
        body.push_back(static_cast<char>(pixel.green));
        body.push_back(static_cast<char>(pixel.blue));
    }
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        throw std::runtime_error("gallery output cannot be opened");
    }
    output.write(body.data(), static_cast<std::streamsize>(body.size()));
    output.flush();
    if (!output) {
        throw std::runtime_error("gallery output cannot be written");
    }
}

int run(int argc, char** argv) {
    const Options options = parse_options(argc, argv);
    const veyra::Theme theme;
    std::vector<veyra::DisplayCommand> commands;
    commands.reserve(128);
    build_scene(commands, theme, options.width, options.height);
    std::vector<veyra::SoftwarePixel> pixels(options.width * options.height);
    std::array<veyra::SoftwareClipRect, 8> clips{};
    std::array<veyra::LayoutPoint, 8> translations{};
    const veyra::SoftwareRenderWorkspace workspace{
        .clips = clips,
        .translations = translations,
    };
    const veyra::SoftwareSurface surface{
        .width = options.width,
        .height = options.height,
        .stride = options.width,
        .pixels = pixels,
    };
    const auto result = veyra::render_software(commands, surface, workspace);
    if (result != veyra::SoftwareRenderResult::complete) {
        throw std::runtime_error("gallery rendering failed");
    }
    write_ppm(options.output, pixels, options.width, options.height);
    return 0;
}

}

int main(int argc, char** argv) {
    try {
        return run(argc, argv);
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 2;
    }
}
