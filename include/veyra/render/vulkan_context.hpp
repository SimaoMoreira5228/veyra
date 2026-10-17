#ifndef VEYRA_RENDER_VULKAN_CONTEXT_HPP
#define VEYRA_RENDER_VULKAN_CONTEXT_HPP

#include <cstddef>
#include <cstdint>

namespace veyra {

enum class VulkanContextResult : std::uint8_t {
    ready,
    invalid_input,
    unsupported,
    creation_failed,
};

struct VulkanDeviceInfo {
    char device_name[256]{};
    char icd_hint[64]{};
    std::uint32_t api_version{};
    std::uint32_t driver_version{};
    std::uint32_t vendor_id{};
    std::uint32_t device_id{};
    bool discrete{};
    bool validation_layers{};
};

struct VulkanCapabilities {
    std::uint32_t max_image_dimension{};
    std::size_t staging_bytes{};
    std::size_t max_frame_images{};
    std::size_t max_rects_per_draw{};
    bool timestamp_valid{};
    bool device_lost{};
};

struct VulkanContext {
    void* instance_state{};
    std::size_t width{};
    std::size_t height{};
    VulkanDeviceInfo info{};
    bool ready{};
};

VulkanContextResult
create_vulkan_context(VulkanContext& context, std::size_t width, std::size_t height) noexcept;

void destroy_vulkan_context(VulkanContext& context) noexcept;

VulkanContextResult
resize_vulkan_target(VulkanContext& context, std::size_t width, std::size_t height) noexcept;

bool vulkan_context_ready(const VulkanContext& context) noexcept;

bool vulkan_context_device_lost(const VulkanContext& context) noexcept;

VulkanContextResult
query_vulkan_capabilities(const VulkanContext& context, VulkanCapabilities& capabilities) noexcept;

}

#endif
