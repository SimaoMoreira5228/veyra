#include "veyra/render/vulkan_rect_pipeline.hpp"

#include "vulkan_internal.hpp"

#include <vulkan/vulkan.h>

#include <cstddef>
#include <new>

namespace veyra {
namespace {}

VulkanRenderResult
create_vulkan_rect_pipeline(VulkanContext& context, VulkanRectPipeline& pipeline) noexcept {
    auto* state = vulkan_detail::state_of(context);

    if (state == nullptr || !context.ready) {
        return VulkanRenderResult::invalid_input;
    }

    if (pipeline.ready) {
        return VulkanRenderResult::invalid_input;
    }

    auto* stored = new (std::nothrow) vulkan_detail::PipelineState{};

    if (stored == nullptr) {
        return VulkanRenderResult::device_error;
    }

    if (state->shared_pass == VK_NULL_HANDLE) {
        delete stored;

        return VulkanRenderResult::device_error;
    }

    stored->pass = state->shared_pass;

    if (!vulkan_detail::create_rect_pipeline(
            state->device,
            state->shared_pass,
            stored->layout,
            stored->pipeline
        )) {
        delete stored;

        return VulkanRenderResult::device_error;
    }

    if (!vulkan_detail::create_rounded_pipeline(
            state->device,
            state->shared_pass,
            stored->rounded_layout,
            stored->rounded_pipeline
        )) {
        vkDestroyPipeline(state->device, stored->pipeline, nullptr);
        vkDestroyPipelineLayout(state->device, stored->layout, nullptr);
        delete stored;

        return VulkanRenderResult::device_error;
    }

    constexpr auto rect_bytes =
        kVulkanMaxRects * kVulkanMaxRectDraws * sizeof(vulkan_detail::RectInstance);

    if (!vulkan_detail::create_instance_buffer(
            *state,
            stored->instances,
            stored->instance_memory,
            stored->instance_mapped,
            rect_bytes
        )) {
        vkDestroyPipeline(state->device, stored->rounded_pipeline, nullptr);
        vkDestroyPipelineLayout(state->device, stored->rounded_layout, nullptr);
        vkDestroyPipeline(state->device, stored->pipeline, nullptr);
        vkDestroyPipelineLayout(state->device, stored->layout, nullptr);
        delete stored;

        return VulkanRenderResult::device_error;
    }

    constexpr auto rounded_bytes =
        kVulkanMaxRects * kVulkanMaxRectDraws * sizeof(vulkan_detail::RoundedRectInstance);

    if (!vulkan_detail::create_instance_buffer(
            *state,
            stored->rounded_instances,
            stored->rounded_instance_memory,
            stored->rounded_instance_mapped,
            rounded_bytes
        )) {
        vulkan_detail::destroy_instance_buffer(
            *state,
            stored->instances,
            stored->instance_memory,
            stored->instance_mapped
        );
        vkDestroyPipeline(state->device, stored->rounded_pipeline, nullptr);
        vkDestroyPipelineLayout(state->device, stored->rounded_layout, nullptr);
        vkDestroyPipeline(state->device, stored->pipeline, nullptr);
        vkDestroyPipelineLayout(state->device, stored->layout, nullptr);
        delete stored;

        return VulkanRenderResult::device_error;
    }

    if (!vulkan_detail::create_texture_cache(*state, *stored)) {
        vulkan_detail::destroy_instance_buffer(
            *state,
            stored->rounded_instances,
            stored->rounded_instance_memory,
            stored->rounded_instance_mapped
        );
        vulkan_detail::destroy_instance_buffer(
            *state,
            stored->instances,
            stored->instance_memory,
            stored->instance_mapped
        );
        vkDestroyPipeline(state->device, stored->rounded_pipeline, nullptr);
        vkDestroyPipelineLayout(state->device, stored->rounded_layout, nullptr);
        vkDestroyPipeline(state->device, stored->pipeline, nullptr);
        vkDestroyPipelineLayout(state->device, stored->layout, nullptr);
        delete stored;

        return VulkanRenderResult::device_error;
    }

    if (!vulkan_detail::create_image_pipeline(
            state->device,
            state->shared_pass,
            stored->descriptor_layout,
            stored->image_layout,
            stored->image_pipeline
        )) {
        vulkan_detail::destroy_texture_cache(*state, *stored);
        vulkan_detail::destroy_instance_buffer(
            *state,
            stored->rounded_instances,
            stored->rounded_instance_memory,
            stored->rounded_instance_mapped
        );
        vulkan_detail::destroy_instance_buffer(
            *state,
            stored->instances,
            stored->instance_memory,
            stored->instance_mapped
        );
        vkDestroyPipeline(state->device, stored->rounded_pipeline, nullptr);
        vkDestroyPipelineLayout(state->device, stored->rounded_layout, nullptr);
        vkDestroyPipeline(state->device, stored->pipeline, nullptr);
        vkDestroyPipelineLayout(state->device, stored->layout, nullptr);
        delete stored;

        return VulkanRenderResult::device_error;
    }

    constexpr auto image_bytes =
        kVulkanMaxRects * kVulkanMaxRectDraws * sizeof(vulkan_detail::ImageInstance);

    if (!vulkan_detail::create_instance_buffer(
            *state,
            stored->image_instances,
            stored->image_instance_memory,
            stored->image_instance_mapped,
            image_bytes
        )) {
        vkDestroyPipeline(state->device, stored->image_pipeline, nullptr);
        vkDestroyPipelineLayout(state->device, stored->image_layout, nullptr);
        vulkan_detail::destroy_texture_cache(*state, *stored);
        vulkan_detail::destroy_instance_buffer(
            *state,
            stored->rounded_instances,
            stored->rounded_instance_memory,
            stored->rounded_instance_mapped
        );
        vulkan_detail::destroy_instance_buffer(
            *state,
            stored->instances,
            stored->instance_memory,
            stored->instance_mapped
        );
        vkDestroyPipeline(state->device, stored->rounded_pipeline, nullptr);
        vkDestroyPipelineLayout(state->device, stored->rounded_layout, nullptr);
        vkDestroyPipeline(state->device, stored->pipeline, nullptr);
        vkDestroyPipelineLayout(state->device, stored->layout, nullptr);
        delete stored;

        return VulkanRenderResult::device_error;
    }

    if (!vulkan_detail::create_mask_pipeline(
            state->device,
            state->shared_pass,
            stored->descriptor_layout,
            stored->mask_layout,
            stored->mask_pipeline
        )) {
        vulkan_detail::destroy_instance_buffer(
            *state,
            stored->image_instances,
            stored->image_instance_memory,
            stored->image_instance_mapped
        );
        vkDestroyPipeline(state->device, stored->image_pipeline, nullptr);
        vkDestroyPipelineLayout(state->device, stored->image_layout, nullptr);
        vulkan_detail::destroy_texture_cache(*state, *stored);
        vulkan_detail::destroy_instance_buffer(
            *state,
            stored->rounded_instances,
            stored->rounded_instance_memory,
            stored->rounded_instance_mapped
        );
        vulkan_detail::destroy_instance_buffer(
            *state,
            stored->instances,
            stored->instance_memory,
            stored->instance_mapped
        );
        vkDestroyPipeline(state->device, stored->rounded_pipeline, nullptr);
        vkDestroyPipelineLayout(state->device, stored->rounded_layout, nullptr);
        vkDestroyPipeline(state->device, stored->pipeline, nullptr);
        vkDestroyPipelineLayout(state->device, stored->layout, nullptr);
        delete stored;

        return VulkanRenderResult::device_error;
    }

    constexpr auto mask_bytes =
        kVulkanMaxRects * kVulkanMaxRectDraws * sizeof(vulkan_detail::MaskInstance);

    if (!vulkan_detail::create_instance_buffer(
            *state,
            stored->mask_instances,
            stored->mask_instance_memory,
            stored->mask_instance_mapped,
            mask_bytes
        )) {
        vkDestroyPipeline(state->device, stored->mask_pipeline, nullptr);
        vkDestroyPipelineLayout(state->device, stored->mask_layout, nullptr);
        vulkan_detail::destroy_instance_buffer(
            *state,
            stored->image_instances,
            stored->image_instance_memory,
            stored->image_instance_mapped
        );
        vkDestroyPipeline(state->device, stored->image_pipeline, nullptr);
        vkDestroyPipelineLayout(state->device, stored->image_layout, nullptr);
        vulkan_detail::destroy_texture_cache(*state, *stored);
        vulkan_detail::destroy_instance_buffer(
            *state,
            stored->rounded_instances,
            stored->rounded_instance_memory,
            stored->rounded_instance_mapped
        );
        vulkan_detail::destroy_instance_buffer(
            *state,
            stored->instances,
            stored->instance_memory,
            stored->instance_mapped
        );
        vkDestroyPipeline(state->device, stored->rounded_pipeline, nullptr);
        vkDestroyPipelineLayout(state->device, stored->rounded_layout, nullptr);
        vkDestroyPipeline(state->device, stored->pipeline, nullptr);
        vkDestroyPipelineLayout(state->device, stored->layout, nullptr);
        delete stored;

        return VulkanRenderResult::device_error;
    }

    if (!vulkan_detail::create_stroke_pipeline(
            state->device,
            state->shared_pass,
            stored->stroke_layout,
            stored->stroke_pipeline
        )) {
        vulkan_detail::destroy_instance_buffer(
            *state,
            stored->mask_instances,
            stored->mask_instance_memory,
            stored->mask_instance_mapped
        );
        vkDestroyPipeline(state->device, stored->mask_pipeline, nullptr);
        vkDestroyPipelineLayout(state->device, stored->mask_layout, nullptr);
        vulkan_detail::destroy_instance_buffer(
            *state,
            stored->image_instances,
            stored->image_instance_memory,
            stored->image_instance_mapped
        );
        vkDestroyPipeline(state->device, stored->image_pipeline, nullptr);
        vkDestroyPipelineLayout(state->device, stored->image_layout, nullptr);
        vulkan_detail::destroy_texture_cache(*state, *stored);
        vulkan_detail::destroy_instance_buffer(
            *state,
            stored->rounded_instances,
            stored->rounded_instance_memory,
            stored->rounded_instance_mapped
        );
        vulkan_detail::destroy_instance_buffer(
            *state,
            stored->instances,
            stored->instance_memory,
            stored->instance_mapped
        );
        vkDestroyPipeline(state->device, stored->rounded_pipeline, nullptr);
        vkDestroyPipelineLayout(state->device, stored->rounded_layout, nullptr);
        vkDestroyPipeline(state->device, stored->pipeline, nullptr);
        vkDestroyPipelineLayout(state->device, stored->layout, nullptr);
        delete stored;

        return VulkanRenderResult::device_error;
    }

    constexpr auto stroke_bytes =
        kVulkanMaxRects * kVulkanMaxRectDraws * sizeof(vulkan_detail::StrokeInstance);

    if (!vulkan_detail::create_instance_buffer(
            *state,
            stored->stroke_instances,
            stored->stroke_instance_memory,
            stored->stroke_instance_mapped,
            stroke_bytes
        )) {
        vkDestroyPipeline(state->device, stored->stroke_pipeline, nullptr);
        vkDestroyPipelineLayout(state->device, stored->stroke_layout, nullptr);
        vulkan_detail::destroy_instance_buffer(
            *state,
            stored->mask_instances,
            stored->mask_instance_memory,
            stored->mask_instance_mapped
        );
        vkDestroyPipeline(state->device, stored->mask_pipeline, nullptr);
        vkDestroyPipelineLayout(state->device, stored->mask_layout, nullptr);
        vulkan_detail::destroy_instance_buffer(
            *state,
            stored->image_instances,
            stored->image_instance_memory,
            stored->image_instance_mapped
        );
        vkDestroyPipeline(state->device, stored->image_pipeline, nullptr);
        vkDestroyPipelineLayout(state->device, stored->image_layout, nullptr);
        vulkan_detail::destroy_texture_cache(*state, *stored);
        vulkan_detail::destroy_instance_buffer(
            *state,
            stored->rounded_instances,
            stored->rounded_instance_memory,
            stored->rounded_instance_mapped
        );
        vulkan_detail::destroy_instance_buffer(
            *state,
            stored->instances,
            stored->instance_memory,
            stored->instance_mapped
        );
        vkDestroyPipeline(state->device, stored->rounded_pipeline, nullptr);
        vkDestroyPipelineLayout(state->device, stored->rounded_layout, nullptr);
        vkDestroyPipeline(state->device, stored->pipeline, nullptr);
        vkDestroyPipelineLayout(state->device, stored->layout, nullptr);
        delete stored;

        return VulkanRenderResult::device_error;
    }

    if (!vulkan_detail::create_rounded_stroke_pipeline(
            state->device,
            state->shared_pass,
            stored->rounded_stroke_layout,
            stored->rounded_stroke_pipeline
        )) {
        vulkan_detail::destroy_instance_buffer(
            *state,
            stored->stroke_instances,
            stored->stroke_instance_memory,
            stored->stroke_instance_mapped
        );
        vkDestroyPipeline(state->device, stored->stroke_pipeline, nullptr);
        vkDestroyPipelineLayout(state->device, stored->stroke_layout, nullptr);
        vulkan_detail::destroy_instance_buffer(
            *state,
            stored->mask_instances,
            stored->mask_instance_memory,
            stored->mask_instance_mapped
        );
        vkDestroyPipeline(state->device, stored->mask_pipeline, nullptr);
        vkDestroyPipelineLayout(state->device, stored->mask_layout, nullptr);
        vulkan_detail::destroy_instance_buffer(
            *state,
            stored->image_instances,
            stored->image_instance_memory,
            stored->image_instance_mapped
        );
        vkDestroyPipeline(state->device, stored->image_pipeline, nullptr);
        vkDestroyPipelineLayout(state->device, stored->image_layout, nullptr);
        vulkan_detail::destroy_texture_cache(*state, *stored);
        vulkan_detail::destroy_instance_buffer(
            *state,
            stored->rounded_instances,
            stored->rounded_instance_memory,
            stored->rounded_instance_mapped
        );
        vulkan_detail::destroy_instance_buffer(
            *state,
            stored->instances,
            stored->instance_memory,
            stored->instance_mapped
        );
        vkDestroyPipeline(state->device, stored->rounded_pipeline, nullptr);
        vkDestroyPipelineLayout(state->device, stored->rounded_layout, nullptr);
        vkDestroyPipeline(state->device, stored->pipeline, nullptr);
        vkDestroyPipelineLayout(state->device, stored->layout, nullptr);
        delete stored;

        return VulkanRenderResult::device_error;
    }

    constexpr auto rounded_stroke_bytes =
        kVulkanMaxRects * kVulkanMaxRectDraws * sizeof(vulkan_detail::RoundedStrokeInstance);

    if (!vulkan_detail::create_instance_buffer(
            *state,
            stored->rounded_stroke_instances,
            stored->rounded_stroke_instance_memory,
            stored->rounded_stroke_instance_mapped,
            rounded_stroke_bytes
        )) {
        vkDestroyPipeline(state->device, stored->rounded_stroke_pipeline, nullptr);
        vkDestroyPipelineLayout(state->device, stored->rounded_stroke_layout, nullptr);
        vulkan_detail::destroy_instance_buffer(
            *state,
            stored->stroke_instances,
            stored->stroke_instance_memory,
            stored->stroke_instance_mapped
        );
        vkDestroyPipeline(state->device, stored->stroke_pipeline, nullptr);
        vkDestroyPipelineLayout(state->device, stored->stroke_layout, nullptr);
        vulkan_detail::destroy_instance_buffer(
            *state,
            stored->mask_instances,
            stored->mask_instance_memory,
            stored->mask_instance_mapped
        );
        vkDestroyPipeline(state->device, stored->mask_pipeline, nullptr);
        vkDestroyPipelineLayout(state->device, stored->mask_layout, nullptr);
        vulkan_detail::destroy_instance_buffer(
            *state,
            stored->image_instances,
            stored->image_instance_memory,
            stored->image_instance_mapped
        );
        vkDestroyPipeline(state->device, stored->image_pipeline, nullptr);
        vkDestroyPipelineLayout(state->device, stored->image_layout, nullptr);
        vulkan_detail::destroy_texture_cache(*state, *stored);
        vulkan_detail::destroy_instance_buffer(
            *state,
            stored->rounded_instances,
            stored->rounded_instance_memory,
            stored->rounded_instance_mapped
        );
        vulkan_detail::destroy_instance_buffer(
            *state,
            stored->instances,
            stored->instance_memory,
            stored->instance_mapped
        );
        vkDestroyPipeline(state->device, stored->rounded_pipeline, nullptr);
        vkDestroyPipelineLayout(state->device, stored->rounded_layout, nullptr);
        vkDestroyPipeline(state->device, stored->pipeline, nullptr);
        vkDestroyPipelineLayout(state->device, stored->layout, nullptr);
        delete stored;

        return VulkanRenderResult::device_error;
    }

    pipeline.pipeline_state = stored;
    pipeline.ready = true;

    return VulkanRenderResult::complete;
}

VulkanRenderResult
create_vulkan_unified_pipeline(VulkanContext& context, VulkanRectPipeline& pipeline) noexcept {
    const auto result = create_vulkan_rect_pipeline(context, pipeline);

    if (result != VulkanRenderResult::complete) {
        return result;
    }

    vulkan_detail::pipeline_of(pipeline)->unified = true;

    return VulkanRenderResult::complete;
}

void destroy_vulkan_rect_pipeline(VulkanContext& context, VulkanRectPipeline& pipeline) noexcept {
    auto* stored = vulkan_detail::pipeline_of(pipeline);

    if (stored == nullptr) {
        return;
    }

    auto* state = vulkan_detail::state_of(context);

    if (state != nullptr && state->device != VK_NULL_HANDLE) {
        vulkan_detail::destroy_instance_buffer(
            *state,
            stored->rounded_stroke_instances,
            stored->rounded_stroke_instance_memory,
            stored->rounded_stroke_instance_mapped
        );

        if (stored->rounded_stroke_pipeline != VK_NULL_HANDLE) {
            vkDestroyPipeline(state->device, stored->rounded_stroke_pipeline, nullptr);

            stored->rounded_stroke_pipeline = VK_NULL_HANDLE;
        }

        if (stored->rounded_stroke_layout != VK_NULL_HANDLE) {
            vkDestroyPipelineLayout(state->device, stored->rounded_stroke_layout, nullptr);

            stored->rounded_stroke_layout = VK_NULL_HANDLE;
        }

        vulkan_detail::destroy_instance_buffer(
            *state,
            stored->stroke_instances,
            stored->stroke_instance_memory,
            stored->stroke_instance_mapped
        );

        if (stored->stroke_pipeline != VK_NULL_HANDLE) {
            vkDestroyPipeline(state->device, stored->stroke_pipeline, nullptr);

            stored->stroke_pipeline = VK_NULL_HANDLE;
        }

        if (stored->stroke_layout != VK_NULL_HANDLE) {
            vkDestroyPipelineLayout(state->device, stored->stroke_layout, nullptr);

            stored->stroke_layout = VK_NULL_HANDLE;
        }

        vulkan_detail::destroy_instance_buffer(
            *state,
            stored->mask_instances,
            stored->mask_instance_memory,
            stored->mask_instance_mapped
        );

        if (stored->mask_pipeline != VK_NULL_HANDLE) {
            vkDestroyPipeline(state->device, stored->mask_pipeline, nullptr);

            stored->mask_pipeline = VK_NULL_HANDLE;
        }

        if (stored->mask_layout != VK_NULL_HANDLE) {
            vkDestroyPipelineLayout(state->device, stored->mask_layout, nullptr);

            stored->mask_layout = VK_NULL_HANDLE;
        }

        vulkan_detail::destroy_instance_buffer(
            *state,
            stored->image_instances,
            stored->image_instance_memory,
            stored->image_instance_mapped
        );

        if (stored->image_pipeline != VK_NULL_HANDLE) {
            vkDestroyPipeline(state->device, stored->image_pipeline, nullptr);

            stored->image_pipeline = VK_NULL_HANDLE;
        }

        if (stored->image_layout != VK_NULL_HANDLE) {
            vkDestroyPipelineLayout(state->device, stored->image_layout, nullptr);

            stored->image_layout = VK_NULL_HANDLE;
        }

        vulkan_detail::destroy_texture_cache(*state, *stored);
        vulkan_detail::destroy_instance_buffer(
            *state,
            stored->rounded_instances,
            stored->rounded_instance_memory,
            stored->rounded_instance_mapped
        );
        vulkan_detail::destroy_instance_buffer(
            *state,
            stored->instances,
            stored->instance_memory,
            stored->instance_mapped
        );

        if (stored->rounded_pipeline != VK_NULL_HANDLE) {
            vkDestroyPipeline(state->device, stored->rounded_pipeline, nullptr);

            stored->rounded_pipeline = VK_NULL_HANDLE;
        }

        if (stored->rounded_layout != VK_NULL_HANDLE) {
            vkDestroyPipelineLayout(state->device, stored->rounded_layout, nullptr);

            stored->rounded_layout = VK_NULL_HANDLE;
        }

        if (stored->pipeline != VK_NULL_HANDLE) {
            vkDestroyPipeline(state->device, stored->pipeline, nullptr);

            stored->pipeline = VK_NULL_HANDLE;
        }

        if (stored->layout != VK_NULL_HANDLE) {
            vkDestroyPipelineLayout(state->device, stored->layout, nullptr);

            stored->layout = VK_NULL_HANDLE;
        }
    }

    delete stored;

    pipeline.pipeline_state = nullptr;
    pipeline.ready = false;
}
}
