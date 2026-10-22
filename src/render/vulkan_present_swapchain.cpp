#include "veyra/render/vulkan_present.hpp"

#include "vulkan_present_internal.hpp"

#include <vulkan/vulkan.h>

#include <array>
#include <cstddef>
#include <cstdint>

namespace veyra {
namespace {

bool pick_surface_format(
    VkPhysicalDevice physical,
    VkSurfaceKHR surface,
    VkFormat& format,
    VkColorSpaceKHR& color_space
) noexcept {
    auto formats = std::array<VkSurfaceFormatKHR, kMaxSurfaceFormats>{};
    auto count = std::uint32_t{kMaxSurfaceFormats};

    const auto enumerated =
        vkGetPhysicalDeviceSurfaceFormatsKHR(physical, surface, &count, formats.data());

    if (enumerated != VK_SUCCESS && enumerated != VK_INCOMPLETE) {
        return false;
    }

    if (count == 0) {
        return false;
    }

    if (count > kMaxSurfaceFormats) {
        count = kMaxSurfaceFormats;
    }

    for (auto i = std::uint32_t{}; i < count; ++i) {
        if (formats[i].format == VK_FORMAT_R8G8B8A8_UNORM) {
            format = formats[i].format;
            color_space = formats[i].colorSpace;

            return true;
        }
    }

    return false;
}

bool pick_present_fifo(VkPhysicalDevice physical, VkSurfaceKHR surface) noexcept {
    auto modes = std::array<VkPresentModeKHR, kMaxPresentModes>{};
    auto count = std::uint32_t{kMaxPresentModes};

    const auto enumerated =
        vkGetPhysicalDeviceSurfacePresentModesKHR(physical, surface, &count, modes.data());

    if (enumerated != VK_SUCCESS && enumerated != VK_INCOMPLETE) {
        return false;
    }

    for (auto i = std::uint32_t{}; i < count && i < kMaxPresentModes; ++i) {
        if (modes[i] == VK_PRESENT_MODE_FIFO_KHR) {
            return true;
        }
    }

    return false;
}

bool query_extent(
    VkPhysicalDevice physical,
    VkSurfaceKHR surface,
    std::size_t width,
    std::size_t height,
    VkExtent2D& extent
) noexcept {
    auto caps = VkSurfaceCapabilitiesKHR{};

    if (vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical, surface, &caps) != VK_SUCCESS) {
        return false;
    }

    if (caps.currentExtent.width != 0xFFFFFFFFU) {
        if (caps.currentExtent.width != static_cast<std::uint32_t>(width) ||
            caps.currentExtent.height != static_cast<std::uint32_t>(height)) {
            return false;
        }

        extent = caps.currentExtent;

        return true;
    }

    const auto clamp = [](std::uint32_t value, std::uint32_t low, std::uint32_t high) noexcept {
        if (value < low) {
            return low;
        }

        if (value > high) {
            return high;
        }

        return value;
    };

    extent = VkExtent2D{
        .width = clamp(
            static_cast<std::uint32_t>(width),
            caps.minImageExtent.width,
            caps.maxImageExtent.width
        ),
        .height = clamp(
            static_cast<std::uint32_t>(height),
            caps.minImageExtent.height,
            caps.maxImageExtent.height
        ),
    };

    return extent.width == static_cast<std::uint32_t>(width) &&
        extent.height == static_cast<std::uint32_t>(height);
}

}

bool create_swapchain(
    PresenterState& state,
    std::size_t width,
    std::size_t height,
    VkSwapchainKHR old_swapchain
) noexcept {
    auto* context = state.context;

    auto caps = VkSurfaceCapabilitiesKHR{};

    if (vkGetPhysicalDeviceSurfaceCapabilitiesKHR(context->physical, state.surface, &caps) !=
        VK_SUCCESS) {
        return false;
    }

    if (caps.minImageCount == 0 || caps.minImageCount > kMaxPresentImages) {
        return false;
    }

    auto wanted = caps.minImageCount + 1;

    if (caps.maxImageCount != 0 && wanted > caps.maxImageCount) {
        wanted = caps.maxImageCount;
    }

    if (wanted > kMaxPresentImages) {
        wanted = kMaxPresentImages;
    }

    auto extent = VkExtent2D{};

    if (!query_extent(context->physical, state.surface, width, height, extent)) {
        return false;
    }

    auto format = VkFormat{VK_FORMAT_R8G8B8A8_UNORM};
    auto color_space = VkColorSpaceKHR{VK_COLOR_SPACE_SRGB_NONLINEAR_KHR};

    if (!pick_surface_format(context->physical, state.surface, format, color_space)) {
        return false;
    }

    if (!pick_present_fifo(context->physical, state.surface)) {
        return false;
    }

    const auto create_info = VkSwapchainCreateInfoKHR{
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .surface = state.surface,
        .minImageCount = wanted,
        .imageFormat = format,
        .imageColorSpace = color_space,
        .imageExtent = extent,
        .imageArrayLayers = 1,
        .imageUsage = VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .preTransform = caps.currentTransform,
        .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        .presentMode = VK_PRESENT_MODE_FIFO_KHR,
        .clipped = VK_TRUE,
        .oldSwapchain = old_swapchain,
    };

    if (vkCreateSwapchainKHR(context->device, &create_info, nullptr, &state.swapchain) !=
        VK_SUCCESS) {
        state.swapchain = VK_NULL_HANDLE;

        return false;
    }

    auto count = std::uint32_t{kMaxPresentImages};

    if (vkGetSwapchainImagesKHR(context->device, state.swapchain, &count, state.images) !=
        VK_SUCCESS) {
        vkDestroySwapchainKHR(context->device, state.swapchain, nullptr);

        state.swapchain = VK_NULL_HANDLE;

        return false;
    }

    if (count == 0 || count > kMaxPresentImages) {
        vkDestroySwapchainKHR(context->device, state.swapchain, nullptr);

        state.swapchain = VK_NULL_HANDLE;

        return false;
    }

    state.image_count = count;

    for (auto i = std::uint32_t{}; i < count; ++i) {
        const auto view_info = VkImageViewCreateInfo{
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image = state.images[i],
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format = format,
            .subresourceRange = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .levelCount = 1,
                .layerCount = 1,
            },
        };

        if (vkCreateImageView(context->device, &view_info, nullptr, &state.views[i]) !=
            VK_SUCCESS) {
            for (auto j = std::uint32_t{}; j < i; ++j) {
                vkDestroyImageView(context->device, state.views[j], nullptr);

                state.views[j] = VK_NULL_HANDLE;
            }

            vkDestroySwapchainKHR(context->device, state.swapchain, nullptr);

            state.swapchain = VK_NULL_HANDLE;
            state.image_count = 0;

            return false;
        }
    }

    return true;
}

void destroy_swapchain(PresenterState& state) noexcept {
    auto* context = state.context;

    if (context == nullptr || context->device == VK_NULL_HANDLE) {
        state.swapchain = VK_NULL_HANDLE;
        state.image_count = 0;

        return;
    }

    for (auto i = std::uint32_t{}; i < state.image_count; ++i) {
        if (state.views[i] != VK_NULL_HANDLE) {
            vkDestroyImageView(context->device, state.views[i], nullptr);

            state.views[i] = VK_NULL_HANDLE;
        }

        state.images[i] = VK_NULL_HANDLE;
    }

    state.image_count = 0;

    if (state.swapchain != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(context->device, state.swapchain, nullptr);

        state.swapchain = VK_NULL_HANDLE;
    }
}

bool create_readback(PresenterState& state, std::size_t width, std::size_t height) noexcept {
    auto* context = state.context;
    const auto bytes = width * height * sizeof(SoftwarePixel);

    const auto buffer_info = VkBufferCreateInfo{
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = static_cast<VkDeviceSize>(bytes),
        .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };

    if (vkCreateBuffer(context->device, &buffer_info, nullptr, &state.readback) != VK_SUCCESS) {
        return false;
    }

    auto requirements = VkMemoryRequirements{};

    vkGetBufferMemoryRequirements(context->device, state.readback, &requirements);

    auto memory_index = std::uint32_t{};
    const auto flags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

    if (!vulkan_detail::find_memory_type(
            context->physical,
            requirements.memoryTypeBits,
            flags,
            memory_index
        )) {
        vkDestroyBuffer(context->device, state.readback, nullptr);

        state.readback = VK_NULL_HANDLE;

        return false;
    }

    const auto alloc_info = VkMemoryAllocateInfo{
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = requirements.size,
        .memoryTypeIndex = memory_index,
    };

    if (vkAllocateMemory(context->device, &alloc_info, nullptr, &state.readback_memory) !=
        VK_SUCCESS) {
        vkDestroyBuffer(context->device, state.readback, nullptr);

        state.readback = VK_NULL_HANDLE;

        return false;
    }

    if (vkBindBufferMemory(context->device, state.readback, state.readback_memory, 0) !=
        VK_SUCCESS) {
        vkFreeMemory(context->device, state.readback_memory, nullptr);
        vkDestroyBuffer(context->device, state.readback, nullptr);

        state.readback_memory = VK_NULL_HANDLE;
        state.readback = VK_NULL_HANDLE;

        return false;
    }

    if (vkMapMemory(
            context->device,
            state.readback_memory,
            0,
            requirements.size,
            0,
            &state.readback_mapped
        ) != VK_SUCCESS) {
        vkFreeMemory(context->device, state.readback_memory, nullptr);
        vkDestroyBuffer(context->device, state.readback, nullptr);

        state.readback_memory = VK_NULL_HANDLE;
        state.readback = VK_NULL_HANDLE;

        return false;
    }

    return true;
}

void destroy_readback(PresenterState& state) noexcept {
    auto* context = state.context;

    if (context == nullptr || context->device == VK_NULL_HANDLE) {
        state.readback = VK_NULL_HANDLE;
        state.readback_memory = VK_NULL_HANDLE;
        state.readback_mapped = nullptr;

        return;
    }

    if (state.readback_mapped != nullptr) {
        vkUnmapMemory(context->device, state.readback_memory);

        state.readback_mapped = nullptr;
    }

    if (state.readback != VK_NULL_HANDLE) {
        vkDestroyBuffer(context->device, state.readback, nullptr);

        state.readback = VK_NULL_HANDLE;
    }

    if (state.readback_memory != VK_NULL_HANDLE) {
        vkFreeMemory(context->device, state.readback_memory, nullptr);

        state.readback_memory = VK_NULL_HANDLE;
    }
}

bool create_sync(PresenterState& state) noexcept {
    auto* context = state.context;

    const auto pool_info = VkCommandPoolCreateInfo{
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        .queueFamilyIndex = context->queue_family,
    };

    if (vkCreateCommandPool(context->device, &pool_info, nullptr, &state.pool) != VK_SUCCESS) {
        return false;
    }

    const auto buffer_info = VkCommandBufferAllocateInfo{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = state.pool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };

    if (vkAllocateCommandBuffers(context->device, &buffer_info, &state.commands) != VK_SUCCESS) {
        vkDestroyCommandPool(context->device, state.pool, nullptr);

        state.pool = VK_NULL_HANDLE;

        return false;
    }

    const auto fence_info = VkFenceCreateInfo{
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
    };

    if (vkCreateFence(context->device, &fence_info, nullptr, &state.fence) != VK_SUCCESS) {
        vkFreeCommandBuffers(context->device, state.pool, 1, &state.commands);
        vkDestroyCommandPool(context->device, state.pool, nullptr);

        state.commands = VK_NULL_HANDLE;
        state.pool = VK_NULL_HANDLE;

        return false;
    }

    return true;
}

void destroy_sync(PresenterState& state) noexcept {
    auto* context = state.context;

    if (context == nullptr || context->device == VK_NULL_HANDLE) {
        state.fence = VK_NULL_HANDLE;
        state.commands = VK_NULL_HANDLE;
        state.pool = VK_NULL_HANDLE;

        return;
    }

    if (state.fence != VK_NULL_HANDLE) {
        vkDestroyFence(context->device, state.fence, nullptr);

        state.fence = VK_NULL_HANDLE;
    }

    if (state.commands != VK_NULL_HANDLE) {
        vkFreeCommandBuffers(context->device, state.pool, 1, &state.commands);

        state.commands = VK_NULL_HANDLE;
    }

    if (state.pool != VK_NULL_HANDLE) {
        vkDestroyCommandPool(context->device, state.pool, nullptr);

        state.pool = VK_NULL_HANDLE;
    }
}

void destroy_surface(PresenterState& state) noexcept {
    auto* context = state.context;

    if (state.surface == VK_NULL_HANDLE) {
        return;
    }

    if (state.owns_surface && context != nullptr && context->instance != VK_NULL_HANDLE) {
        vkDestroySurfaceKHR(context->instance, state.surface, nullptr);
    }

    state.surface = VK_NULL_HANDLE;
    state.owns_surface = false;
}

}
