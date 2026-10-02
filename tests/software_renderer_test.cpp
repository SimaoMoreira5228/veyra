#include "veyra/render/software_renderer.hpp"

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

bool pixel_is(
    const veyra::SoftwarePixel& pixel,
    std::uint8_t red,
    std::uint8_t green,
    std::uint8_t blue,
    std::uint8_t alpha = 255
) {
    return pixel.red == red && pixel.green == green && pixel.blue == blue && pixel.alpha == alpha;
}

void check_fills_and_nested_clips() {
    constexpr std::size_t width = 5;
    constexpr std::size_t height = 4;
    constexpr std::size_t stride = 7;
    const auto initial = veyra::SoftwarePixel{
        .red = 1,
        .green = 2,
        .blue = 3,
        .alpha = 4,
    };
    std::array<veyra::SoftwarePixel, stride * height> pixels;
    pixels.fill(initial);
    const std::array commands{
        veyra::DisplayCommand{
            .bounds = {.x = -1.0F, .y = -1.0F, .width = 7.0F, .height = 6.0F},
            .color = {.red = 10, .green = 20, .blue = 30},
        },
        veyra::DisplayCommand{
            .type = veyra::DisplayCommandType::push_clip,
            .bounds = {.x = 1.0F, .y = 1.0F, .width = 3.0F, .height = 2.0F},
        },
        veyra::DisplayCommand{
            .bounds = {.x = 0.0F, .y = 0.0F, .width = 5.0F, .height = 4.0F},
            .color = {.red = 40, .green = 50, .blue = 60},
        },
        veyra::DisplayCommand{
            .type = veyra::DisplayCommandType::push_clip,
            .bounds = {.x = 2.0F, .y = 0.0F, .width = 2.0F, .height = 3.0F},
        },
        veyra::DisplayCommand{
            .bounds = {.x = 0.0F, .y = 0.0F, .width = 5.0F, .height = 4.0F},
            .color = {.red = 70, .green = 80, .blue = 90},
        },
        veyra::DisplayCommand{.type = veyra::DisplayCommandType::pop_clip},
        veyra::DisplayCommand{.type = veyra::DisplayCommandType::pop_clip},
    };
    std::array<veyra::SoftwareClipRect, 2> clips{};
    const auto surface = veyra::SoftwareSurface{
        .width = width,
        .height = height,
        .stride = stride,
        .pixels = pixels,
    };
    const auto workspace = veyra::SoftwareRenderWorkspace{.clips = clips};

    const auto result = veyra::render_software(commands, surface, workspace);
    require(result == veyra::SoftwareRenderResult::complete, "nested clip list renders");
    require(pixel_is(pixels[0], 10, 20, 30), "surface edge receives the first fill");
    require(pixel_is(pixels[1 * stride + 1], 40, 50, 60), "outer clip receives its fill");
    require(pixel_is(pixels[1 * stride + 2], 70, 80, 90), "nested clip intersects its parent");
    require(pixel_is(pixels[2 * stride + 3], 70, 80, 90), "nested clip covers its full bounds");
    require(pixel_is(pixels[2 * stride + 1], 40, 50, 60), "pop restores the parent clip");
    require(
        pixel_is(pixels[3 * stride], 10, 20, 30),
        "pixels outside clips preserve earlier fills"
    );
    require(pixel_is(pixels[5], 1, 2, 3, 4), "row padding remains untouched");
    require(pixel_is(pixels[stride + 6], 1, 2, 3, 4), "each row uses the supplied stride");
    require(pixel_is(pixels[0], 10, 20, 30, 255), "filled pixels are opaque RGBA");
}

void check_damage_regions() {
    constexpr std::size_t width = 4;
    constexpr std::size_t height = 3;
    std::array<veyra::SoftwarePixel, width * height> pixels;
    const auto initial = veyra::SoftwarePixel{
        .red = 1,
        .green = 2,
        .blue = 3,
    };
    pixels.fill(initial);
    const std::array commands{
        veyra::DisplayCommand{
            .bounds = {.width = 4.0F, .height = 3.0F},
            .color = {.red = 10, .green = 20, .blue = 30},
        },
    };
    const std::array damage{
        veyra::LayoutRect{.x = 1.0F, .y = 1.0F, .width = 2.0F, .height = 1.0F},
    };
    const auto surface = veyra::SoftwareSurface{
        .width = width,
        .height = height,
        .stride = width,
        .pixels = pixels,
    };

    const auto result = veyra::render_software(commands, surface, {}, damage);
    require(result == veyra::SoftwareRenderResult::complete, "damage-limited rendering completes");
    require(pixel_is(pixels[width + 1], 10, 20, 30), "damage region is redrawn");
    require(pixel_is(pixels[width + 2], 10, 20, 30), "damage includes its final column");
    require(pixel_is(pixels[0], 1, 2, 3), "pixels outside damage remain unchanged");
    require(pixel_is(pixels[2 * width + 1], 1, 2, 3), "damage excludes its final row");

    const std::span<const veyra::LayoutRect> no_damage;
    require(
        veyra::render_software(commands, surface, {}, no_damage) ==
            veyra::SoftwareRenderResult::complete,
        "empty damage performs no drawing"
    );
    require(pixel_is(pixels[0], 1, 2, 3), "empty damage leaves pixels unchanged");
}

void check_fractional_bounds() {
    std::array<veyra::SoftwarePixel, 4> pixels{};
    const std::array commands{
        veyra::DisplayCommand{
            .bounds = {.x = 0.5F, .y = 0.5F, .width = 1.0F, .height = 1.0F},
            .color = {.red = 10, .green = 20, .blue = 30},
        },
    };
    const auto surface = veyra::SoftwareSurface{
        .width = 2,
        .height = 2,
        .stride = 2,
        .pixels = pixels,
    };

    const auto result = veyra::render_software(commands, surface, {});
    require(result == veyra::SoftwareRenderResult::complete, "fractional rectangle renders");
    for (const auto& pixel : pixels) {
        require(pixel_is(pixel, 10, 20, 30), "fractional bounds cover touched pixels");
    }
}

void check_alpha_compositing() {
    std::array<veyra::SoftwarePixel, 3> pixels{
        veyra::SoftwarePixel{
            .red = 20,
            .green = 40,
            .blue = 60,
            .alpha = 128,
        },
        veyra::SoftwarePixel{
            .red = 11,
            .green = 22,
            .blue = 33,
            .alpha = 44,
        },
        veyra::SoftwarePixel{
            .red = 20,
            .green = 40,
            .blue = 60,
            .alpha = 255,
        },
    };
    const std::array commands{
        veyra::DisplayCommand{
            .bounds = {.width = 1.0F, .height = 1.0F},
            .color = {.red = 200, .green = 100, .blue = 0, .alpha = 128},
        },
        veyra::DisplayCommand{
            .bounds = {.x = 1.0F, .width = 1.0F, .height = 1.0F},
            .color = {.red = 255, .green = 0, .blue = 0, .alpha = 0},
        },
        veyra::DisplayCommand{
            .bounds = {.x = 2.0F, .width = 1.0F, .height = 1.0F},
            .color = {.red = 200, .green = 100, .blue = 0, .alpha = 128},
        },
    };
    const auto surface = veyra::SoftwareSurface{
        .width = 3,
        .height = 1,
        .stride = 3,
        .pixels = pixels,
    };

    const auto result = veyra::render_software(commands, surface, {});
    require(result == veyra::SoftwareRenderResult::complete, "translucent fills render");
    require(pixel_is(pixels[0], 140, 80, 20, 192), "source-over blends RGBA channels");
    require(pixel_is(pixels[1], 11, 22, 33, 44), "fully transparent fill preserves destination");
    require(pixel_is(pixels[2], 110, 70, 30, 255), "source-over blends opaque destinations");
}

void check_overlapping_damage_is_rejected() {
    std::array<veyra::SoftwarePixel, 3> pixels{
        veyra::SoftwarePixel{.red = 1, .green = 2, .blue = 3},
        veyra::SoftwarePixel{.red = 4, .green = 5, .blue = 6},
        veyra::SoftwarePixel{.red = 7, .green = 8, .blue = 9},
    };
    const auto before = pixels;
    const std::array commands{
        veyra::DisplayCommand{
            .bounds = {.width = 3.0F, .height = 1.0F},
            .color = {.red = 100, .green = 110, .blue = 120, .alpha = 128},
        },
    };
    const std::array overlapping_damage{
        veyra::LayoutRect{.x = 0.25F, .y = 0.25F, .width = 0.5F, .height = 0.5F},
        veyra::LayoutRect{.x = 0.75F, .y = 0.25F, .width = 0.5F, .height = 0.5F},
    };
    const auto surface = veyra::SoftwareSurface{
        .width = 3,
        .height = 1,
        .stride = 3,
        .pixels = pixels,
    };

    const auto result = veyra::render_software(commands, surface, {}, overlapping_damage);
    require(result == veyra::SoftwareRenderResult::invalid_input, "overlapping damage is rejected");
    for (std::size_t index = 0; index < pixels.size(); ++index) {
        require(
            pixel_is(
                pixels[index],
                before[index].red,
                before[index].green,
                before[index].blue,
                before[index].alpha
            ),
            "overlapping damage rejection leaves the surface unchanged"
        );
    }
}

void check_invalid_input_is_transactional() {
    constexpr std::size_t width = 3;
    constexpr std::size_t height = 2;
    const auto initial = veyra::SoftwarePixel{
        .red = 5,
        .green = 6,
        .blue = 7,
    };
    std::array<veyra::SoftwarePixel, width * height> pixels;
    pixels.fill(initial);
    const auto surface = veyra::SoftwareSurface{
        .width = width,
        .height = height,
        .stride = width,
        .pixels = pixels,
    };
    const std::array valid_fill{
        veyra::DisplayCommand{
            .bounds = {.width = 3.0F, .height = 2.0F},
            .color = {.red = 10, .green = 20, .blue = 30},
        },
    };
    const std::array unmatched_pop{
        veyra::DisplayCommand{.type = veyra::DisplayCommandType::pop_clip},
        valid_fill[0],
    };
    const std::array unclosed_push{
        veyra::DisplayCommand{
            .type = veyra::DisplayCommandType::push_clip,
            .bounds = {.width = 3.0F, .height = 2.0F},
        },
        valid_fill[0],
    };
    const std::array clipped_fill{
        veyra::DisplayCommand{
            .type = veyra::DisplayCommandType::push_clip,
            .bounds = {.width = 3.0F, .height = 2.0F},
        },
        valid_fill[0],
        veyra::DisplayCommand{.type = veyra::DisplayCommandType::pop_clip},
    };
    const std::array invalid_bounds{
        veyra::DisplayCommand{
            .bounds = {.width = -1.0F, .height = 2.0F},
            .color = {.red = 10, .green = 20, .blue = 30},
        },
    };
    std::array<veyra::SoftwareClipRect, 1> clips{};
    const auto workspace = veyra::SoftwareRenderWorkspace{.clips = clips};
    const auto untouched = [&pixels] {
        for (const auto& pixel : pixels) {
            if (!pixel_is(pixel, 5, 6, 7)) {
                return false;
            }
        }
        return true;
    };

    require(
        veyra::render_software(unmatched_pop, surface, workspace) ==
            veyra::SoftwareRenderResult::invalid_input,
        "unmatched clip pop is rejected"
    );
    require(untouched(), "invalid clip stack leaves the surface untouched");
    require(
        veyra::render_software(unclosed_push, surface, workspace) ==
            veyra::SoftwareRenderResult::invalid_input,
        "unclosed clip push is rejected"
    );
    require(untouched(), "unclosed clip stack leaves the surface untouched");
    require(
        veyra::render_software(invalid_bounds, surface, workspace) ==
            veyra::SoftwareRenderResult::invalid_input,
        "negative rectangle extent is rejected"
    );
    require(untouched(), "invalid geometry leaves the surface untouched");
    require(
        veyra::render_software(clipped_fill, surface, {}) ==
            veyra::SoftwareRenderResult::insufficient_workspace,
        "short clip scratch is reported before rendering"
    );
    require(untouched(), "short clip scratch leaves the surface untouched");

    const std::array invalid_damage{
        veyra::LayoutRect{.x = 0.0F, .y = 0.0F, .width = 3.0F, .height = -1.0F},
    };
    require(
        veyra::render_software(valid_fill, surface, workspace, invalid_damage) ==
            veyra::SoftwareRenderResult::invalid_input,
        "negative damage extent is rejected"
    );
    require(untouched(), "invalid damage leaves the surface untouched");

    const auto short_surface = veyra::SoftwareSurface{
        .width = width,
        .height = height,
        .stride = width,
        .pixels = std::span<veyra::SoftwarePixel>(pixels).first(pixels.size() - 1),
    };
    require(
        veyra::render_software(valid_fill, short_surface, workspace) ==
            veyra::SoftwareRenderResult::invalid_surface,
        "short pixel storage is rejected"
    );
    require(untouched(), "invalid surface storage leaves the available pixels untouched");
}

}

int main() {
    try {
        check_fills_and_nested_clips();
        check_damage_regions();
        check_fractional_bounds();
        check_alpha_compositing();
        check_overlapping_damage_is_rejected();
        check_invalid_input_is_transactional();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
