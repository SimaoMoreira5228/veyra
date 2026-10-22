#ifndef VEYRA_SRC_RENDER_VULKAN_PRESENT_INTERNAL_HPP
#define VEYRA_SRC_RENDER_VULKAN_PRESENT_INTERNAL_HPP

#include "veyra/render/vulkan_present.hpp"
#include "vulkan_internal.hpp"

#include <vulkan/vulkan.h>

#include <cstddef>
#include <cstdint>

namespace veyra {

constexpr std::uint32_t kMaxPresentImages = 8;
constexpr std::uint32_t kMaxSurfaceFormats = 32;
constexpr std::uint32_t kMaxPresentModes = 8;
constexpr std::uint64_t kPresentFenceTimeoutNs = 10'000'000'000ULL;
constexpr std::uint32_t kOpaqueBlackDword = 0xFF000000U;

struct PresenterState {
    vulkan_detail::ContextState* context{};
    VulkanContext* owner{};
    VkSurfaceKHR surface{VK_NULL_HANDLE};
    bool owns_surface{};
    VkSwapchainKHR swapchain{VK_NULL_HANDLE};
    VkImage images[kMaxPresentImages]{};
    VkImageView views[kMaxPresentImages]{};
    std::uint32_t image_count{};
    VkCommandPool pool{VK_NULL_HANDLE};
    VkCommandBuffer commands{VK_NULL_HANDLE};
    VkFence fence{VK_NULL_HANDLE};
    VkBuffer readback{VK_NULL_HANDLE};
    VkDeviceMemory readback_memory{VK_NULL_HANDLE};
    void* readback_mapped{};
    std::uint64_t generation{1};
    std::size_t width{};
    std::size_t height{};
    bool device_lost{};
};

PresenterState* state_of(VulkanPresenter& presenter) noexcept;
const PresenterState* state_of(const VulkanPresenter& presenter) noexcept;

void mark_device_lost(PresenterState& state) noexcept;

bool create_swapchain(
    PresenterState& state,
    std::size_t width,
    std::size_t height,
    VkSwapchainKHR old_swapchain
) noexcept;

void destroy_swapchain(PresenterState& state) noexcept;

bool create_readback(PresenterState& state, std::size_t width, std::size_t height) noexcept;

void destroy_readback(PresenterState& state) noexcept;

bool create_sync(PresenterState& state) noexcept;

void destroy_sync(PresenterState& state) noexcept;

void destroy_surface(PresenterState& state) noexcept;

}

#endif
