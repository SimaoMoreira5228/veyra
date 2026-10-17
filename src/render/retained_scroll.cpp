#include "veyra/render/retained_scroll.hpp"

#include <array>

namespace veyra {
namespace {

bool same_retained_storage(
    const SoftwareSurface& surface,
    const SoftwareSurface& retained
) noexcept {
    return surface.width == retained.width && surface.height == retained.height &&
        surface.stride == retained.stride && surface.pixels.data() == retained.pixels.data() &&
        surface.pixels.size() == retained.pixels.size();
}

LayoutRect strip_damage(const ScrollShiftRect& strip) noexcept {
    return LayoutRect{
        .x = static_cast<double>(strip.left),
        .y = static_cast<double>(strip.top),
        .width = static_cast<double>(strip.width),
        .height = static_cast<double>(strip.height),
    };
}

RetainedScrollResult map_render_result(SoftwareRenderResult result) noexcept {
    switch (result) {
    case SoftwareRenderResult::complete:
        return RetainedScrollResult::complete;
    case SoftwareRenderResult::invalid_input:
        return RetainedScrollResult::invalid_input;
    case SoftwareRenderResult::invalid_surface:
        return RetainedScrollResult::invalid_surface;
    case SoftwareRenderResult::insufficient_workspace:
        return RetainedScrollResult::insufficient_workspace;
    }

    return RetainedScrollResult::invalid_input;
}

RetainedScrollResult paint_full(
    std::span<const DisplayCommand> commands,
    SoftwareSurface surface,
    SoftwareRenderWorkspace workspace,
    const RetainedScrollRequest& request,
    RetainedScrollSurface& retained,
    RetainedScrollStats& stats
) noexcept {
    const auto result = render_software(commands, surface, workspace);

    if (result != SoftwareRenderResult::complete) {
        stats = RetainedScrollStats{};

        return map_render_result(result);
    }

    retained.pixels = surface;
    retained.painted_generation = request.content_generation;
    stats = RetainedScrollStats{};

    return RetainedScrollResult::complete;
}

}

RetainedScrollResult render_retained_scrolled(
    std::span<const DisplayCommand> commands,
    SoftwareSurface surface,
    SoftwareRenderWorkspace workspace,
    const RetainedScrollRequest& request,
    RetainedScrollSurface& retained,
    RetainedScrollStats& stats
) noexcept {
    stats = RetainedScrollStats{};

    if (surface.width == 0 || surface.height == 0) {
        return RetainedScrollResult::invalid_surface;
    }

    if (!same_retained_storage(surface, retained.pixels)) {
        return paint_full(commands, surface, workspace, request, retained, stats);
    }

    auto plan = ScrollShiftPlan{};
    const auto planned = resolve_scroll_shift(
        {
            .width = surface.width,
            .height = surface.height,
            .delta_x = request.delta_x,
            .delta_y = request.delta_y,
            .has_clip = request.has_clip,
            .clip = request.clip,
            .current_generation = request.content_generation,
            .retained_generation = retained.painted_generation,
        },
        plan
    );

    if (planned == ScrollShiftResult::stale_generation) {
        return paint_full(commands, surface, workspace, request, retained, stats);
    }

    if (planned == ScrollShiftResult::no_movement) {
        return RetainedScrollResult::complete;
    }

    if (planned != ScrollShiftResult::complete || plan.strip_count == 0 || plan.strip_count > 2) {
        return RetainedScrollResult::invalid_input;
    }

    if (plan.has_blit) {
        const auto moved = shift_surface_pixels(
            surface.pixels,
            surface.stride,
            surface.width,
            surface.height,
            plan.source,
            plan.destination
        );

        if (!moved) {
            return paint_full(commands, surface, workspace, request, retained, stats);
        }
    }

    auto damage = std::array<LayoutRect, 2>{};

    for (auto index = std::size_t{}; index < plan.strip_count; ++index) {
        damage[index] = strip_damage(plan.strips[index]);
    }

    const auto painted = render_software(
        commands,
        surface,
        workspace,
        std::span<const LayoutRect>{damage.data(), plan.strip_count}
    );

    if (painted != SoftwareRenderResult::complete) {
        return map_render_result(painted);
    }

    retained.painted_generation = request.content_generation;
    stats.reused = plan.has_blit;
    stats.strip_count = plan.strip_count;

    return RetainedScrollResult::complete;
}

}
