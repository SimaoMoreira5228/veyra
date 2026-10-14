#include "veyra/render/vulkan_context.hpp"

#include "vulkan_internal.hpp"

#include <vulkan/vulkan.h>

#include <cstddef>
#include <cstdint>
#include <cstdlib>

namespace veyra::vulkan_detail {

ContextState* state_of(VulkanContext& context) noexcept {
    return static_cast<ContextState*>(context.instance_state);
}

const ContextState* state_of(const VulkanContext& context) noexcept {
    return static_cast<const ContextState*>(context.instance_state);
}

PipelineState* pipeline_of(VulkanRectPipeline& pipeline) noexcept {
    return static_cast<PipelineState*>(pipeline.pipeline_state);
}

bool find_memory_type(
    VkPhysicalDevice physical,
    std::uint32_t type_bits,
    VkMemoryPropertyFlags required,
    std::uint32_t& index
) noexcept {
    auto heaps = VkPhysicalDeviceMemoryProperties{};

    vkGetPhysicalDeviceMemoryProperties(physical, &heaps);

    for (auto i = std::uint32_t{}; i < heaps.memoryTypeCount; ++i) {
        const auto matches = (type_bits & (1U << i)) != 0U;
        const auto flags = heaps.memoryTypes[i].propertyFlags;

        if (matches && (flags & required) == required) {
            index = i;

            return true;
        }
    }

    return false;
}

void copy_device_name(char (&output)[256], const char* source) noexcept {
    auto i = std::size_t{};

    for (; i + 1 < sizeof(output) && source[i] != '\0'; ++i) {
        output[i] = source[i];
    }

    output[i] = '\0';
}

void copy_icd_hint(char (&output)[64]) noexcept {
    const char* hint = std::getenv("VK_ICD_FILENAMES");

    if (hint == nullptr) {
        hint = "default";
    }

    auto i = std::size_t{};

    for (; i + 1 < sizeof(output) && hint[i] != '\0'; ++i) {
        output[i] = hint[i];
    }

    output[i] = '\0';
}

bool create_image(ContextState& state, std::size_t width, std::size_t height) noexcept {
    const auto image_info = VkImageCreateInfo{
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = VK_FORMAT_R8G8B8A8_UNORM,
        .extent =
            {
                .width = static_cast<std::uint32_t>(width),
                .height = static_cast<std::uint32_t>(height),
                .depth = 1,
            },
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
            VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };

    if (vkCreateImage(state.device, &image_info, nullptr, &state.target.image) != VK_SUCCESS) {
        return false;
    }

    auto requirements = VkMemoryRequirements{};

    vkGetImageMemoryRequirements(state.device, state.target.image, &requirements);

    auto memory_index = std::uint32_t{};

    if (!find_memory_type(
            state.physical,
            requirements.memoryTypeBits,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            memory_index
        )) {
        vkDestroyImage(state.device, state.target.image, nullptr);

        state.target.image = VK_NULL_HANDLE;

        return false;
    }

    const auto alloc_info = VkMemoryAllocateInfo{
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = requirements.size,
        .memoryTypeIndex = memory_index,
    };

    if (vkAllocateMemory(state.device, &alloc_info, nullptr, &state.target.memory) != VK_SUCCESS) {
        vkDestroyImage(state.device, state.target.image, nullptr);

        state.target.image = VK_NULL_HANDLE;

        return false;
    }

    if (vkBindImageMemory(state.device, state.target.image, state.target.memory, 0) != VK_SUCCESS) {
        vkFreeMemory(state.device, state.target.memory, nullptr);
        vkDestroyImage(state.device, state.target.image, nullptr);

        state.target.memory = VK_NULL_HANDLE;
        state.target.image = VK_NULL_HANDLE;

        return false;
    }

    const auto view_info = VkImageViewCreateInfo{
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = state.target.image,
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

    if (vkCreateImageView(state.device, &view_info, nullptr, &state.target.view) != VK_SUCCESS) {
        vkFreeMemory(state.device, state.target.memory, nullptr);
        vkDestroyImage(state.device, state.target.image, nullptr);

        state.target.memory = VK_NULL_HANDLE;
        state.target.image = VK_NULL_HANDLE;

        return false;
    }

    return true;
}

bool create_staging(ContextState& state, std::size_t width, std::size_t height) noexcept {
    state.target.staging_bytes = width * height * 4;

    const auto buffer_info = VkBufferCreateInfo{
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = static_cast<VkDeviceSize>(state.target.staging_bytes),
        .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };

    if (vkCreateBuffer(state.device, &buffer_info, nullptr, &state.target.staging) != VK_SUCCESS) {
        return false;
    }

    auto requirements = VkMemoryRequirements{};

    vkGetBufferMemoryRequirements(state.device, state.target.staging, &requirements);

    auto memory_index = std::uint32_t{};

    const auto flags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

    if (!find_memory_type(state.physical, requirements.memoryTypeBits, flags, memory_index)) {
        vkDestroyBuffer(state.device, state.target.staging, nullptr);

        state.target.staging = VK_NULL_HANDLE;

        return false;
    }

    const auto alloc_info = VkMemoryAllocateInfo{
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = requirements.size,
        .memoryTypeIndex = memory_index,
    };

    if (vkAllocateMemory(state.device, &alloc_info, nullptr, &state.target.staging_memory) !=
        VK_SUCCESS) {
        vkDestroyBuffer(state.device, state.target.staging, nullptr);

        state.target.staging = VK_NULL_HANDLE;

        return false;
    }

    if (vkBindBufferMemory(state.device, state.target.staging, state.target.staging_memory, 0) !=
        VK_SUCCESS) {
        vkFreeMemory(state.device, state.target.staging_memory, nullptr);
        vkDestroyBuffer(state.device, state.target.staging, nullptr);

        state.target.staging_memory = VK_NULL_HANDLE;
        state.target.staging = VK_NULL_HANDLE;

        return false;
    }

    if (vkMapMemory(
            state.device,
            state.target.staging_memory,
            0,
            requirements.size,
            0,
            &state.target.staging_mapped
        ) != VK_SUCCESS) {
        vkFreeMemory(state.device, state.target.staging_memory, nullptr);
        vkDestroyBuffer(state.device, state.target.staging, nullptr);

        state.target.staging_memory = VK_NULL_HANDLE;
        state.target.staging = VK_NULL_HANDLE;

        return false;
    }

    return true;
}

bool create_target(ContextState& state, std::size_t width, std::size_t height) noexcept {
    if (!create_image(state, width, height)) {
        return false;
    }

    if (!create_staging(state, width, height)) {
        destroy_target(state);

        return false;
    }

    return true;
}

void destroy_target(ContextState& state) noexcept {
    if (state.target.staging_mapped != nullptr) {
        vkUnmapMemory(state.device, state.target.staging_memory);

        state.target.staging_mapped = nullptr;
    }

    if (state.target.staging != VK_NULL_HANDLE) {
        vkDestroyBuffer(state.device, state.target.staging, nullptr);

        state.target.staging = VK_NULL_HANDLE;
    }

    if (state.target.staging_memory != VK_NULL_HANDLE) {
        vkFreeMemory(state.device, state.target.staging_memory, nullptr);

        state.target.staging_memory = VK_NULL_HANDLE;
    }

    if (state.target.view != VK_NULL_HANDLE) {
        vkDestroyImageView(state.device, state.target.view, nullptr);

        state.target.view = VK_NULL_HANDLE;
    }

    if (state.target.image != VK_NULL_HANDLE) {
        vkDestroyImage(state.device, state.target.image, nullptr);

        state.target.image = VK_NULL_HANDLE;
    }

    if (state.target.memory != VK_NULL_HANDLE) {
        vkFreeMemory(state.device, state.target.memory, nullptr);

        state.target.memory = VK_NULL_HANDLE;
    }

    state.target.staging_bytes = 0;
}

bool create_framebuffer(ContextState& state) noexcept {
    if (state.shared_pass == VK_NULL_HANDLE) {
        return true;
    }

    if (state.target.view == VK_NULL_HANDLE) {
        return false;
    }

    const auto framebuffer_info = VkFramebufferCreateInfo{
        .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
        .renderPass = state.shared_pass,
        .attachmentCount = 1,
        .pAttachments = &state.target.view,
        .width = static_cast<std::uint32_t>(state.width),
        .height = static_cast<std::uint32_t>(state.height),
        .layers = 1,
    };

    if (vkCreateFramebuffer(state.device, &framebuffer_info, nullptr, &state.framebuffer) !=
        VK_SUCCESS) {
        return false;
    }

    return true;
}

void destroy_framebuffer(ContextState& state) noexcept {
    if (state.framebuffer != VK_NULL_HANDLE) {
        vkDestroyFramebuffer(state.device, state.framebuffer, nullptr);

        state.framebuffer = VK_NULL_HANDLE;
    }
}

bool create_instance_buffer(
    ContextState& state,
    VkBuffer& buffer,
    VkDeviceMemory& memory,
    void*& mapped,
    std::size_t bytes
) noexcept {
    const auto buffer_info = VkBufferCreateInfo{
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = bytes,
        .usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };

    if (vkCreateBuffer(state.device, &buffer_info, nullptr, &buffer) != VK_SUCCESS) {
        return false;
    }

    auto requirements = VkMemoryRequirements{};

    vkGetBufferMemoryRequirements(state.device, buffer, &requirements);

    auto memory_index = std::uint32_t{};

    const auto flags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

    if (!find_memory_type(state.physical, requirements.memoryTypeBits, flags, memory_index)) {
        vkDestroyBuffer(state.device, buffer, nullptr);

        buffer = VK_NULL_HANDLE;

        return false;
    }

    const auto alloc_info = VkMemoryAllocateInfo{
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = requirements.size,
        .memoryTypeIndex = memory_index,
    };

    if (vkAllocateMemory(state.device, &alloc_info, nullptr, &memory) != VK_SUCCESS) {
        vkDestroyBuffer(state.device, buffer, nullptr);

        buffer = VK_NULL_HANDLE;

        return false;
    }

    if (vkBindBufferMemory(state.device, buffer, memory, 0) != VK_SUCCESS) {
        vkFreeMemory(state.device, memory, nullptr);
        vkDestroyBuffer(state.device, buffer, nullptr);

        memory = VK_NULL_HANDLE;
        buffer = VK_NULL_HANDLE;

        return false;
    }

    if (vkMapMemory(state.device, memory, 0, requirements.size, 0, &mapped) != VK_SUCCESS) {
        vkFreeMemory(state.device, memory, nullptr);
        vkDestroyBuffer(state.device, buffer, nullptr);

        memory = VK_NULL_HANDLE;
        buffer = VK_NULL_HANDLE;

        return false;
    }

    return true;
}

void destroy_instance_buffer(
    ContextState& state,
    VkBuffer& buffer,
    VkDeviceMemory& memory,
    void*& mapped
) noexcept {
    if (mapped != nullptr) {
        vkUnmapMemory(state.device, memory);

        mapped = nullptr;
    }

    if (buffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(state.device, buffer, nullptr);

        buffer = VK_NULL_HANDLE;
    }

    if (memory != VK_NULL_HANDLE) {
        vkFreeMemory(state.device, memory, nullptr);

        memory = VK_NULL_HANDLE;
    }
}

}
