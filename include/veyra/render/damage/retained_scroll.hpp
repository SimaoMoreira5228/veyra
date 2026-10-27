#ifndef VEYRA_RENDER_RETAINED_SCROLL_HPP
#define VEYRA_RENDER_RETAINED_SCROLL_HPP

#include "veyra/render/damage/scroll_shift.hpp"
#include "veyra/render/cpu/software_renderer.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

namespace veyra {

struct RetainedScrollSurface {
    SoftwareSurface pixels{};
    std::uint64_t painted_generation{};
};

struct RetainedScrollRequest {
    int delta_x{};
    int delta_y{};
    bool has_clip{};
    ScrollShiftRect clip{};
    std::uint64_t content_generation{};
};

struct RetainedScrollStats {
    bool reused{};
    std::size_t strip_count{};
};

enum class RetainedScrollResult : std::uint8_t {
    complete,
    invalid_input,
    invalid_surface,
    insufficient_workspace,
};

enum class RetainedVerifyResult : std::uint8_t {
    complete,
    mismatch,
    invalid_input,
    invalid_surface,
    insufficient_workspace,
};

struct RetainedScrollVerifyStats {
    bool verified{};
    std::size_t first_different{};
};

RetainedScrollResult render_retained_scrolled(
    std::span<const DisplayCommand> commands,
    SoftwareSurface surface,
    SoftwareRenderWorkspace workspace,
    const RetainedScrollRequest& request,
    RetainedScrollSurface& retained,
    RetainedScrollStats& stats
) noexcept;

RetainedVerifyResult render_retained_scrolled_verified(
    std::span<const DisplayCommand> commands,
    SoftwareSurface surface,
    SoftwareRenderWorkspace workspace,
    const RetainedScrollRequest& request,
    RetainedScrollSurface& retained,
    SoftwareSurface scratch,
    RetainedScrollStats& stats,
    RetainedScrollVerifyStats& verify_stats
) noexcept;

}

#endif
