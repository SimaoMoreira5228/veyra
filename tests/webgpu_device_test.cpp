#include "veyra/render/gpu/webgpu/webgpu_backend.hpp"
#include "veyra/render/resource/glyph_atlas.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
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

void check_full_surface_fill() {
    auto backend = veyra::WebGpuBackend{};
    if (!backend.create()) {
        std::cout << "no WebGPU adapter, skipping\n";
        std::exit(77);
    }

    constexpr auto width = std::uint32_t{64};
    constexpr auto height = std::uint32_t{32};
    constexpr auto color = veyra::DisplayColor{
        .red = 64,
        .green = 128,
        .blue = 192,
        .alpha = 255,
    };

    const auto commands = std::array<veyra::DisplayCommand, 1>{
        veyra::FillRect{
            .bounds = {.width = static_cast<float>(width), .height = static_cast<float>(height)},
            .color = color,
        },
    };

    auto expected = std::array<veyra::SoftwarePixel, static_cast<std::size_t>(width) * height>{};
    expected.fill({.alpha = 0});
    const veyra::SoftwareSurface surface{
        .width = width,
        .height = height,
        .stride = width,
        .pixels = expected,
    };
    const auto workspace = veyra::SoftwareRenderWorkspace{};
    require(
        veyra::render_software(commands, surface, workspace) ==
            veyra::SoftwareRenderResult::complete,
        "the software reference renders"
    );

    auto actual = std::array<veyra::SoftwarePixel, static_cast<std::size_t>(width) * height>{};
    const auto result = backend.render(commands, width, height, actual);
    require(result == veyra::WebGpuFrameResult::complete, "the WebGPU frame completes");

    for (std::size_t index = 0; index < actual.size(); ++index) {
        require(actual[index].red == expected[index].red, "red parity");
        require(actual[index].green == expected[index].green, "green parity");
        require(actual[index].blue == expected[index].blue, "blue parity");
        require(actual[index].alpha == expected[index].alpha, "alpha parity");
    }
}

void check_multi_rect_parity() {
    auto backend = veyra::WebGpuBackend{};
    if (!backend.create()) {
        std::exit(77);
    }

    constexpr auto width = std::uint32_t{32};
    constexpr auto height = std::uint32_t{24};
    const auto commands = std::array<veyra::DisplayCommand, 5>{
        veyra::FillRect{
            .bounds = {.x = 0.0F, .y = 0.0F, .width = 10.0F, .height = 8.0F},
            .color = {.red = 220, .green = 30, .blue = 40, .alpha = 255},
        },
        veyra::FillRect{
            .bounds = {.x = 5.0F, .y = 3.0F, .width = 12.0F, .height = 10.0F},
            .color = {.red = 20, .green = 200, .blue = 60, .alpha = 255},
        },
        veyra::FillRect{
            .bounds = {.x = 20.0F, .y = 10.0F, .width = 12.0F, .height = 14.0F},
            .color = {.red = 30, .green = 60, .blue = 230, .alpha = 255},
        },
        veyra::FillRect{
            .bounds = {.x = 2.0F, .y = 15.0F, .width = 6.0F, .height = 6.0F},
            .color = {.red = 250, .green = 210, .blue = 20, .alpha = 128},
        },
        veyra::FillRect{
            .bounds = {.x = 28.0F, .y = 20.0F, .width = 10.0F, .height = 10.0F},
            .color = {.red = 120, .green = 20, .blue = 180, .alpha = 255},
        },
    };

    auto expected = std::array<veyra::SoftwarePixel, static_cast<std::size_t>(width) * height>{};
    expected.fill({.alpha = 0});
    const veyra::SoftwareSurface surface{
        .width = width,
        .height = height,
        .stride = width,
        .pixels = expected,
    };
    require(
        veyra::render_software(commands, surface, veyra::SoftwareRenderWorkspace{}) ==
            veyra::SoftwareRenderResult::complete,
        "the software reference renders the multi-rect scene"
    );

    auto actual = std::array<veyra::SoftwarePixel, static_cast<std::size_t>(width) * height>{};
    require(
        backend.render(commands, width, height, actual) == veyra::WebGpuFrameResult::complete,
        "the WebGPU multi-rect scene completes"
    );

    for (std::size_t index = 0; index < actual.size(); ++index) {
        const auto difference = [](std::uint8_t left, std::uint8_t right) {
            return static_cast<int>(left) - static_cast<int>(right);
        };
        require(
            difference(actual[index].red, expected[index].red) >= -1 &&
                difference(actual[index].red, expected[index].red) <= 1,
            "red parity"
        );
        require(
            difference(actual[index].green, expected[index].green) >= -1 &&
                difference(actual[index].green, expected[index].green) <= 1,
            "green parity"
        );
        require(
            difference(actual[index].blue, expected[index].blue) >= -1 &&
                difference(actual[index].blue, expected[index].blue) <= 1,
            "blue parity"
        );
        require(
            difference(actual[index].alpha, expected[index].alpha) >= -1 &&
                difference(actual[index].alpha, expected[index].alpha) <= 1,
            "alpha parity"
        );
    }
}

void check_parity(
    std::span<const veyra::SoftwarePixel> actual,
    std::span<const veyra::SoftwarePixel> expected,
    std::string_view stage
) {
    for (std::size_t index = 0; index < actual.size(); ++index) {
        const auto difference = [](std::uint8_t left, std::uint8_t right) {
            return static_cast<int>(left) - static_cast<int>(right);
        };
        const auto alpha = std::max(actual[index].alpha, expected[index].alpha);
        const auto tolerance = alpha == 0 ? 1 : std::max(1, (255 + alpha - 1) / alpha);
        const auto close = [difference, tolerance](std::uint8_t left, std::uint8_t right) {
            const auto delta = difference(left, right);
            return delta >= -tolerance && delta <= tolerance;
        };
        require(
            close(actual[index].red, expected[index].red) &&
                close(actual[index].green, expected[index].green) &&
                close(actual[index].blue, expected[index].blue),
            stage
        );
        require(
            difference(actual[index].alpha, expected[index].alpha) >= -1 &&
                difference(actual[index].alpha, expected[index].alpha) <= 1,
            stage
        );
    }
}

void check_rounded_rect_parity() {
    auto backend = veyra::WebGpuBackend{};
    if (!backend.create()) {
        std::exit(77);
    }

    constexpr auto width = std::uint32_t{32};
    constexpr auto height = std::uint32_t{24};
    const auto commands = std::array<veyra::DisplayCommand, 2>{
        veyra::FillRoundedRect{
            .bounds = {.x = 2.0F, .y = 2.0F, .width = 20.0F, .height = 16.0F},
            .radius = 6.0F,
            .color = {.red = 40, .green = 180, .blue = 90, .alpha = 255},
        },
        veyra::FillRoundedRect{
            .bounds = {.x = 8.0F, .y = 8.0F, .width = 18.0F, .height = 12.0F},
            .radius = 4.0F,
            .color = {.red = 220, .green = 60, .blue = 30, .alpha = 180},
        },
    };

    auto expected = std::array<veyra::SoftwarePixel, static_cast<std::size_t>(width) * height>{};
    expected.fill({.alpha = 0});
    const veyra::SoftwareSurface surface{
        .width = width,
        .height = height,
        .stride = width,
        .pixels = expected,
    };
    require(
        veyra::render_software(commands, surface, veyra::SoftwareRenderWorkspace{}) ==
            veyra::SoftwareRenderResult::complete,
        "the software reference renders rounded rects"
    );

    auto actual = std::array<veyra::SoftwarePixel, static_cast<std::size_t>(width) * height>{};
    require(
        backend.render(commands, width, height, actual) == veyra::WebGpuFrameResult::complete,
        "the WebGPU rounded scene completes"
    );
    check_parity(actual, expected, "rounded rect parity");
}

void check_clip_parity() {
    auto backend = veyra::WebGpuBackend{};
    if (!backend.create()) {
        std::exit(77);
    }

    constexpr auto width = std::uint32_t{32};
    constexpr auto height = std::uint32_t{24};
    const auto commands = std::array<veyra::DisplayCommand, 4>{
        veyra::PushClip{
            .bounds = {.x = 4.0F, .y = 4.0F, .width = 18.0F, .height = 12.0F},
            .clip_id = 3,
        },
        veyra::FillRect{
            .bounds = {.x = 0.0F, .y = 0.0F, .width = 32.0F, .height = 24.0F},
            .color = {.red = 30, .green = 110, .blue = 210, .alpha = 255},
        },
        veyra::FillRoundedRect{
            .bounds = {.x = 10.0F, .y = 6.0F, .width = 16.0F, .height = 10.0F},
            .radius = 4.0F,
            .color = {.red = 230, .green = 160, .blue = 20, .alpha = 255},
        },
        veyra::PopClip{},
    };

    auto expected = std::array<veyra::SoftwarePixel, static_cast<std::size_t>(width) * height>{};
    expected.fill({.alpha = 0});
    const veyra::SoftwareSurface surface{
        .width = width,
        .height = height,
        .stride = width,
        .pixels = expected,
    };
    auto clips = std::array<veyra::SoftwareClipRect, 1>{};
    require(
        veyra::render_software(commands, surface, veyra::SoftwareRenderWorkspace{.clips = clips}) ==
            veyra::SoftwareRenderResult::complete,
        "the software reference renders the clipped scene"
    );

    auto actual = std::array<veyra::SoftwarePixel, static_cast<std::size_t>(width) * height>{};
    require(
        backend.render(commands, width, height, actual) == veyra::WebGpuFrameResult::complete,
        "the WebGPU clipped scene completes"
    );
    check_parity(actual, expected, "clip parity");
}

void check_mask_parity() {
    auto backend = veyra::WebGpuBackend{};
    if (!backend.create()) {
        std::exit(77);
    }

    constexpr auto width = std::uint32_t{24};
    constexpr auto height = std::uint32_t{20};
    const auto coverage = std::array<std::uint8_t, 20>{
        0, 32, 64, 96, 128, 160, 192, 224, 255, 200, 128, 64, 32, 0, 90, 180, 220, 250, 128, 40,
    };
    const veyra::SoftwareMask mask{
        .width = 5,
        .height = 4,
        .stride = 5,
        .coverage = coverage,
        .generation = 1,
    };
    const auto commands = std::array<veyra::DisplayCommand, 1>{
        veyra::DrawMask{
            .x = 3.0F,
            .y = 4.0F,
            .mask = &mask,
            .color = {.red = 200, .green = 100, .blue = 50, .alpha = 255},
        },
    };

    auto expected = std::array<veyra::SoftwarePixel, static_cast<std::size_t>(width) * height>{};
    expected.fill({.alpha = 0});
    const veyra::SoftwareSurface surface{
        .width = width,
        .height = height,
        .stride = width,
        .pixels = expected,
    };
    require(
        veyra::render_software(commands, surface, veyra::SoftwareRenderWorkspace{}) ==
            veyra::SoftwareRenderResult::complete,
        "the software reference renders a mask"
    );

    auto actual = std::array<veyra::SoftwarePixel, static_cast<std::size_t>(width) * height>{};
    require(
        backend.render(commands, width, height, actual) == veyra::WebGpuFrameResult::complete,
        "the WebGPU mask scene completes"
    );
    check_parity(actual, expected, "mask parity");
}

void check_glyph_parity() {
    auto backend = veyra::WebGpuBackend{};
    if (!backend.create()) {
        std::exit(77);
    }

    constexpr auto width = std::uint32_t{24};
    constexpr auto height = std::uint32_t{20};
    auto slots = std::array<veyra::GlyphAtlasSlot, 8>{};
    auto atlas_coverage = std::array<std::uint8_t, static_cast<std::size_t>(16U) * 16U>{};
    auto nodes = std::array<stbrp_node, 16>{};
    auto atlas = veyra::GlyphAtlas(slots, atlas_coverage, nodes, 16, 16);

    const auto pattern = std::array<std::uint8_t, 35>{
        0,  40, 80, 120, 160, 200, 255, 210, 150, 90, 30, 80, 140, 200, 255, 220, 170, 120,
        70, 20, 60, 120, 180, 240, 255, 200, 140, 80, 20, 0,  90,  180, 250, 180, 90,
    };
    const auto key = veyra::GlyphMaskKey{
        .face_id = 1,
        .glyph_id = 5,
        .pixel_size = 12,
    };
    const auto mask = veyra::GlyphMask{
        .mask = {
            .width = 5,
            .height = 7,
            .stride = 5,
            .coverage = pattern,
            .generation = 1,
        },
    };
    require(
        atlas.insert(key, mask) == veyra::GlyphAtlasResult::inserted,
        "the test glyph inserts into the atlas"
    );
    const auto placement = atlas.find(key);
    if (!placement.has_value()) {
        throw std::runtime_error("the test glyph resolves");
    }
    const auto glyph = *placement;

    const auto commands = std::array<veyra::DisplayCommand, 1>{
        veyra::DrawGlyph{
            .atlas = &atlas,
            .u = glyph.u,
            .v = glyph.v,
            .width = glyph.width,
            .height = glyph.height,
            .x = 4.0F,
            .y = 3.0F,
            .color = {.red = 240, .green = 180, .blue = 40, .alpha = 255},
        },
    };

    auto expected = std::array<veyra::SoftwarePixel, static_cast<std::size_t>(width) * height>{};
    expected.fill({.alpha = 0});
    const veyra::SoftwareSurface surface{
        .width = width,
        .height = height,
        .stride = width,
        .pixels = expected,
    };
    require(
        veyra::render_software(commands, surface, veyra::SoftwareRenderWorkspace{}) ==
            veyra::SoftwareRenderResult::complete,
        "the software reference renders a glyph"
    );

    auto actual = std::array<veyra::SoftwarePixel, static_cast<std::size_t>(width) * height>{};
    require(
        backend.render(commands, width, height, actual) == veyra::WebGpuFrameResult::complete,
        "the WebGPU glyph scene completes"
    );
    check_parity(actual, expected, "glyph parity");
}

void check_image_parity() {
    auto backend = veyra::WebGpuBackend{};
    if (!backend.create()) {
        std::exit(77);
    }

    constexpr auto width = std::uint32_t{24};
    constexpr auto height = std::uint32_t{20};
    const auto pixels = std::array<veyra::SoftwarePixel, 16>{
        veyra::SoftwarePixel{.red = 240, .green = 20, .blue = 20, .alpha = 255},
        veyra::SoftwarePixel{.red = 20, .green = 240, .blue = 20, .alpha = 255},
        veyra::SoftwarePixel{.red = 20, .green = 20, .blue = 240, .alpha = 255},
        veyra::SoftwarePixel{.red = 240, .green = 240, .blue = 20, .alpha = 255},
        veyra::SoftwarePixel{.red = 20, .green = 240, .blue = 240, .alpha = 200},
        veyra::SoftwarePixel{.red = 240, .green = 20, .blue = 240, .alpha = 200},
        veyra::SoftwarePixel{.red = 128, .green = 128, .blue = 128, .alpha = 160},
        veyra::SoftwarePixel{.red = 255, .green = 255, .blue = 255, .alpha = 255},
        veyra::SoftwarePixel{.red = 10, .green = 20, .blue = 30, .alpha = 255},
        veyra::SoftwarePixel{.red = 40, .green = 50, .blue = 60, .alpha = 255},
        veyra::SoftwarePixel{.red = 70, .green = 80, .blue = 90, .alpha = 255},
        veyra::SoftwarePixel{.red = 100, .green = 110, .blue = 120, .alpha = 255},
        veyra::SoftwarePixel{.red = 130, .green = 140, .blue = 150, .alpha = 255},
        veyra::SoftwarePixel{.red = 160, .green = 170, .blue = 180, .alpha = 255},
        veyra::SoftwarePixel{.red = 190, .green = 200, .blue = 210, .alpha = 255},
        veyra::SoftwarePixel{.red = 220, .green = 230, .blue = 240, .alpha = 255},
    };
    const auto image = veyra::SoftwareImage{
        .width = 4,
        .height = 4,
        .stride = 4,
        .pixels = pixels,
        .generation = 1,
    };
    const auto commands = std::array<veyra::DisplayCommand, 2>{
        veyra::DrawImage{
            .source = {.width = 4.0F, .height = 4.0F},
            .destination = {.x = 2.0F, .y = 2.0F, .width = 8.0F, .height = 8.0F},
            .image = &image,
            .sampling = veyra::SoftwareImageSampling::nearest,
            .opacity = 255,
        },
        veyra::DrawImage{
            .source = {.width = 4.0F, .height = 4.0F},
            .destination = {.x = 14.0F, .y = 8.0F, .width = 4.0F, .height = 4.0F},
            .image = &image,
            .sampling = veyra::SoftwareImageSampling::linear,
            .opacity = 180,
        },
    };

    auto expected = std::array<veyra::SoftwarePixel, static_cast<std::size_t>(width) * height>{};
    expected.fill({.alpha = 0});
    const veyra::SoftwareSurface surface{
        .width = width,
        .height = height,
        .stride = width,
        .pixels = expected,
    };
    require(
        veyra::render_software(commands, surface, veyra::SoftwareRenderWorkspace{}) ==
            veyra::SoftwareRenderResult::complete,
        "the software reference renders images"
    );

    auto actual = std::array<veyra::SoftwarePixel, static_cast<std::size_t>(width) * height>{};
    require(
        backend.render(commands, width, height, actual) == veyra::WebGpuFrameResult::complete,
        "the WebGPU image scene completes"
    );
    check_parity(actual, expected, "image parity");
}

void check_unsupported_plans() {
    auto backend = veyra::WebGpuBackend{};
    if (!backend.create()) {
        std::exit(77);
    }

    constexpr auto width = std::uint32_t{8};
    constexpr auto height = std::uint32_t{8};
    auto output = std::array<veyra::SoftwarePixel, static_cast<std::size_t>(width) * height>{};

    const auto path = std::array<veyra::DisplayCommand, 1>{
        veyra::FillPath{
            .color = {.red = 1, .green = 2, .blue = 3, .alpha = 255},
        },
    };
    require(
        backend.render(path, width, height, output) == veyra::WebGpuFrameResult::unsupported,
        "a path is rejected by the current pipelines"
    );

    require(
        backend.render({}, width, height, output) == veyra::WebGpuFrameResult::unsupported,
        "an empty plan is rejected"
    );
}

}

int main() {
    try {
        check_full_surface_fill();
        check_multi_rect_parity();
        check_rounded_rect_parity();
        check_clip_parity();
        check_mask_parity();
        check_glyph_parity();
        check_image_parity();
        check_unsupported_plans();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';

        return 1;
    }

    std::cout << "WebGPU device checks passed\n";

    return 0;
}
