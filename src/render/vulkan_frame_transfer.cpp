#include "vulkan_frame_transfer.hpp"

#include "software_renderer_internal.hpp"

#include <vulkan/vulkan.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>

namespace veyra::vulkan_detail {
namespace {

bool commands_begin(ContextState& state) noexcept {
    if (vkResetCommandBuffer(state.commands, 0) != VK_SUCCESS) {
        return false;
    }

    const auto begin_info = VkCommandBufferBeginInfo{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };

    if (vkBeginCommandBuffer(state.commands, &begin_info) != VK_SUCCESS) {
        return false;
    }

    return true;
}

void copy_image_to_staging(ContextState& state, SoftwareClipRect bounds) noexcept {
    const auto copy = VkBufferImageCopy{
        .bufferOffset = static_cast<VkDeviceSize>(
            (bounds.top * state.width + bounds.left) * sizeof(SoftwarePixel)
        ),
        .bufferRowLength = static_cast<std::uint32_t>(state.width),
        .bufferImageHeight = static_cast<std::uint32_t>(state.height),
        .imageSubresource =
            {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .layerCount = 1,
            },
        .imageOffset =
            {
                .x = static_cast<std::int32_t>(bounds.left),
                .y = static_cast<std::int32_t>(bounds.top),
            },
        .imageExtent = {
            .width = static_cast<std::uint32_t>(bounds.right - bounds.left),
            .height = static_cast<std::uint32_t>(bounds.bottom - bounds.top),
            .depth = 1,
        },
    };

    vkCmdCopyImageToBuffer(
        state.commands,
        state.target.image,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        state.target.staging,
        1,
        &copy
    );
}

}

bool begin_frame_commands(ContextState& state) noexcept {
    return commands_begin(state);
}

VulkanRenderResult end_frame_transfer(
    ContextState& state,
    std::uint64_t& gpu_ns,
    std::span<const LayoutRect> damage,
    const SoftwareClipRect* exposed,
    std::size_t exposed_count,
    bool full_copy
) noexcept {
    gpu_ns = 0;

    vkCmdEndRenderPass(state.commands);

    const auto copy_image = [&](SoftwareClipRect bounds) noexcept {
        copy_image_to_staging(state, bounds);
    };

    const auto to_transfer = VkImageMemoryBarrier{
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = state.target.image,
        .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .levelCount = 1,
            .layerCount = 1,
        },
    };

    vkCmdPipelineBarrier(
        state.commands,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        0,
        0,
        nullptr,
        0,
        0,
        1,
        &to_transfer
    );

    if (full_copy) {
        copy_image(
            SoftwareClipRect{
                .left = 0,
                .top = 0,
                .right = state.width,
                .bottom = state.height,
            }
        );
    } else {
        for (const auto& region : damage) {
            const auto bounds = render_detail::pixel_bounds(region, state.width, state.height);

            if (bounds.left >= bounds.right || bounds.top >= bounds.bottom) {
                continue;
            }

            copy_image(bounds);
        }

        for (auto index = std::size_t{}; index < exposed_count; ++index) {
            copy_image(exposed[index]);
        }
    }

    const auto restore = VkImageMemoryBarrier{
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
        .dstAccessMask = {},
        .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        .newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = state.target.image,
        .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .levelCount = 1,
            .layerCount = 1,
        },
    };

    vkCmdPipelineBarrier(
        state.commands,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        0,
        0,
        nullptr,
        0,
        nullptr,
        1,
        &restore
    );

    if (state.timestamp_pool != VK_NULL_HANDLE) {
        vkCmdWriteTimestamp(
            state.commands,
            VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
            state.timestamp_pool,
            1
        );
    }

    if (vkEndCommandBuffer(state.commands) != VK_SUCCESS) {
        return VulkanRenderResult::device_error;
    }

    vkResetFences(state.device, 1, &state.fence);

    const auto submit = VkSubmitInfo{
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1,
        .pCommandBuffers = &state.commands,
    };

    const auto submitted = vkQueueSubmit(state.queue, 1, &submit, state.fence);

    if (submitted == VK_ERROR_DEVICE_LOST) {
        state.device_lost = true;

        return VulkanRenderResult::device_lost;
    }

    if (submitted != VK_SUCCESS) {
        return VulkanRenderResult::device_error;
    }

    constexpr auto timeout_ns = 10'000'000'000ULL;

    const auto waited = vkWaitForFences(state.device, 1, &state.fence, VK_TRUE, timeout_ns);

    if (waited == VK_ERROR_DEVICE_LOST) {
        state.device_lost = true;

        return VulkanRenderResult::device_lost;
    }

    if (waited != VK_SUCCESS) {
        return VulkanRenderResult::device_error;
    }

    if (state.timestamp_pool != VK_NULL_HANDLE) {
        auto ticks = std::array<std::uint64_t, 2>{};

        if (vkGetQueryPoolResults(
                state.device,
                state.timestamp_pool,
                0,
                2,
                sizeof(ticks),
                ticks.data(),
                sizeof(std::uint64_t),
                VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT
            ) == VK_SUCCESS) {
            const auto elapsed = (ticks[1] - ticks[0]) & state.timestamp_mask;
            gpu_ns = static_cast<std::uint64_t>(
                static_cast<double>(elapsed) * static_cast<double>(state.timestamp_period)
            );
        }
    }

    return VulkanRenderResult::complete;
}

VulkanRenderResult retain_scrolled_image(
    ContextState& state,
    FrameBatch& batch,
    std::span<const LayoutRect> damage,
    std::int32_t scroll_x,
    std::int32_t scroll_y,
    std::array<SoftwareClipRect, 2>& exposed,
    std::size_t& exposed_count
) noexcept {
    exposed_count = 0;

    const auto scroll_x_64 = static_cast<std::int64_t>(scroll_x);
    const auto scroll_y_64 = static_cast<std::int64_t>(scroll_y);
    const auto surface_width = static_cast<std::int64_t>(state.width);
    const auto surface_height = static_cast<std::int64_t>(state.height);

    if (scroll_y_64 > 0) {
        exposed[exposed_count++] = SoftwareClipRect{
            .left = 0,
            .top = 0,
            .right = state.width,
            .bottom = static_cast<std::size_t>(scroll_y_64),
        };
    } else if (scroll_y_64 < 0) {
        exposed[exposed_count++] = SoftwareClipRect{
            .left = 0,
            .top = static_cast<std::size_t>(surface_height + scroll_y_64),
            .right = state.width,
            .bottom = state.height,
        };
    }

    if (scroll_x_64 != 0) {
        auto band_top = std::size_t{};
        auto band_bottom = state.height;

        if (scroll_y_64 > 0) {
            band_top = static_cast<std::size_t>(scroll_y_64);
        } else if (scroll_y_64 < 0) {
            band_bottom = static_cast<std::size_t>(surface_height + scroll_y_64);
        }

        if (scroll_x_64 > 0) {
            exposed[exposed_count++] = SoftwareClipRect{
                .left = 0,
                .top = band_top,
                .right = static_cast<std::size_t>(scroll_x_64),
                .bottom = band_bottom,
            };
        } else {
            exposed[exposed_count++] = SoftwareClipRect{
                .left = static_cast<std::size_t>(surface_width + scroll_x_64),
                .top = band_top,
                .right = state.width,
                .bottom = band_bottom,
            };
        }
    }

    for (auto exposed_index = std::size_t{}; exposed_index < exposed_count; ++exposed_index) {
        for (const auto& region : damage) {
            if (render_detail::overlaps(
                    exposed[exposed_index],
                    render_detail::pixel_bounds(region, state.width, state.height)
                )) {
                batch.abandon();

                return VulkanRenderResult::invalid_input;
            }
        }
    }

    const auto source_left = static_cast<std::size_t>(scroll_x_64 < 0 ? -scroll_x_64 : 0);
    const auto source_top = static_cast<std::size_t>(scroll_y_64 < 0 ? -scroll_y_64 : 0);
    const auto source_right =
        static_cast<std::size_t>(scroll_x_64 > 0 ? surface_width - scroll_x_64 : surface_width);
    const auto source_bottom =
        static_cast<std::size_t>(scroll_y_64 > 0 ? surface_height - scroll_y_64 : surface_height);
    const auto destination_left = static_cast<std::size_t>(scroll_x_64 > 0 ? scroll_x_64 : 0);
    const auto destination_top = static_cast<std::size_t>(scroll_y_64 > 0 ? scroll_y_64 : 0);

    const auto acquire = VkImageMemoryBarrier{
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .newLayout = VK_IMAGE_LAYOUT_GENERAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = state.target.image,
        .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .levelCount = 1,
            .layerCount = 1,
        },
    };

    vkCmdPipelineBarrier(
        state.commands,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        0,
        0,
        nullptr,
        0,
        0,
        1,
        &acquire
    );

    const auto shift = VkBufferImageCopy{
        .bufferOffset = static_cast<VkDeviceSize>(
            (source_top * state.width + source_left) * sizeof(SoftwarePixel)
        ),
        .bufferRowLength = static_cast<std::uint32_t>(state.width),
        .bufferImageHeight = static_cast<std::uint32_t>(state.height),
        .imageSubresource =
            {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .layerCount = 1,
            },
        .imageOffset =
            {
                .x = static_cast<std::int32_t>(source_left),
                .y = static_cast<std::int32_t>(source_top),
            },
        .imageExtent = {
            .width = static_cast<std::uint32_t>(source_right - source_left),
            .height = static_cast<std::uint32_t>(source_bottom - source_top),
            .depth = 1,
        },
    };

    vkCmdCopyImageToBuffer(
        state.commands,
        state.target.image,
        VK_IMAGE_LAYOUT_GENERAL,
        state.target.staging,
        1,
        &shift
    );

    const auto place = VkBufferImageCopy{
        .bufferOffset = shift.bufferOffset,
        .bufferRowLength = shift.bufferRowLength,
        .bufferImageHeight = shift.bufferImageHeight,
        .imageSubresource = shift.imageSubresource,
        .imageOffset =
            {
                .x = static_cast<std::int32_t>(destination_left),
                .y = static_cast<std::int32_t>(destination_top),
            },
        .imageExtent = shift.imageExtent,
    };

    vkCmdCopyBufferToImage(
        state.commands,
        state.target.staging,
        state.target.image,
        VK_IMAGE_LAYOUT_GENERAL,
        1,
        &place
    );

    const auto release = VkImageMemoryBarrier{
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
        .dstAccessMask = {},
        .oldLayout = VK_IMAGE_LAYOUT_GENERAL,
        .newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = state.target.image,
        .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .levelCount = 1,
            .layerCount = 1,
        },
    };

    vkCmdPipelineBarrier(
        state.commands,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        0,
        0,
        nullptr,
        0,
        0,
        1,
        &release
    );

    return VulkanRenderResult::complete;
}

void copy_frame_output(
    ContextState& state,
    std::span<SoftwarePixel> output,
    SoftwareClipRect bounds
) noexcept {
    const auto copy_row_bytes = (bounds.right - bounds.left) * sizeof(SoftwarePixel);

    for (auto y = bounds.top; y < bounds.bottom; ++y) {
        const auto destination = y * state.width + bounds.left;

        std::memcpy(
            output.data() + destination,
            static_cast<const SoftwarePixel*>(state.target.staging_mapped) + destination,
            copy_row_bytes
        );
    }
}

}
