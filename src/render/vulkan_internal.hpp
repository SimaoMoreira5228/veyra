#ifndef VEYRA_SRC_RENDER_VULKAN_INTERNAL_HPP
#define VEYRA_SRC_RENDER_VULKAN_INTERNAL_HPP

#include "veyra/render/vulkan_context.hpp"
#include "veyra/render/vulkan_rect_pipeline.hpp"

#include <vulkan/vulkan.h>

#include <cstddef>
#include <cstdint>

namespace veyra::vulkan_detail {

constexpr std::size_t kMaxPhysicalDevices = 8;
constexpr std::size_t kMaxQueueFamilies = 16;
constexpr std::size_t kMaxLayers = 16;

struct TargetResources {
    VkImage image{VK_NULL_HANDLE};
    VkDeviceMemory memory{VK_NULL_HANDLE};
    VkImageView view{VK_NULL_HANDLE};
    VkBuffer staging{VK_NULL_HANDLE};
    VkDeviceMemory staging_memory{VK_NULL_HANDLE};
    void* staging_mapped{nullptr};
    std::size_t staging_bytes{};
};

struct ContextState {
    VkInstance instance{VK_NULL_HANDLE};
    VkPhysicalDevice physical{VK_NULL_HANDLE};
    VkDevice device{VK_NULL_HANDLE};
    VkQueue queue{VK_NULL_HANDLE};
    std::uint32_t queue_family{};
    VkCommandPool pool{VK_NULL_HANDLE};
    VkCommandBuffer commands{VK_NULL_HANDLE};
    VkFence fence{VK_NULL_HANDLE};
    VkRenderPass shared_pass{VK_NULL_HANDLE};
    VkFramebuffer framebuffer{VK_NULL_HANDLE};
    TargetResources target{};
    VulkanDeviceInfo info{};
    std::size_t width{};
    std::size_t height{};
};

struct RectInstance {
    float min_x{};
    float min_y{};
    float max_x{};
    float max_y{};
    float red{};
    float green{};
    float blue{};
    float alpha{};
};

struct PipelineState {
    VkRenderPass pass{VK_NULL_HANDLE};
    VkPipelineLayout layout{VK_NULL_HANDLE};
    VkPipeline pipeline{VK_NULL_HANDLE};
    VkBuffer instances{VK_NULL_HANDLE};
    VkDeviceMemory instance_memory{VK_NULL_HANDLE};
    void* instance_mapped{nullptr};
};

ContextState* state_of(VulkanContext& context) noexcept;

PipelineState* pipeline_of(VulkanRectPipeline& pipeline) noexcept;

bool find_memory_type(
    VkPhysicalDevice physical,
    std::uint32_t type_bits,
    VkMemoryPropertyFlags required,
    std::uint32_t& index
) noexcept;

bool create_target(ContextState& state, std::size_t width, std::size_t height) noexcept;

void destroy_target(ContextState& state) noexcept;

bool create_framebuffer(ContextState& state) noexcept;

void destroy_framebuffer(ContextState& state) noexcept;

void copy_device_name(char (&output)[256], const char* source) noexcept;

void copy_icd_hint(char (&output)[64]) noexcept;

}
#endif
