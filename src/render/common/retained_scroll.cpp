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
        .x = static_cast<float>(strip.left),
        .y = static_cast<float>(strip.top),
        .width = static_cast<float>(strip.width),
        .height = static_cast<float>(strip.height),
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

bool same_geometry(const SoftwareSurface& left, const SoftwareSurface& right) noexcept {
    return left.width == right.width && left.height == right.height &&
        left.stride == right.stride && left.pixels.size() == right.pixels.size();
}

bool same_pixel(const SoftwarePixel& left, const SoftwarePixel& right) noexcept {
    return left.red == right.red && left.green == right.green && left.blue == right.blue &&
        left.alpha == right.alpha;
}

std::size_t first_live_pixel_difference(
    const SoftwareSurface& surface,
    const SoftwareSurface& scratch
) noexcept {
    for (auto row = std::size_t{}; row < surface.height; ++row) {
        const auto base = row * surface.stride;

        for (auto column = std::size_t{}; column < surface.width; ++column) {
            if (!same_pixel(surface.pixels[base + column], scratch.pixels[base + column])) {
                return base + column;
            }
        }
    }

    return surface.pixels.size();
}

void copy_live_pixels(const SoftwareSurface& scratch, SoftwareSurface surface) noexcept {
    for (auto row = std::size_t{}; row < surface.height; ++row) {
        const auto base = row * surface.stride;

        for (auto column = std::size_t{}; column < surface.width; ++column) {
            surface.pixels[base + column] = scratch.pixels[base + column];
        }
    }
}

RetainedVerifyResult map_verify_retained_result(RetainedScrollResult result) noexcept {
    switch (result) {
    case RetainedScrollResult::complete:
        return RetainedVerifyResult::complete;
    case RetainedScrollResult::invalid_input:
        return RetainedVerifyResult::invalid_input;
    case RetainedScrollResult::invalid_surface:
        return RetainedVerifyResult::invalid_surface;
    case RetainedScrollResult::insufficient_workspace:
        return RetainedVerifyResult::insufficient_workspace;
    }

    return RetainedVerifyResult::invalid_input;
}

RetainedVerifyResult map_verify_render_result(SoftwareRenderResult result) noexcept {
    switch (result) {
    case SoftwareRenderResult::complete:
        return RetainedVerifyResult::complete;
    case SoftwareRenderResult::invalid_input:
        return RetainedVerifyResult::invalid_input;
    case SoftwareRenderResult::invalid_surface:
        return RetainedVerifyResult::invalid_surface;
    case SoftwareRenderResult::insufficient_workspace:
        return RetainedVerifyResult::insufficient_workspace;
    }

    return RetainedVerifyResult::invalid_input;
}

}

RetainedVerifyResult render_retained_scrolled_verified(
    std::span<const DisplayCommand> commands,
    SoftwareSurface surface,
    SoftwareRenderWorkspace workspace,
    const RetainedScrollRequest& request,
    RetainedScrollSurface& retained,
    SoftwareSurface scratch,
    RetainedScrollStats& stats,
    RetainedScrollVerifyStats& verify_stats
) noexcept {
    stats = RetainedScrollStats{};
    verify_stats = RetainedScrollVerifyStats{};

    if (surface.width == 0 || surface.height == 0) {
        return RetainedVerifyResult::invalid_surface;
    }

    if (!same_geometry(surface, scratch)) {
        return RetainedVerifyResult::invalid_input;
    }

    const auto retained_result =
        render_retained_scrolled(commands, surface, workspace, request, retained, stats);

    if (retained_result != RetainedScrollResult::complete) {
        return map_verify_retained_result(retained_result);
    }

    const auto oracle_result = render_software(commands, scratch, workspace);

    if (oracle_result != SoftwareRenderResult::complete) {
        stats = RetainedScrollStats{};
        verify_stats = RetainedScrollVerifyStats{};

        return map_verify_render_result(oracle_result);
    }

    const auto first_different = first_live_pixel_difference(surface, scratch);
    verify_stats.verified = true;
    verify_stats.first_different = first_different;

    if (first_different == surface.pixels.size()) {
        return RetainedVerifyResult::complete;
    }

    copy_live_pixels(scratch, surface);
    retained.painted_generation = request.content_generation;

    return RetainedVerifyResult::mismatch;
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
