#ifndef VEYRA_WIDGETS_BASIC_TEXT_AREA_HPP
#define VEYRA_WIDGETS_BASIC_TEXT_AREA_HPP

#include "veyra/layout/core/geometry.hpp"
#include "veyra/render/cpu/software_renderer.hpp"
#include "veyra/style/theme.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

namespace veyra {

struct TextAreaState {
    bool focused{};
    bool disabled{};
    bool has_selection{};
};

struct TextAreaAppearance {
    Srgba8 box_fill;
    Srgba8 border;
    Srgba8 caret;
    Srgba8 placeholder;
    Srgba8 selection;
    LogicalPx corner_radius;
    LogicalPx horizontal_padding;
    LogicalPx vertical_padding;
};

TextAreaAppearance text_area_appearance(const Theme& theme, TextAreaState state) noexcept;

struct TextAreaGeometry {
    LayoutRect bounds{};
    std::span<const LayoutRect> selection_rects{};
    LayoutRect caret{};
    bool show_caret{};
    LayoutRect composition_underline{};
    bool has_composition{};
    bool clip_to_viewport{};
};

enum class TextAreaEmitResult : std::uint8_t {
    complete,
    invalid_input,
    insufficient_output,
};

struct TextAreaOutput {
    std::size_t placed{};
    std::size_t required{};
    TextAreaEmitResult result{TextAreaEmitResult::complete};
};

std::size_t text_area_required_commands(const TextAreaGeometry& geometry) noexcept;

TextAreaOutput emit_text_area(
    const Theme& theme,
    TextAreaState state,
    const TextAreaGeometry& geometry,
    std::span<DisplayCommand> commands_out
) noexcept;

}

#endif
