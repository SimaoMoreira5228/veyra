#include "veyra/render/vulkan_present.hpp"

#include "vulkan_present_internal.hpp"

#include <vulkan/vulkan.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>

namespace veyra {
namespace {

bool begin_present_commands(PresenterState& state) noexcept {
    if (vkResetCommandBuffer(state.commands, 0) != VK_SUCCESS) {
        return false;
    }

    const auto begin_info = VkCommandBufferBeginInfo{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };

    return vkBeginCommandBuffer(state.commands, &begin_info) == VK_SUCCESS;
}

void barrier_image(
    PresenterState& state,
    VkImage image,
    VkImageLayout old_layout,
    VkImageLayout new_layout,
    VkAccessFlags src_access,
    VkAccessFlags dst_access,
    VkPipelineStageFlags src_stage,
    VkPipelineStageFlags dst_stage
) noexcept {
    const auto barrier = VkImageMemoryBarrier{
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask = src_access,
        .dstAccessMask = dst_access,
        .oldLayout = old_layout,
        .newLayout = new_layout,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = image,
        .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .levelCount = 1,
            .layerCount = 1,
        },
    };

    vkCmdPipelineBarrier(
        state.commands,
        src_stage,
        dst_stage,
        0,
        0,
        nullptr,
        0,
        nullptr,
        1,
        &barrier
    );
}

void barrier_readback(PresenterState& state) noexcept {
    const auto barrier = VkBufferMemoryBarrier{
        .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_HOST_READ_BIT,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .buffer = state.readback,
        .offset = 0,
        .size = VK_WHOLE_SIZE,
    };

    vkCmdPipelineBarrier(
        state.commands,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_HOST_BIT,
        0,
        0,
        nullptr,
        1,
        &barrier,
        0,
        nullptr
    );
}

void copy_target_into(PresenterState& state, VkImage destination, bool rendered) noexcept {
    auto* context = state.context;
    const auto width = static_cast<std::uint32_t>(state.width);
    const auto height = static_cast<std::uint32_t>(state.height);

    barrier_image(
        state,
        destination,
        VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        {},
        VK_ACCESS_TRANSFER_WRITE_BIT,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT
    );

    if (!rendered) {
        const auto black = VkClearColorValue{.float32 = {0.0F, 0.0F, 0.0F, 1.0F}};
        const auto range = VkImageSubresourceRange{
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .levelCount = 1,
            .layerCount = 1,
        };

        vkCmdClearColorImage(
            state.commands,
            destination,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            &black,
            1,
            &range
        );

        vkCmdFillBuffer(state.commands, state.readback, 0, VK_WHOLE_SIZE, kOpaqueBlackDword);
        barrier_readback(state);

        barrier_image(
            state,
            destination,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
            VK_ACCESS_TRANSFER_WRITE_BIT,
            {},
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT
        );

        return;
    }

    barrier_image(
        state,
        context->target.image,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
        VK_ACCESS_TRANSFER_READ_BIT,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT
    );

    const auto region = VkImageCopy{
        .srcSubresource = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .layerCount = 1},
        .dstSubresource = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .layerCount = 1},
        .extent = {.width = width, .height = height, .depth = 1},
    };

    vkCmdCopyImage(
        state.commands,
        context->target.image,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        destination,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        1,
        &region
    );

    const auto to_buffer = VkBufferImageCopy{
        .bufferOffset = 0,
        .bufferRowLength = width,
        .bufferImageHeight = height,
        .imageSubresource = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .layerCount = 1},
        .imageExtent = {.width = width, .height = height, .depth = 1},
    };

    vkCmdCopyImageToBuffer(
        state.commands,
        context->target.image,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        state.readback,
        1,
        &to_buffer
    );

    barrier_readback(state);

    barrier_image(
        state,
        context->target.image,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        VK_ACCESS_TRANSFER_READ_BIT,
        {},
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT
    );

    barrier_image(
        state,
        destination,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        VK_ACCESS_TRANSFER_WRITE_BIT,
        {},
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT
    );
}

VulkanPresentResult submit_present_copy(PresenterState& state) noexcept {
    auto* context = state.context;

    if (vkEndCommandBuffer(state.commands) != VK_SUCCESS) {
        return VulkanPresentResult::creation_failed;
    }

    vkResetFences(context->device, 1, &state.fence);

    const auto submit = VkSubmitInfo{
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1,
        .pCommandBuffers = &state.commands,
    };

    const auto submitted = vkQueueSubmit(context->queue, 1, &submit, state.fence);

    if (submitted == VK_ERROR_DEVICE_LOST) {
        mark_device_lost(state);

        return VulkanPresentResult::device_lost;
    }

    if (submitted != VK_SUCCESS) {
        return VulkanPresentResult::creation_failed;
    }

    const auto waited =
        vkWaitForFences(context->device, 1, &state.fence, VK_TRUE, kPresentFenceTimeoutNs);

    if (waited == VK_ERROR_DEVICE_LOST) {
        mark_device_lost(state);

        return VulkanPresentResult::device_lost;
    }

    if (waited != VK_SUCCESS) {
        return VulkanPresentResult::creation_failed;
    }

    return VulkanPresentResult::ready;
}

}

VulkanPresentResult present_vulkan_frame(
    VulkanPresenter& presenter,
    std::uint32_t image,
    VulkanPresentStats* stats
) noexcept {
    auto* state = state_of(presenter);

    if (state == nullptr || state->context == nullptr || !presenter.ready) {
        return VulkanPresentResult::invalid_input;
    }

    if (image >= state->image_count) {
        return VulkanPresentResult::invalid_input;
    }

    if (state->device_lost || state->context->device_lost) {
        mark_device_lost(*state);

        return VulkanPresentResult::device_lost;
    }

    if (state->swapchain == VK_NULL_HANDLE || state->commands == VK_NULL_HANDLE) {
        return VulkanPresentResult::invalid_input;
    }

    auto* context = state->context;

    if (!begin_present_commands(*state)) {
        return VulkanPresentResult::creation_failed;
    }

    // NOTE: target_valid tracks whether the borrowed target image has rendered
    // at least once since create/resize; only then is COLOR_ATTACHMENT_OPTIMAL
    // a legal source layout, otherwise the swapchain clears to opaque black.
    copy_target_into(*state, state->images[image], context->target_valid);

    const auto copied = submit_present_copy(*state);

    if (copied != VulkanPresentResult::ready) {
        return copied;
    }

    const auto present_info = VkPresentInfoKHR{
        .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .swapchainCount = 1,
        .pSwapchains = &state->swapchain,
        .pImageIndices = &image,
    };

    const auto presented = vkQueuePresentKHR(context->queue, &present_info);

    if (presented == VK_ERROR_DEVICE_LOST) {
        mark_device_lost(*state);

        return VulkanPresentResult::device_lost;
    }

    if (stats != nullptr) {
        *stats = VulkanPresentStats{
            .gpu_ns = 0,
            .gpu_timed = false,
            .image = image,
        };
    }

    if (presented == VK_ERROR_OUT_OF_DATE_KHR || presented == VK_SUBOPTIMAL_KHR) {
        return VulkanPresentResult::needs_recreate;
    }

    if (presented != VK_SUCCESS) {
        return VulkanPresentResult::creation_failed;
    }

    return VulkanPresentResult::ready;
}

VulkanPresentResult read_vulkan_presenter_pixels(
    const VulkanPresenter& presenter,
    std::span<SoftwarePixel> output
) noexcept {
    const auto* state = state_of(presenter);

    if (state == nullptr || !presenter.ready) {
        return VulkanPresentResult::invalid_input;
    }

    if (output.size() != state->width * state->height) {
        return VulkanPresentResult::invalid_input;
    }

    if (state->readback_mapped == nullptr) {
        return VulkanPresentResult::invalid_input;
    }

    std::memcpy(output.data(), state->readback_mapped, output.size() * sizeof(SoftwarePixel));

    return VulkanPresentResult::ready;
}

}
