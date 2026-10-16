#ifndef VEYRA_SRC_RENDER_VULKAN_FRAME_EMIT_HPP
#define VEYRA_SRC_RENDER_VULKAN_FRAME_EMIT_HPP

#include "veyra/render/software_renderer.hpp"
#include "veyra/render/vulkan_rect_pipeline.hpp"
#include "vulkan_frame_batch.hpp"
#include "vulkan_internal.hpp"

namespace veyra {

constexpr float kFrameChannelScale = 1.0F / 255.0F;

inline bool frame_clip_excludes(SoftwareClipRect clip, SoftwareClipRect bounds) noexcept {
    return bounds.left >= clip.right || bounds.right <= clip.left || bounds.top >= clip.bottom ||
        bounds.bottom <= clip.top;
}

struct ShapeStyle {
    DrawPipe pipe{DrawPipe::rect};
    VkDescriptorSet set{};
    LayoutRect shape{};
    DisplayColor color{};
    LayoutRect source{};
    const SoftwareImage* image{};
    const SoftwareMask* mask{};
    SoftwareImageSampling sampling{SoftwareImageSampling::nearest};
    float radius{};
    float opacity{1.0F};
    float thickness{};
    LayoutPoint glyph_uv{};
    bool skip{false};
};

struct FrameEmitter {
    FrameBatch& batch;
    vulkan_detail::ContextState& state;
    vulkan_detail::PipelineState& stored;

    FrameEmitter(
        FrameBatch& batch,
        vulkan_detail::ContextState& state,
        vulkan_detail::PipelineState& stored
    ) noexcept;

    VulkanRenderResult resolve_image(const DrawImage& drawn, ShapeStyle& style) noexcept;

    VulkanRenderResult resolve_mask(const DrawMask& masked, ShapeStyle& style) noexcept;

    VulkanRenderResult resolve_glyph(const DrawGlyph& glyphed, ShapeStyle& style) noexcept;

    VulkanRenderResult
    resolve_rounded_stroke(const StrokeRoundedRect& rounded_stroked, ShapeStyle& style) noexcept;

    VulkanRenderResult resolve_fill(
        const FillRect* fill,
        const FillRoundedRect* rounded_fill,
        ShapeStyle& style
    ) noexcept;

    VulkanRenderResult emit_shape(const ShapeStyle& style, const LayoutPoint& translation) noexcept;

    VulkanRenderResult
    emit_stroke_rect(const StrokeRect& stroked, const LayoutPoint& translation) noexcept;

    VulkanRenderResult emit_line(const StrokeLine& lined, const LayoutPoint& translation) noexcept;

    VulkanRenderResult
    emit_polyline(const StrokePolyline& polylined, const LayoutPoint& translation) noexcept;

    VulkanRenderResult emit_path(const FillPath& pathed, const LayoutPoint& translation) noexcept;
};

}

#endif
