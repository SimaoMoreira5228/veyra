#include "veyra/render/software_renderer.hpp"

#include <algorithm>
#include <array>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

void require(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

bool pixel_is(
    const veyra::SoftwarePixel& pixel,
    std::uint8_t red,
    std::uint8_t green,
    std::uint8_t blue,
    std::uint8_t alpha = 255
) {
    return pixel.red == red && pixel.green == green && pixel.blue == blue && pixel.alpha == alpha;
}

void check_nested_translations() {
    std::array<veyra::SoftwarePixel, 8> pixels{};
    std::array<veyra::SoftwareClipRect, 1> clips{};
    std::array<veyra::LayoutPoint, 2> translations{};
    const std::array<veyra::DisplayCommand, 8> commands{
        veyra::PushTranslation{
            .offset = {.x = 1.0F},
        },
        veyra::PushClip{
            .bounds = {.width = 3.0F, .height = 1.0F},
            .clip_id = 0,
        },
        veyra::PushTranslation{
            .offset = {.x = 1.0F},
        },
        veyra::FillRect{
            .bounds = {.width = 1.0F, .height = 1.0F},
            .color = {.red = 20, .green = 30, .blue = 40},
        },
        veyra::PopTranslation{},
        veyra::FillRect{
            .bounds = {.width = 1.0F, .height = 1.0F},
            .color = {.red = 50, .green = 60, .blue = 70},
        },
        veyra::PopClip{},
        veyra::PopTranslation{},
    };
    const auto result = veyra::render_software(
        commands,
        {
            .width = pixels.size(),
            .height = 1,
            .stride = pixels.size(),
            .pixels = pixels,
        },
        {
            .clips = clips,
            .translations = translations,
        }
    );

    require(result == veyra::SoftwareRenderResult::complete, "nested translations render");
    require(pixel_is(pixels[0], 0, 0, 0), "translation leaves earlier pixels unchanged");
    require(pixel_is(pixels[1], 50, 60, 70), "translated clip starts at its translated origin");
    require(pixel_is(pixels[2], 20, 30, 40), "nested translation accumulates offsets");
    require(pixel_is(pixels[3], 0, 0, 0), "popping translation restores the outer offset");
    require(pixel_is(pixels[4], 0, 0, 0), "translated clip retains its width");

    std::array<veyra::SoftwarePixel, 4> fractional_pixels{};
    const std::array<std::uint8_t, 1> coverage{255};
    const auto mask = veyra::SoftwareMask{
        .width = 1,
        .height = 1,
        .stride = 1,
        .coverage = coverage,
        .generation = 0,
    };
    const std::array<veyra::DisplayCommand, 3> fractional_commands{
        veyra::PushTranslation{
            .offset = {.x = 0.5F},
        },
        veyra::DrawMask{
            .mask = &mask,
            .color = {.red = 80, .green = 90, .blue = 100},
        },
        veyra::PopTranslation{},
    };
    std::array<veyra::LayoutPoint, 1> fractional_translations{};
    const auto fractional_result = veyra::render_software(
        fractional_commands,
        {
            .width = fractional_pixels.size(),
            .height = 1,
            .stride = fractional_pixels.size(),
            .pixels = fractional_pixels,
        },
        {
            .translations = fractional_translations,
        }
    );
    require(fractional_result == veyra::SoftwareRenderResult::complete, "fractional mask renders");
    require(pixel_is(fractional_pixels[0], 80, 90, 100), "fractional mask samples pixel centers");
    require(pixel_is(fractional_pixels[1], 80, 90, 100), "fractional mask covers touched pixels");

    std::array<veyra::SoftwarePixel, 4> untouched_pixels{};
    const auto insufficient_workspace_result = veyra::render_software(
        fractional_commands,
        {
            .width = untouched_pixels.size(),
            .height = 1,
            .stride = untouched_pixels.size(),
            .pixels = untouched_pixels,
        },
        {}
    );
    require(
        insufficient_workspace_result == veyra::SoftwareRenderResult::insufficient_workspace,
        "translation scratch space is required"
    );
    require(
        std::all_of(
            untouched_pixels.begin(),
            untouched_pixels.end(),
            [](const auto& pixel) {
                return pixel_is(pixel, 0, 0, 0);
            }
        ),
        "insufficient translation scratch leaves the surface unchanged"
    );

    const std::array<veyra::DisplayCommand, 1> unmatched_pop{veyra::PopTranslation{}};
    require(
        veyra::render_software(
            unmatched_pop,
            {
                .width = fractional_pixels.size(),
                .height = 1,
                .stride = fractional_pixels.size(),
                .pixels = fractional_pixels,
            },
            {}
        ) == veyra::SoftwareRenderResult::invalid_input,
        "unmatched translation pop is rejected"
    );

    const std::array<veyra::DisplayCommand, 3> unclosed_push{
        fractional_commands[0],
        fractional_commands[1],
        fractional_commands[1],
    };
    require(
        veyra::render_software(
            unclosed_push,
            {
                .width = fractional_pixels.size(),
                .height = 1,
                .stride = fractional_pixels.size(),
                .pixels = fractional_pixels,
            },
            {
                .translations = fractional_translations,
            }
        ) == veyra::SoftwareRenderResult::invalid_input,
        "unclosed translation push is rejected"
    );
}

void check_translated_strokes_and_images() {
    std::array<veyra::SoftwarePixel, 25> pixels{};
    const std::array<veyra::SoftwarePixel, 1> image_pixels{
        veyra::SoftwarePixel{
            .red = 10,
            .green = 20,
            .blue = 30,
        },
    };
    const auto image = veyra::SoftwareImage{
        .width = 1,
        .height = 1,
        .stride = 1,
        .pixels = image_pixels,
        .generation = 0,
    };
    const std::array<veyra::DisplayCommand, 5> commands{
        veyra::PushTranslation{
            .offset = {.x = 1.0F, .y = 1.0F},
        },
        veyra::StrokeRect{
            .bounds = {.width = 1.0F, .height = 1.0F},
            .thickness = 1.0F,
            .color = {.red = 40, .green = 50, .blue = 60},
        },
        veyra::StrokeLine{
            .start = {.x = 1.0F, .y = 3.5F},
            .end = {.x = 2.0F, .y = 3.5F},
            .thickness = 1.0F,
            .color = {.red = 70, .green = 80, .blue = 90},
        },
        veyra::DrawImage{
            .source = {.width = 1.0F, .height = 1.0F},
            .destination = {.x = 2.0F, .y = 2.0F, .width = 1.0F, .height = 1.0F},
            .image = &image,
        },
        veyra::PopTranslation{},
    };
    std::array<veyra::LayoutPoint, 1> translations{};
    const auto result = veyra::render_software(
        commands,
        {
            .width = 5,
            .height = 5,
            .stride = 5,
            .pixels = pixels,
        },
        {
            .translations = translations,
        }
    );

    require(result == veyra::SoftwareRenderResult::complete, "translated strokes and image render");
    require(pixel_is(pixels[6], 40, 50, 60), "translated border moves to its offset");
    require(pixel_is(pixels[22], 70, 80, 90), "translated line moves both endpoints");
    require(pixel_is(pixels[18], 10, 20, 30), "translated image destination moves");
}

}

int main() {
    try {
        check_nested_translations();
        check_translated_strokes_and_images();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
