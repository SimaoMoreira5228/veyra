#ifndef VEYRA_WIDGETS_BASIC_SLIDER_HPP
#define VEYRA_WIDGETS_BASIC_SLIDER_HPP

#include "veyra/foundation/geometry.hpp"
#include "veyra/render/cpu/software_renderer.hpp"
#include "veyra/style/theme/theme.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

namespace veyra {

struct SliderState {
    double fraction{};
    bool disabled{};
};

struct SliderAppearance {
    Srgba8 track;
    Srgba8 fill;
    Srgba8 thumb;
    Srgba8 disabled_track;
    Srgba8 disabled_fill;
};

SliderAppearance slider_appearance(const Theme& theme, SliderState state) noexcept;

struct SliderEmitState {
    double fraction_0{};
    double fraction_1{};
    bool has_second{};
    bool disabled{};
};

struct SliderGeometry {
    LayoutRect bar{};
    float thickness{};
};

enum class SliderEmitResult : std::uint8_t {
    complete,
    invalid_input,
    insufficient_output,
};

struct SliderOutput {
    std::size_t placed{};
    std::size_t required{};
    SliderEmitResult result{SliderEmitResult::complete};
};

std::size_t slider_required_commands(SliderEmitState state) noexcept;

SliderOutput emit_slider(
    const Theme& theme,
    SliderEmitState state,
    SliderGeometry geometry,
    std::span<DisplayCommand> commands_out
) noexcept;

}

namespace veyra::widgets {
using ::veyra::SliderAppearance;
using ::veyra::SliderEmitResult;
using ::veyra::SliderEmitState;
using ::veyra::SliderGeometry;
using ::veyra::SliderOutput;
using ::veyra::SliderState;
using ::veyra::emit_slider;
using ::veyra::slider_appearance;
using ::veyra::slider_required_commands;
}

namespace veyra::widgets::basic {
using ::veyra::SliderAppearance;
using ::veyra::SliderEmitResult;
using ::veyra::SliderEmitState;
using ::veyra::SliderGeometry;
using ::veyra::SliderOutput;
using ::veyra::SliderState;
using ::veyra::emit_slider;
using ::veyra::slider_appearance;
using ::veyra::slider_required_commands;
}

#endif
