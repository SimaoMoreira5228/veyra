#include "veyra/behaviors/control/pressable.hpp"
#include "veyra/behaviors/input/radiogroup.hpp"
#include "veyra/behaviors/input/range.hpp"
#include "veyra/behaviors/control/toggle.hpp"
#include "veyra/render/cpu/software_renderer.hpp"
#include "veyra/text/text_editor_buffer.hpp"
#include "veyra/widgets/basic/button.hpp"
#include "veyra/widgets/basic/radio.hpp"
#include "veyra/widgets/basic/textfield.hpp"
#include "veyra/widgets/basic/toggle.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

constexpr double kBaselineScales[]{1.0, 1.25, 1.5, 2.0};

// NOTE: every fractional-coverage path quantizes coverage to 8 bits and the
// over operator rounds once, so a blended channel lands within 2 LSB of the
// source-destination segment over an opaque background.
constexpr int kCoverageTolerance = 2;

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

void check_radio_recipes_track_state() {
    const veyra::Theme theme;
    constexpr std::array<veyra::RadioOption, 2> options{{
        {.id = 1},
        {.id = 2, .disabled = true},
    }};

    auto group = veyra::RadioGroupController{};
    group.configure({.options = options, .selected = 1});

    const auto picked = veyra::radio_appearance(theme, group.state(), true);
    require(
        picked.ring == theme.color(veyra::tokens::color_toggle_checked) &&
            picked.dot == theme.color(veyra::tokens::color_toggle_on_checked),
        "a selected radio shows the checked ring and dot"
    );

    const auto skipped = veyra::radio_appearance(theme, group.state(), false);
    require(
        skipped.ring == theme.color(veyra::tokens::color_text_disabled),
        "an unselected radio shows the muted ring"
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

veyra::LayoutRect scaled_rect(float x, float y, float width, float height, double scale) {
    const auto factor = static_cast<float>(scale);
    return {.x = x * factor, .y = y * factor, .width = width * factor, .height = height * factor};
}

void render_scene(
    std::span<const veyra::DisplayCommand> commands,
    std::span<veyra::SoftwarePixel> pixels,
    std::size_t width,
    std::size_t height
) {
    std::array<veyra::SoftwareClipRect, 4> clips{};
    std::array<veyra::LayoutPoint, 4> translations{};
    const veyra::SoftwareSurface surface{
        .width = width,
        .height = height,
        .stride = width,
        .pixels = pixels,
    };
    const auto result =
        veyra::render_software(commands, surface, {.clips = clips, .translations = translations});
    require(result == veyra::SoftwareRenderResult::complete, "the scaled scene renders");
}

void render_solid_baseline(
    double scale,
    std::span<veyra::SoftwarePixel> pixels,
    std::size_t width,
    std::size_t height,
    veyra::Srgba8 surface,
    veyra::Srgba8 button,
    veyra::Srgba8 checked
) {
    const std::array<veyra::DisplayCommand, 3> commands{
        veyra::FillRect{.bounds = scaled_rect(0.0F, 0.0F, 8.0F, 8.0F, scale), .color = surface},
        veyra::FillRect{.bounds = scaled_rect(0.0F, 0.0F, 4.0F, 4.0F, scale), .color = button},
        veyra::FillRect{.bounds = scaled_rect(4.0F, 4.0F, 4.0F, 4.0F, scale), .color = checked},
    };
    render_scene(commands, pixels, width, height);
}

void check_scaled_solids_stay_exact() {
    const veyra::Theme theme;
    const veyra::Srgba8 surface = theme.color(veyra::tokens::color_surface);
    const auto idle = veyra::PressableController{};
    const veyra::Srgba8 button = veyra::button_appearance(theme, idle.state()).background;
    auto toggle = veyra::ToggleController{};
    toggle.configure({.value = veyra::ToggleValue::checked});
    const veyra::Srgba8 checked = veyra::checkbox_appearance(theme, toggle.state()).box;
    for (const double scale : kBaselineScales) {
        const auto width = static_cast<std::size_t>(std::lround(8.0 * scale));
        std::array<veyra::SoftwarePixel, 16 * 16> pixels{};
        render_solid_baseline(
            scale,
            std::span(pixels).first(width * width),
            width,
            width,
            surface,
            button,
            checked
        );
        const auto edge = static_cast<std::size_t>(std::lround(4.0 * scale));
        for (std::size_t y = 0; y < width; ++y) {
            for (std::size_t x = 0; x < width; ++x) {
                const veyra::Srgba8 want =
                    x < edge && y < edge ? button : (x >= edge && y >= edge ? checked : surface);
                const auto& pixel = pixels[y * width + x];
                require(
                    pixel.red == want.red && pixel.green == want.green && pixel.blue == want.blue &&
                        pixel.alpha == 255,
                    "integer-mapped solids stay byte-exact at every baseline scale"
                );
            }
        }
    }
}

void check_double_scale_matches_blocks() {
    const veyra::Theme theme;
    const veyra::Srgba8 surface = theme.color(veyra::tokens::color_surface);
    const auto idle = veyra::PressableController{};
    const veyra::Srgba8 button = veyra::button_appearance(theme, idle.state()).background;
    auto toggle = veyra::ToggleController{};
    toggle.configure({.value = veyra::ToggleValue::checked});
    const veyra::Srgba8 checked = veyra::checkbox_appearance(theme, toggle.state()).box;
    std::array<veyra::SoftwarePixel, 8 * 8> single{};
    render_solid_baseline(1.0, single, 8, 8, surface, button, checked);
    std::array<veyra::SoftwarePixel, 16 * 16> doubled{};
    render_solid_baseline(2.0, doubled, 16, 16, surface, button, checked);
    for (std::size_t y = 0; y < 8; ++y) {
        for (std::size_t x = 0; x < 8; ++x) {
            const auto& want = single[y * 8 + x];
            for (std::size_t dy = 0; dy < 2; ++dy) {
                for (std::size_t dx = 0; dx < 2; ++dx) {
                    const auto& pixel = doubled[(2 * y + dy) * 16 + 2 * x + dx];
                    require(
                        pixel.red == want.red && pixel.green == want.green &&
                            pixel.blue == want.blue && pixel.alpha == want.alpha,
                        "scale 2.0 repeats every scale 1.0 pixel as a 2x2 block"
                    );
                }
            }
        }
    }
}

bool within_segment(std::uint8_t value, std::uint8_t end_a, std::uint8_t end_b) {
    const auto low = std::min(end_a, end_b);
    const auto high = std::max(end_a, end_b);
    const int floor = low < kCoverageTolerance ? 0 : low - kCoverageTolerance;
    const int ceiling = high > 255 - kCoverageTolerance ? 255 : high + kCoverageTolerance;
    return value >= floor && value <= ceiling;
}

bool same_pixel(veyra::SoftwarePixel pixel, veyra::Srgba8 color) {
    return pixel.red == color.red && pixel.green == color.green && pixel.blue == color.blue &&
        pixel.alpha == 255;
}

void check_fractional_clip_and_overlap() {
    constexpr double scale = 1.25;
    const veyra::Theme theme;
    const veyra::Srgba8 surface = theme.color(veyra::tokens::color_surface);
    const auto idle = veyra::PressableController{};
    const veyra::Srgba8 face = veyra::button_appearance(theme, idle.state()).background;
    auto toggle = veyra::ToggleController{};
    toggle.configure({.value = veyra::ToggleValue::checked});
    const veyra::Srgba8 top = veyra::checkbox_appearance(theme, toggle.state()).box;
    const std::array<veyra::DisplayCommand, 5> commands{
        veyra::FillRect{.bounds = scaled_rect(0.0F, 0.0F, 8.0F, 8.0F, scale), .color = surface},
        veyra::PushClip{.bounds = scaled_rect(1.0F, 1.0F, 6.0F, 6.0F, scale), .clip_id = 7},
        veyra::StrokeLine{
            .start = {.x = static_cast<float>(0.0 * scale), .y = static_cast<float>(0.0 * scale)},
            .end = {.x = static_cast<float>(8.0 * scale), .y = static_cast<float>(8.0 * scale)},
            .thickness = static_cast<float>(1.6 * scale),
            .color = face,
        },
        veyra::PopClip{},
        veyra::FillRect{.bounds = scaled_rect(4.0F, 4.0F, 4.0F, 4.0F, scale), .color = top},
    };
    std::array<veyra::SoftwarePixel, 10 * 10> pixels{};
    render_scene(commands, pixels, 10, 10);
    auto blended = false;
    for (std::size_t y = 0; y < 10; ++y) {
        for (std::size_t x = 0; x < 10; ++x) {
            const auto& pixel = pixels[y * 10 + x];
            if (x >= 5 && y >= 5) {
                require(same_pixel(pixel, top), "the integer-mapped overlap stays byte-exact");
                continue;
            }
            require(
                within_segment(pixel.red, surface.red, face.red) &&
                    within_segment(pixel.green, surface.green, face.green) &&
                    within_segment(pixel.blue, surface.blue, face.blue) && pixel.alpha == 255,
                "fractional clip coverage stays within 2 LSB of the blend segment"
            );
            if (!same_pixel(pixel, face) && !same_pixel(pixel, surface)) {
                blended = true;
            }
        }
    }
    require(
        same_pixel(pixels[9 * 10 + 0], surface) && same_pixel(pixels[0 * 10 + 9], surface),
        "the mid-pixel clip keeps paint off far corners"
    );
    require(blended, "the fractional edge actually blends");
}

}

int main() {
    try {
        check_button_recipes_track_state();
        check_toggle_recipes_track_state();
        check_radio_recipes_track_state();
        check_textfield_invalid_border();
        check_range_fractions_drive_thumbs();
        check_gallery_snapshot();
        check_scaled_solids_stay_exact();
        check_double_scale_matches_blocks();
        check_fractional_clip_and_overlap();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
