#include "veyra/render/gpu/webgpu/webgpu_backend.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
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

void check_unsupported_plans() {
    auto backend = veyra::WebGpuBackend{};
    if (!backend.create()) {
        std::exit(77);
    }

    constexpr auto width = std::uint32_t{8};
    constexpr auto height = std::uint32_t{8};
    auto output = std::array<veyra::SoftwarePixel, static_cast<std::size_t>(width) * height>{};

    const auto clipped = std::array<veyra::DisplayCommand, 3>{
        veyra::PushClip{
            .bounds = {.width = 4.0F, .height = 4.0F},
            .clip_id = 1,
        },
        veyra::FillRect{
            .bounds = {.width = 4.0F, .height = 4.0F},
            .color = {.red = 1, .green = 2, .blue = 3, .alpha = 255},
        },
        veyra::PopClip{},
    };
    require(
        backend.render(clipped, width, height, output) == veyra::WebGpuFrameResult::unsupported,
        "a clipped plan is rejected by the rect pass"
    );

    const auto rounded = std::array<veyra::DisplayCommand, 1>{
        veyra::FillRoundedRect{
            .bounds = {.width = 8.0F, .height = 8.0F},
            .radius = 2.0F,
            .color = {.red = 1, .green = 2, .blue = 3, .alpha = 255},
        },
    };
    require(
        backend.render(rounded, width, height, output) == veyra::WebGpuFrameResult::unsupported,
        "a rounded rect is rejected by the rect pass"
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
        check_unsupported_plans();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';

        return 1;
    }

    std::cout << "WebGPU device checks passed\n";

    return 0;
}
