#include "vulkan_internal.hpp"

#include <vulkan/vulkan.h>

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace veyra::vulkan_detail {

void evict_mask_entry(
    ContextState& state,
    PipelineState& pipeline,
    MaskCacheEntry& entry
) noexcept {
    if (entry.set != VK_NULL_HANDLE) {
        vkFreeDescriptorSets(state.device, pipeline.descriptor_pool, 1, &entry.set);

        entry.set = VK_NULL_HANDLE;
    }

    if (entry.view != VK_NULL_HANDLE) {
        vkDestroyImageView(state.device, entry.view, nullptr);

        entry.view = VK_NULL_HANDLE;
    }

    if (entry.image != VK_NULL_HANDLE) {
        vkDestroyImage(state.device, entry.image, nullptr);

        entry.image = VK_NULL_HANDLE;
    }

    if (entry.memory != VK_NULL_HANDLE) {
        vkFreeMemory(state.device, entry.memory, nullptr);

        entry.memory = VK_NULL_HANDLE;
    }

    entry.occupied = false;
}

VkDescriptorSet lookup_mask(const PipelineState& pipeline, const SoftwareMask& mask) noexcept {
    for (const auto& entry : pipeline.masks) {
        if (!entry.occupied || entry.key != &mask || entry.generation != mask.generation ||
            entry.width != mask.width || entry.height != mask.height) {
            continue;
        }

        return entry.set;
    }

    return VK_NULL_HANDLE;
}

MaskResolveResult resolve_mask(
    ContextState& state,
    PipelineState& pipeline,
    const SoftwareMask& mask,
    VkDescriptorSet& descriptor,
    std::size_t& uploaded_bytes
) noexcept {
    uploaded_bytes = 0;

    for (auto& entry : pipeline.masks) {
        if (!entry.occupied || entry.key != &mask || entry.generation != mask.generation ||
            entry.width != mask.width || entry.height != mask.height) {
            continue;
        }

        if (entry.set == VK_NULL_HANDLE) {
            const auto set_alloc_info = VkDescriptorSetAllocateInfo{
                .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
                .descriptorPool = pipeline.descriptor_pool,
                .descriptorSetCount = 1,
                .pSetLayouts = &pipeline.descriptor_layout,
            };

            if (vkAllocateDescriptorSets(state.device, &set_alloc_info, &entry.set) != VK_SUCCESS) {
                return MaskResolveResult::device_error;
            }

            const auto set_image_info = VkDescriptorImageInfo{
                .sampler = pipeline.nearest_sampler,
                .imageView = entry.view,
                .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            };

            const auto set_write = VkWriteDescriptorSet{
                .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .dstSet = entry.set,
                .dstBinding = 0,
                .descriptorCount = 1,
                .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                .pImageInfo = &set_image_info,
            };

            vkUpdateDescriptorSets(state.device, 1, &set_write, 0, nullptr);
        }

        descriptor = entry.set;

        return MaskResolveResult::ready;
    }

    const auto row_bytes = mask.width;
    const auto upload_bytes = row_bytes * mask.height;

    pipeline.staging_used =
        (pipeline.staging_used + static_cast<std::size_t>(3)) & ~static_cast<std::size_t>(3);

    if (upload_bytes == 0 || pipeline.staging_used + upload_bytes > kVulkanImageStagingBytes) {
        return MaskResolveResult::too_large;
    }

    auto slot = pipeline.mask_evict_cursor % kVulkanMaxFrameImages;

    pipeline.mask_evict_cursor += 1;

    for (auto index = std::size_t{}; index < kVulkanMaxFrameImages; ++index) {
        auto& candidate = pipeline.masks[index];

        if (candidate.occupied && candidate.key == &mask) {
            slot = index;

            break;
        }
    }

    auto& entry = pipeline.masks[slot];

    if (entry.occupied) {
        evict_mask_entry(state, pipeline, entry);
    }

    const auto image_info = VkImageCreateInfo{
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = VK_FORMAT_R8_UNORM,
        .extent =
            {
                .width = static_cast<std::uint32_t>(mask.width),
                .height = static_cast<std::uint32_t>(mask.height),
                .depth = 1,
            },
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };

    if (vkCreateImage(state.device, &image_info, nullptr, &entry.image) != VK_SUCCESS) {
        return MaskResolveResult::device_error;
    }

    auto requirements = VkMemoryRequirements{};

    vkGetImageMemoryRequirements(state.device, entry.image, &requirements);

    auto memory_index = std::uint32_t{};

    if (!find_memory_type(
            state.physical,
            requirements.memoryTypeBits,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            memory_index
        )) {
        vkDestroyImage(state.device, entry.image, nullptr);

        entry.image = VK_NULL_HANDLE;

        return MaskResolveResult::device_error;
    }

    const auto alloc_info = VkMemoryAllocateInfo{
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = requirements.size,
        .memoryTypeIndex = memory_index,
    };

    if (vkAllocateMemory(state.device, &alloc_info, nullptr, &entry.memory) != VK_SUCCESS) {
        vkDestroyImage(state.device, entry.image, nullptr);

        entry.image = VK_NULL_HANDLE;

        return MaskResolveResult::device_error;
    }

    if (vkBindImageMemory(state.device, entry.image, entry.memory, 0) != VK_SUCCESS) {
        vkFreeMemory(state.device, entry.memory, nullptr);
        vkDestroyImage(state.device, entry.image, nullptr);

        entry.memory = VK_NULL_HANDLE;
        entry.image = VK_NULL_HANDLE;

        return MaskResolveResult::device_error;
    }

    const auto view_info = VkImageViewCreateInfo{
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = entry.image,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = VK_FORMAT_R8_UNORM,
        .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1,
        },
    };

    if (vkCreateImageView(state.device, &view_info, nullptr, &entry.view) != VK_SUCCESS) {
        vkFreeMemory(state.device, entry.memory, nullptr);
        vkDestroyImage(state.device, entry.image, nullptr);

        entry.memory = VK_NULL_HANDLE;
        entry.image = VK_NULL_HANDLE;

        return MaskResolveResult::device_error;
    }

    auto* staging_bytes = static_cast<std::uint8_t*>(pipeline.staging_mapped);
    const auto staging_offset = pipeline.staging_used;

    pipeline.staging_used += upload_bytes;

    for (auto row = std::size_t{}; row < mask.height; ++row) {
        std::memcpy(
            staging_bytes + staging_offset + row * row_bytes,
            mask.coverage.data() + row * mask.stride,
            row_bytes
        );
    }

    const auto to_transfer = VkImageMemoryBarrier{
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask = 0,
        .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = entry.image,
        .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1,
        },
    };

    vkCmdPipelineBarrier(
        state.commands,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        0,
        0,
        nullptr,
        0,
        nullptr,
        1,
        &to_transfer
    );

    const auto copy = VkBufferImageCopy{
        .bufferOffset = staging_offset,
        .bufferRowLength = 0,
        .bufferImageHeight = 0,
        .imageSubresource =
            {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .mipLevel = 0,
                .baseArrayLayer = 0,
                .layerCount = 1,
            },
        .imageOffset = {0, 0, 0},
        .imageExtent = {
            .width = static_cast<std::uint32_t>(mask.width),
            .height = static_cast<std::uint32_t>(mask.height),
            .depth = 1,
        },
    };

    vkCmdCopyBufferToImage(
        state.commands,
        pipeline.staging,
        entry.image,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        1,
        &copy
    );

    const auto to_sampled = VkImageMemoryBarrier{
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        .newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = entry.image,
        .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1,
        },
    };

    vkCmdPipelineBarrier(
        state.commands,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        0,
        0,
        nullptr,
        0,
        nullptr,
        1,
        &to_sampled
    );

    const auto set_alloc_info = VkDescriptorSetAllocateInfo{
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = pipeline.descriptor_pool,
        .descriptorSetCount = 1,
        .pSetLayouts = &pipeline.descriptor_layout,
    };

    if (vkAllocateDescriptorSets(state.device, &set_alloc_info, &entry.set) != VK_SUCCESS) {
        vkDestroyImageView(state.device, entry.view, nullptr);
        vkFreeMemory(state.device, entry.memory, nullptr);
        vkDestroyImage(state.device, entry.image, nullptr);

        entry.view = VK_NULL_HANDLE;
        entry.memory = VK_NULL_HANDLE;
        entry.image = VK_NULL_HANDLE;

        return MaskResolveResult::device_error;
    }

    const auto descriptor_image_info = VkDescriptorImageInfo{
        .sampler = pipeline.nearest_sampler,
        .imageView = entry.view,
        .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
    };

    const auto descriptor_write = VkWriteDescriptorSet{
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = entry.set,
        .dstBinding = 0,
        .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .pImageInfo = &descriptor_image_info,
    };

    vkUpdateDescriptorSets(state.device, 1, &descriptor_write, 0, nullptr);

    entry.key = &mask;
    entry.generation = mask.generation;
    entry.frame_tag = pipeline.frame_seq;
    entry.width = mask.width;
    entry.height = mask.height;
    entry.occupied = true;

    descriptor = entry.set;
    uploaded_bytes = upload_bytes;

    return MaskResolveResult::ready;
}
}
