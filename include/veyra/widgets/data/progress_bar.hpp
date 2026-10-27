#ifndef VEYRA_WIDGETS_DATA_PROGRESS_BAR_HPP
#define VEYRA_WIDGETS_DATA_PROGRESS_BAR_HPP

#include "veyra/behaviors/control/progress.hpp"
#include "veyra/layout/core/geometry.hpp"
#include "veyra/render/cpu/software_renderer.hpp"
#include "veyra/style/theme/theme.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

namespace veyra {

inline constexpr std::uint64_t kProgressBarIndeterminatePeriodMs = 1200;
inline constexpr float kProgressBarIndeterminateShare = 0.25F;

struct ProgressBarAppearance {
    Srgba8 track;
    Srgba8 fill;
    Srgba8 indeterminate_block;
};

ProgressBarAppearance progress_bar_appearance(const Theme& theme, ProgressState state) noexcept;

struct ProgressBarGeometry {
    LayoutRect bar{};
    std::uint64_t animation_tick_ms{};
};

enum class ProgressBarEmitResult : std::uint8_t {
    complete,
    invalid_input,
    insufficient_output,
};

struct ProgressBarOutput {
    std::size_t placed{};
    std::size_t required{};
    ProgressBarEmitResult result{ProgressBarEmitResult::complete};
};

std::size_t progress_bar_required_commands(ProgressState state) noexcept;

ProgressBarOutput emit_progress_bar(
    const Theme& theme,
    ProgressState state,
    ProgressBarGeometry geometry,
    std::span<DisplayCommand> commands_out
) noexcept;

}

#endif
