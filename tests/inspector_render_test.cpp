#include "inspector_render.hpp"

#include <array>
#include <cstddef>
#include <iostream>
#include <stdexcept>
#include <string_view>

namespace {

void require(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

}

int main() {
    try {
        constexpr auto width = std::size_t{240};
        constexpr auto height = std::size_t{160};
        auto pixels = std::array<veyra::SoftwarePixel, width * height>{};
        const auto result = veyra::render_inspector(
            veyra::Theme::dark(),
            {.selected = 2, .focused = true},
            pixels,
            width,
            height
        );
        require(result == veyra::SoftwareRenderResult::complete, "inspector renders");
        require(pixels[0].blue != 0, "inspector has a header surface");
        require(pixels[100].blue != 0, "inspector body has content");

        auto distinct = std::size_t{};
        const auto first = pixels[0];
        for (const auto& pixel : pixels) {
            if (pixel.red != first.red || pixel.green != first.green || pixel.blue != first.blue) {
                ++distinct;
            }
        }
        require(distinct > width, "inspector is more than a flat fill");
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }

    return 0;
}
