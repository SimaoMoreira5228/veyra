#include "vulkan_internal.hpp"

#include "software_renderer_internal.hpp"
#include "veyra/render/glyph_atlas.hpp"

#include <vulkan/vulkan.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>

namespace veyra::vulkan_detail {

bool create_texture_cache(ContextState& state, PipelineState& pipeline) noexcept {
    const auto nearest_info = VkSamplerCreateInfo{
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter = VK_FILTER_NEAREST,
        .minFilter = VK_FILTER_NEAREST,
        .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .maxLod = 0.0F,
    };

    if (vkCreateSampler(state.device, &nearest_info, nullptr, &pipeline.nearest_sampler) !=
        VK_SUCCESS) {
        return false;
    }

    const auto linear_info = VkSamplerCreateInfo{
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter = VK_FILTER_LINEAR,
        .minFilter = VK_FILTER_LINEAR,
        .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .maxLod = 0.0F,
    };

    if (vkCreateSampler(state.device, &linear_info, nullptr, &pipeline.linear_sampler) !=
        VK_SUCCESS) {
        vkDestroySampler(state.device, pipeline.nearest_sampler, nullptr);

        pipeline.nearest_sampler = VK_NULL_HANDLE;

        return false;
    }

    const auto sampler_binding = VkDescriptorSetLayoutBinding{
        .binding = 0,
        .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .descriptorCount = 1,
        .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
    };

    const auto layout_info = VkDescriptorSetLayoutCreateInfo{
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = 1,
        .pBindings = &sampler_binding,
    };

    if (vkCreateDescriptorSetLayout(
            state.device,
            &layout_info,
            nullptr,
            &pipeline.descriptor_layout
        ) != VK_SUCCESS) {
        vkDestroySampler(state.device, pipeline.linear_sampler, nullptr);
        vkDestroySampler(state.device, pipeline.nearest_sampler, nullptr);

        pipeline.linear_sampler = VK_NULL_HANDLE;
        pipeline.nearest_sampler = VK_NULL_HANDLE;

        return false;
    }

    const auto pool_size = VkDescriptorPoolSize{
        .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .descriptorCount = kVulkanMaxFrameImages * 4,
    };

    const auto pool_info = VkDescriptorPoolCreateInfo{
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT,
        .maxSets = kVulkanMaxFrameImages * 4,
        .poolSizeCount = 1,
        .pPoolSizes = &pool_size,
    };

    if (vkCreateDescriptorPool(state.device, &pool_info, nullptr, &pipeline.descriptor_pool) !=
        VK_SUCCESS) {
        vkDestroyDescriptorSetLayout(state.device, pipeline.descriptor_layout, nullptr);
        vkDestroySampler(state.device, pipeline.linear_sampler, nullptr);
        vkDestroySampler(state.device, pipeline.nearest_sampler, nullptr);

        pipeline.descriptor_layout = VK_NULL_HANDLE;
        pipeline.linear_sampler = VK_NULL_HANDLE;
        pipeline.nearest_sampler = VK_NULL_HANDLE;

        return false;
    }

    const auto staging_info = VkBufferCreateInfo{
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = kVulkanImageStagingBytes,
        .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };

    if (vkCreateBuffer(state.device, &staging_info, nullptr, &pipeline.staging) != VK_SUCCESS) {
        vkDestroyDescriptorPool(state.device, pipeline.descriptor_pool, nullptr);
        vkDestroyDescriptorSetLayout(state.device, pipeline.descriptor_layout, nullptr);
        vkDestroySampler(state.device, pipeline.linear_sampler, nullptr);
        vkDestroySampler(state.device, pipeline.nearest_sampler, nullptr);

        pipeline.descriptor_pool = VK_NULL_HANDLE;
        pipeline.descriptor_layout = VK_NULL_HANDLE;
        pipeline.linear_sampler = VK_NULL_HANDLE;
        pipeline.nearest_sampler = VK_NULL_HANDLE;

        return false;
    }

    auto requirements = VkMemoryRequirements{};

    vkGetBufferMemoryRequirements(state.device, pipeline.staging, &requirements);

    auto memory_index = std::uint32_t{};

    const auto flags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

    if (!find_memory_type(state.physical, requirements.memoryTypeBits, flags, memory_index)) {
        vkDestroyBuffer(state.device, pipeline.staging, nullptr);

        pipeline.staging = VK_NULL_HANDLE;

        vkDestroyDescriptorPool(state.device, pipeline.descriptor_pool, nullptr);
        vkDestroyDescriptorSetLayout(state.device, pipeline.descriptor_layout, nullptr);
        vkDestroySampler(state.device, pipeline.linear_sampler, nullptr);
        vkDestroySampler(state.device, pipeline.nearest_sampler, nullptr);

        pipeline.descriptor_pool = VK_NULL_HANDLE;
        pipeline.descriptor_layout = VK_NULL_HANDLE;
        pipeline.linear_sampler = VK_NULL_HANDLE;
        pipeline.nearest_sampler = VK_NULL_HANDLE;

        return false;
    }

    const auto alloc_info = VkMemoryAllocateInfo{
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = requirements.size,
        .memoryTypeIndex = memory_index,
    };

    if (vkAllocateMemory(state.device, &alloc_info, nullptr, &pipeline.staging_memory) !=
        VK_SUCCESS) {
        vkDestroyBuffer(state.device, pipeline.staging, nullptr);

        pipeline.staging = VK_NULL_HANDLE;

        vkDestroyDescriptorPool(state.device, pipeline.descriptor_pool, nullptr);
        vkDestroyDescriptorSetLayout(state.device, pipeline.descriptor_layout, nullptr);
        vkDestroySampler(state.device, pipeline.linear_sampler, nullptr);
        vkDestroySampler(state.device, pipeline.nearest_sampler, nullptr);

        pipeline.descriptor_pool = VK_NULL_HANDLE;
        pipeline.descriptor_layout = VK_NULL_HANDLE;
        pipeline.linear_sampler = VK_NULL_HANDLE;
        pipeline.nearest_sampler = VK_NULL_HANDLE;

        return false;
    }

    if (vkBindBufferMemory(state.device, pipeline.staging, pipeline.staging_memory, 0) !=
        VK_SUCCESS) {
        vkFreeMemory(state.device, pipeline.staging_memory, nullptr);
        vkDestroyBuffer(state.device, pipeline.staging, nullptr);

        pipeline.staging_memory = VK_NULL_HANDLE;
        pipeline.staging = VK_NULL_HANDLE;

        vkDestroyDescriptorPool(state.device, pipeline.descriptor_pool, nullptr);
        vkDestroyDescriptorSetLayout(state.device, pipeline.descriptor_layout, nullptr);
        vkDestroySampler(state.device, pipeline.linear_sampler, nullptr);
        vkDestroySampler(state.device, pipeline.nearest_sampler, nullptr);

        pipeline.descriptor_pool = VK_NULL_HANDLE;
        pipeline.descriptor_layout = VK_NULL_HANDLE;
        pipeline.linear_sampler = VK_NULL_HANDLE;
        pipeline.nearest_sampler = VK_NULL_HANDLE;

        return false;
    }

    if (vkMapMemory(
            state.device,
            pipeline.staging_memory,
            0,
            requirements.size,
            0,
            &pipeline.staging_mapped
        ) != VK_SUCCESS) {
        vkFreeMemory(state.device, pipeline.staging_memory, nullptr);
        vkDestroyBuffer(state.device, pipeline.staging, nullptr);

        pipeline.staging_memory = VK_NULL_HANDLE;
        pipeline.staging = VK_NULL_HANDLE;

        vkDestroyDescriptorPool(state.device, pipeline.descriptor_pool, nullptr);
        vkDestroyDescriptorSetLayout(state.device, pipeline.descriptor_layout, nullptr);
        vkDestroySampler(state.device, pipeline.linear_sampler, nullptr);
        vkDestroySampler(state.device, pipeline.nearest_sampler, nullptr);

        pipeline.descriptor_pool = VK_NULL_HANDLE;
        pipeline.descriptor_layout = VK_NULL_HANDLE;
        pipeline.linear_sampler = VK_NULL_HANDLE;
        pipeline.nearest_sampler = VK_NULL_HANDLE;

        return false;
    }

    return true;
}

void evict_image_entry(
    ContextState& state,
    PipelineState& pipeline,
    ImageCacheEntry& entry
) noexcept {
    if (entry.nearest != VK_NULL_HANDLE) {
        vkFreeDescriptorSets(state.device, pipeline.descriptor_pool, 1, &entry.nearest);

        entry.nearest = VK_NULL_HANDLE;
    }

    if (entry.linear != VK_NULL_HANDLE) {
        vkFreeDescriptorSets(state.device, pipeline.descriptor_pool, 1, &entry.linear);

        entry.linear = VK_NULL_HANDLE;
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

void destroy_texture_cache(ContextState& state, PipelineState& pipeline) noexcept {
    for (auto& entry : pipeline.images) {
        if (entry.occupied) {
            evict_image_entry(state, pipeline, entry);
        }
    }

    for (auto& entry : pipeline.masks) {
        if (entry.occupied) {
            evict_mask_entry(state, pipeline, entry);
        }
    }

    if (pipeline.staging_mapped != nullptr) {
        vkUnmapMemory(state.device, pipeline.staging_memory);

        pipeline.staging_mapped = nullptr;
    }

    if (pipeline.staging != VK_NULL_HANDLE) {
        vkDestroyBuffer(state.device, pipeline.staging, nullptr);

        pipeline.staging = VK_NULL_HANDLE;
    }

    if (pipeline.staging_memory != VK_NULL_HANDLE) {
        vkFreeMemory(state.device, pipeline.staging_memory, nullptr);

        pipeline.staging_memory = VK_NULL_HANDLE;
    }

    if (pipeline.descriptor_pool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(state.device, pipeline.descriptor_pool, nullptr);

        pipeline.descriptor_pool = VK_NULL_HANDLE;
    }

    if (pipeline.descriptor_layout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(state.device, pipeline.descriptor_layout, nullptr);

        pipeline.descriptor_layout = VK_NULL_HANDLE;
    }

    if (pipeline.linear_sampler != VK_NULL_HANDLE) {
        vkDestroySampler(state.device, pipeline.linear_sampler, nullptr);

        pipeline.linear_sampler = VK_NULL_HANDLE;
    }

    if (pipeline.nearest_sampler != VK_NULL_HANDLE) {
        vkDestroySampler(state.device, pipeline.nearest_sampler, nullptr);

        pipeline.nearest_sampler = VK_NULL_HANDLE;
    }
}

VkDescriptorSet lookup_image(
    const PipelineState& pipeline,
    const SoftwareImage& image,
    SoftwareImageSampling sampling
) noexcept {
    for (const auto& entry : pipeline.images) {
        if (!entry.occupied || entry.key != &image || entry.generation != image.generation ||
            entry.width != image.width || entry.height != image.height) {
            continue;
        }

        if (sampling == SoftwareImageSampling::nearest) {
            return entry.nearest;
        }

        return entry.linear;
    }

    return VK_NULL_HANDLE;
}

void evict_frame_images(ContextState& state, PipelineState& pipeline) noexcept {
    for (auto& entry : pipeline.images) {
        if (entry.occupied && entry.frame_tag == pipeline.frame_seq) {
            evict_image_entry(state, pipeline, entry);
        }
    }

    for (auto& entry : pipeline.masks) {
        if (entry.occupied && entry.frame_tag == pipeline.frame_seq) {
            evict_mask_entry(state, pipeline, entry);
        }
    }
}

ImageResolveResult resolve_image(
    ContextState& state,
    PipelineState& pipeline,
    const SoftwareImage& image,
    SoftwareImageSampling sampling,
    VkDescriptorSet& descriptor,
    bool& uploaded
) noexcept {
    uploaded = false;

    for (auto& entry : pipeline.images) {
        if (!entry.occupied || entry.key != &image || entry.generation != image.generation ||
            entry.width != image.width || entry.height != image.height) {
            continue;
        }

        auto& cached = sampling == SoftwareImageSampling::nearest ? entry.nearest : entry.linear;

        if (cached == VK_NULL_HANDLE) {
            const auto sampler = sampling == SoftwareImageSampling::nearest
                ? pipeline.nearest_sampler
                : pipeline.linear_sampler;
            const auto alloc_info = VkDescriptorSetAllocateInfo{
                .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
                .descriptorPool = pipeline.descriptor_pool,
                .descriptorSetCount = 1,
                .pSetLayouts = &pipeline.descriptor_layout,
            };

            if (vkAllocateDescriptorSets(state.device, &alloc_info, &cached) != VK_SUCCESS) {
                return ImageResolveResult::device_error;
            }

            const auto image_info = VkDescriptorImageInfo{
                .sampler = sampler,
                .imageView = entry.view,
                .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            };

            const auto write = VkWriteDescriptorSet{
                .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .dstSet = cached,
                .dstBinding = 0,
                .descriptorCount = 1,
                .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                .pImageInfo = &image_info,
            };

            vkUpdateDescriptorSets(state.device, 1, &write, 0, nullptr);
        }

        descriptor = cached;

        return ImageResolveResult::ready;
    }

    const auto row_bytes = image.width * sizeof(SoftwarePixel);
    const auto upload_bytes = row_bytes * image.height;

    pipeline.staging_used =
        (pipeline.staging_used + static_cast<std::size_t>(3)) & ~static_cast<std::size_t>(3);

    if (upload_bytes == 0 || upload_bytes > kVulkanImageStagingBytes ||
        pipeline.staging_used + upload_bytes > kVulkanImageStagingBytes) {
        return ImageResolveResult::too_large;
    }

    auto slot = pipeline.evict_cursor % kVulkanMaxFrameImages;

    pipeline.evict_cursor += 1;

    for (auto index = std::size_t{}; index < kVulkanMaxFrameImages; ++index) {
        auto& candidate = pipeline.images[index];

        if (candidate.occupied && candidate.key == &image) {
            slot = index;

            break;
        }
    }

    auto& entry = pipeline.images[slot];

    if (entry.occupied) {
        evict_image_entry(state, pipeline, entry);
    }

    const auto image_info = VkImageCreateInfo{
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = VK_FORMAT_R8G8B8A8_UNORM,
        .extent =
            {
                .width = static_cast<std::uint32_t>(image.width),
                .height = static_cast<std::uint32_t>(image.height),
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
        return ImageResolveResult::device_error;
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

        return ImageResolveResult::device_error;
    }

    const auto alloc_info = VkMemoryAllocateInfo{
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = requirements.size,
        .memoryTypeIndex = memory_index,
    };

    if (vkAllocateMemory(state.device, &alloc_info, nullptr, &entry.memory) != VK_SUCCESS) {
        vkDestroyImage(state.device, entry.image, nullptr);

        entry.image = VK_NULL_HANDLE;

        return ImageResolveResult::device_error;
    }

    if (vkBindImageMemory(state.device, entry.image, entry.memory, 0) != VK_SUCCESS) {
        vkFreeMemory(state.device, entry.memory, nullptr);
        vkDestroyImage(state.device, entry.image, nullptr);

        entry.memory = VK_NULL_HANDLE;
        entry.image = VK_NULL_HANDLE;

        return ImageResolveResult::device_error;
    }

    const auto view_info = VkImageViewCreateInfo{
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = entry.image,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = VK_FORMAT_R8G8B8A8_UNORM,
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

        return ImageResolveResult::device_error;
    }

    auto* staging_bytes = static_cast<std::uint8_t*>(pipeline.staging_mapped);
    const auto staging_offset = pipeline.staging_used;

    pipeline.staging_used += upload_bytes;

    for (auto row = std::size_t{}; row < image.height; ++row) {
        std::memcpy(
            staging_bytes + staging_offset + row * row_bytes,
            image.pixels.data() + row * image.stride,
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
            .width = static_cast<std::uint32_t>(image.width),
            .height = static_cast<std::uint32_t>(image.height),
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

    const auto sampler = sampling == SoftwareImageSampling::nearest ? pipeline.nearest_sampler
                                                                    : pipeline.linear_sampler;
    const auto set_alloc_info = VkDescriptorSetAllocateInfo{
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = pipeline.descriptor_pool,
        .descriptorSetCount = 1,
        .pSetLayouts = &pipeline.descriptor_layout,
    };

    auto& cached = sampling == SoftwareImageSampling::nearest ? entry.nearest : entry.linear;

    if (vkAllocateDescriptorSets(state.device, &set_alloc_info, &cached) != VK_SUCCESS) {
        vkDestroyImageView(state.device, entry.view, nullptr);
        vkFreeMemory(state.device, entry.memory, nullptr);
        vkDestroyImage(state.device, entry.image, nullptr);

        entry.view = VK_NULL_HANDLE;
        entry.memory = VK_NULL_HANDLE;
        entry.image = VK_NULL_HANDLE;

        return ImageResolveResult::device_error;
    }

    const auto descriptor_image_info = VkDescriptorImageInfo{
        .sampler = sampler,
        .imageView = entry.view,
        .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
    };

    const auto write = VkWriteDescriptorSet{
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = cached,
        .dstBinding = 0,
        .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .pImageInfo = &descriptor_image_info,
    };

    vkUpdateDescriptorSets(state.device, 1, &write, 0, nullptr);

    entry.key = &image;
    entry.generation = image.generation;
    entry.frame_tag = pipeline.frame_seq;
    entry.width = image.width;
    entry.height = image.height;
    entry.occupied = true;

    descriptor = cached;
    uploaded = true;

    return ImageResolveResult::ready;
}

VulkanRenderResult upload_textures(
    vulkan_detail::ContextState& state,
    vulkan_detail::PipelineState& stored,
    std::span<const DisplayCommand> commands,
    std::size_t& uploads
) noexcept {
    for (const auto& command : commands) {
        if (const auto* drawn = std::get_if<DrawImage>(&command)) {
            if (drawn->image == nullptr || !render_detail::valid_image(*drawn->image) ||
                !render_detail::valid_image_placement(*drawn)) {
                return VulkanRenderResult::invalid_input;
            }

            if (drawn->opacity == 0) {
                continue;
            }

            if (drawn->source.width <= 0.0F || drawn->source.height <= 0.0F) {
                continue;
            }

            auto descriptor = VkDescriptorSet{VK_NULL_HANDLE};
            auto uploaded = false;

            const auto resolved = vulkan_detail::resolve_image(
                state,
                stored,
                *drawn->image,
                drawn->sampling,
                descriptor,
                uploaded
            );

            if (resolved == vulkan_detail::ImageResolveResult::too_large) {
                return VulkanRenderResult::insufficient_capacity;
            }

            if (resolved != vulkan_detail::ImageResolveResult::ready) {
                return VulkanRenderResult::device_error;
            }

            if (uploaded) {
                ++uploads;
            }

            continue;
        }

        const auto* masked = std::get_if<DrawMask>(&command);
        const auto* glyphed = std::get_if<DrawGlyph>(&command);

        if (masked == nullptr && glyphed == nullptr) {
            continue;
        }

        if (masked != nullptr) {
            if (masked->mask == nullptr || masked->mask->stride < masked->mask->width ||
                masked->mask->coverage.size() < masked->mask->height * masked->mask->stride) {
                return VulkanRenderResult::invalid_input;
            }

            if (masked->color.alpha == 0 || masked->mask->width == 0 || masked->mask->height == 0) {
                continue;
            }

            auto descriptor = VkDescriptorSet{VK_NULL_HANDLE};
            auto uploaded = false;

            const auto resolved =
                vulkan_detail::resolve_mask(state, stored, *masked->mask, descriptor, uploaded);

            if (resolved == vulkan_detail::MaskResolveResult::too_large) {
                return VulkanRenderResult::insufficient_capacity;
            }

            if (resolved != vulkan_detail::MaskResolveResult::ready) {
                return VulkanRenderResult::device_error;
            }

            if (uploaded) {
                ++uploads;
            }

            continue;
        }

        if (glyphed->atlas == nullptr) {
            return VulkanRenderResult::invalid_input;
        }

        const auto& atlas_view = glyphed->atlas->view();

        if (atlas_view.stride < atlas_view.width ||
            atlas_view.coverage.size() < atlas_view.height * atlas_view.stride) {
            return VulkanRenderResult::invalid_input;
        }

        if (glyphed->color.alpha == 0 || glyphed->width == 0 || glyphed->height == 0) {
            continue;
        }

        auto glyph_descriptor = VkDescriptorSet{VK_NULL_HANDLE};
        auto glyph_uploaded = false;

        const auto glyph_resolved = vulkan_detail::resolve_mask(
            state,
            stored,
            atlas_view,
            glyph_descriptor,
            glyph_uploaded
        );

        if (glyph_resolved == vulkan_detail::MaskResolveResult::too_large) {
            return VulkanRenderResult::insufficient_capacity;
        }

        if (glyph_resolved != vulkan_detail::MaskResolveResult::ready) {
            return VulkanRenderResult::device_error;
        }

        if (glyph_uploaded) {
            ++uploads;
        }
    }

    return VulkanRenderResult::complete;
}

}
