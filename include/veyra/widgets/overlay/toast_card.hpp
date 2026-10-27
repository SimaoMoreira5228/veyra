#ifndef VEYRA_WIDGETS_OVERLAY_TOAST_CARD_HPP
#define VEYRA_WIDGETS_OVERLAY_TOAST_CARD_HPP

#include "veyra/behaviors/overlay/toast.hpp"
#include "veyra/layout/core/geometry.hpp"
#include "veyra/render/cpu/software_renderer.hpp"
#include "veyra/style/theme.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

namespace veyra {

struct ToastCardState {
    ToastKind kind{ToastKind::info};
    bool assertive{};
};

struct ToastCardAppearance {
    Srgba8 background;
    Srgba8 border;
    Srgba8 accent;
    Srgba8 text;
};

ToastCardAppearance toast_card_appearance(const Theme& theme, ToastCardState state) noexcept;

struct ToastCardGeometry {
    LayoutRect card{};
};

enum class ToastCardEmitResult : std::uint8_t {
    complete,
    invalid_input,
    insufficient_output,
};

struct ToastCardOutput {
    std::size_t placed{};
    std::size_t required{};
    ToastCardEmitResult result{ToastCardEmitResult::complete};
};

constexpr std::size_t kToastCardRequiredCommands = 3;

ToastCardOutput emit_toast_card(
    const Theme& theme,
    ToastCardState state,
    const ToastCardGeometry& geometry,
    std::span<DisplayCommand> commands_out
) noexcept;

}

#endif
