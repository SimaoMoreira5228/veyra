#ifndef VEYRA_RENDER_RETAINED_SCROLL_HPP
#define VEYRA_RENDER_RETAINED_SCROLL_HPP

#include "veyra/render/scroll_shift.hpp"
#include "veyra/render/software_renderer.hpp"

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

RetainedScrollResult render_retained_scrolled(
    std::span<const DisplayCommand> commands,
    SoftwareSurface surface,
    SoftwareRenderWorkspace workspace,
    const RetainedScrollRequest& request,
    RetainedScrollSurface& retained,
    RetainedScrollStats& stats
) noexcept;

}

#endif
