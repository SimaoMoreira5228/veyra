#ifndef VEYRA_RENDER_SHADOW_HPP
#define VEYRA_RENDER_SHADOW_HPP

#include "veyra/foundation/geometry.hpp"
#include "veyra/render/cpu/software_renderer.hpp"

#include <cstddef>
#include <cstdint>

namespace veyra {

struct BoxShadow {
    LayoutRect bounds{};
    float radius{};
    float blur{};
    float offset_x{};
    float offset_y{};
    DisplayColor color{};
};

enum class ShadowResult : std::uint8_t {
    complete,
    invalid_input,
    insufficient_output,
};

struct ShadowLowered {
    ShadowResult result{ShadowResult::complete};
    std::size_t commands{};
};

ShadowLowered lower_box_shadow(const BoxShadow& shadow, std::span<DisplayCommand> output) noexcept;

}

namespace veyra::render {
using ::veyra::BoxShadow;
using ::veyra::ShadowLowered;
using ::veyra::ShadowResult;
using ::veyra::lower_box_shadow;
}

namespace veyra::render::draw {
using ::veyra::BoxShadow;
using ::veyra::ShadowLowered;
using ::veyra::ShadowResult;
using ::veyra::lower_box_shadow;
}

#endif
