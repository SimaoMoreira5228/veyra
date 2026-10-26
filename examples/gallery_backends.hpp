#ifndef VEYRA_EXAMPLES_GALLERY_BACKENDS_HPP
#define VEYRA_EXAMPLES_GALLERY_BACKENDS_HPP

#include "veyra/render/software_renderer.hpp"

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

namespace gallery {

enum class Backend : std::uint8_t { cpu, sdlgpu };

bool parse_backend(std::string_view name, Backend& backend);

int render_pixels(
    const std::vector<veyra::DisplayCommand>& commands,
    std::vector<veyra::SoftwarePixel>& pixels,
    std::size_t width,
    std::size_t height,
    Backend backend
);

}

#endif
