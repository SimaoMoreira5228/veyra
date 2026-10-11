#include "veyra/behaviors/pressable.hpp"
#include "veyra/behaviors/range.hpp"
#include "veyra/behaviors/toggle.hpp"
#include "veyra/render/software_renderer.hpp"
#include "veyra/text/text_editor_buffer.hpp"
#include "veyra/widgets/basic/button.hpp"
#include "veyra/widgets/basic/textfield.hpp"
#include "veyra/widgets/basic/toggle.hpp"

#include <array>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

void require(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

void check_button_recipes_track_state() {
    const veyra::Theme theme;
    const auto idle = veyra::PressableController{};
    require(
        veyra::button_appearance(theme, idle.state()).background ==
            theme.color(veyra::tokens::color_action_primary),
        "an idle button shows the primary action color"
    );
    auto disabled = veyra::PressableController{};
    disabled.set_disabled(true);
    require(
        veyra::button_appearance(theme, disabled.state()).background ==
            theme.color(veyra::tokens::color_action_disabled),
        "a disabled button shows the disabled color"
    );
    auto pressed = veyra::PressableController{};
    pressed.pointer_enter();
    pressed.pointer_down(1, true);
    require(
        veyra::button_appearance(theme, pressed.state()).background ==
            theme.color(veyra::tokens::color_action_pressed),
        "a pressed button shows the pressed color"
    );
}

void check_toggle_recipes_track_state() {
    const veyra::Theme theme;
    auto toggle = veyra::ToggleController{};
    toggle.configure({.value = veyra::ToggleValue::checked});
    require(
        veyra::checkbox_appearance(theme, toggle.state()).box ==
            theme.color(veyra::tokens::color_toggle_checked),
        "a checked box shows the checked color"
    );
    auto field_toggle = veyra::ToggleController{};
    field_toggle.configure({.kind = veyra::ToggleKind::toggle_switch});
    require(
        veyra::switch_appearance(theme, field_toggle.state()).track ==
            theme.color(veyra::tokens::color_surface),
        "an unchecked switch shows the surface color"
    );
}

void check_textfield_invalid_border() {
    const veyra::Theme theme;
    std::array<char, 64> storage{};
    veyra::TextEditorBuffer buffer(std::span<char>{storage}, "");
    auto field = veyra::EditableTextController{buffer};
    field.set_invalid(true);
    require(
        veyra::textfield_appearance(theme, field.state()).border ==
            theme.color(veyra::tokens::color_error),
        "an invalid field shows the error border"
    );
}

void check_range_fractions_drive_thumbs() {
    auto slider = veyra::RangeController{};
    require(
        slider.configure({.minimum = 0.0, .maximum = 100.0, .step = 1.0, .value = 25.0}),
        "the gallery slider domain configures cleanly"
    );
    require(slider.fraction() == 0.25, "a quarter value reads back a quarter fraction");
    auto band = veyra::RangeSliderController{};
    require(
        band.configure({.minimum = 0.0, .maximum = 100.0, .step = 1.0, .low = 25.0, .high = 75.0}),
        "the gallery range domain configures cleanly"
    );
    require(
        band.fraction(veyra::RangeThumb::low) == 0.25 &&
            band.fraction(veyra::RangeThumb::high) == 0.75,
        "the range thumbs sit at the quarter points"
    );
}

void check_gallery_snapshot() {
    const veyra::Theme theme;
    const auto idle = veyra::PressableController{};
    const veyra::Srgba8 button = veyra::button_appearance(theme, idle.state()).background;
    auto toggle = veyra::ToggleController{};
    toggle.configure({.value = veyra::ToggleValue::checked});
    const veyra::Srgba8 checked = veyra::checkbox_appearance(theme, toggle.state()).box;
    constexpr auto width = std::size_t{6};
    constexpr auto height = std::size_t{2};
    const std::array<veyra::DisplayCommand, 3> commands{
        veyra::FillRect{
            .bounds = {.width = static_cast<float>(width), .height = static_cast<float>(height)},
            .color = theme.color(veyra::tokens::color_surface),
        },
        veyra::FillRect{
            .bounds = {.x = 1.0F, .width = 2.0F, .height = 1.0F},
            .color = button,
        },
        veyra::FillRect{
            .bounds = {.x = 3.0F, .y = 1.0F, .width = 2.0F, .height = 1.0F},
            .color = checked,
        },
    };
    const veyra::Srgba8 surface = theme.color(veyra::tokens::color_surface);
    const std::array<veyra::SoftwarePixel, width * height> expected{
        veyra::SoftwarePixel{.red = surface.red, .green = surface.green, .blue = surface.blue},
        veyra::SoftwarePixel{.red = button.red, .green = button.green, .blue = button.blue},
        veyra::SoftwarePixel{.red = button.red, .green = button.green, .blue = button.blue},
        veyra::SoftwarePixel{.red = surface.red, .green = surface.green, .blue = surface.blue},
        veyra::SoftwarePixel{.red = surface.red, .green = surface.green, .blue = surface.blue},
        veyra::SoftwarePixel{.red = surface.red, .green = surface.green, .blue = surface.blue},
        veyra::SoftwarePixel{.red = surface.red, .green = surface.green, .blue = surface.blue},
        veyra::SoftwarePixel{.red = surface.red, .green = surface.green, .blue = surface.blue},
        veyra::SoftwarePixel{.red = surface.red, .green = surface.green, .blue = surface.blue},
        veyra::SoftwarePixel{.red = checked.red, .green = checked.green, .blue = checked.blue},
        veyra::SoftwarePixel{.red = checked.red, .green = checked.green, .blue = checked.blue},
        veyra::SoftwarePixel{.red = surface.red, .green = surface.green, .blue = surface.blue},
    };
    std::array<veyra::SoftwarePixel, width * height> pixels{};
    std::array<veyra::SoftwareClipRect, 1> clips{};
    std::array<veyra::LayoutPoint, 1> translations{};
    const auto result = veyra::render_software(
        commands,
        {
            .width = width,
            .height = height,
            .stride = width,
            .pixels = pixels,
        },
        {.clips = clips, .translations = translations}
    );
    require(result == veyra::SoftwareRenderResult::complete, "the gallery scene renders");
    for (std::size_t index = 0; index < pixels.size(); ++index) {
        require(
            pixels[index].red == expected[index].red &&
                pixels[index].green == expected[index].green &&
                pixels[index].blue == expected[index].blue &&
                pixels[index].alpha == expected[index].alpha,
            "recipe colors land on exact pixels"
        );
    }
}

}

int main() {
    try {
        check_button_recipes_track_state();
        check_toggle_recipes_track_state();
        check_textfield_invalid_border();
        check_range_fractions_drive_thumbs();
        check_gallery_snapshot();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
