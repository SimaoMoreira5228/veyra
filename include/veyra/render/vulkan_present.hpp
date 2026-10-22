#ifndef VEYRA_RENDER_VULKAN_PRESENT_HPP
#define VEYRA_RENDER_VULKAN_PRESENT_HPP

#include "veyra/render/software_renderer.hpp"
#include "veyra/render/vulkan_context.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

namespace veyra {

enum class VulkanPresentResult : std::uint8_t {
    ready,
    invalid_input,
    unsupported,
    creation_failed,
    needs_recreate,
    device_lost,
};

struct VulkanPresentStats {
    std::uint64_t gpu_ns{};
    bool gpu_timed{};
    std::uint32_t image{};
};

struct VulkanPresenter {
    void* presenter_state{};
    std::size_t width{};
    std::size_t height{};
    std::uint64_t generation{};
    bool ready{};
};

VulkanPresentResult create_vulkan_presenter(
    VulkanContext& context,
    std::size_t width,
    std::size_t height,
    void* surface_or_null,
    VulkanPresenter& presenter
) noexcept;

void destroy_vulkan_presenter(VulkanPresenter& presenter) noexcept;

VulkanPresentResult acquire_vulkan_frame(
    VulkanPresenter& presenter,
    std::uint64_t timeout_ns,
    std::uint32_t& image
) noexcept;

VulkanPresentResult present_vulkan_frame(
    VulkanPresenter& presenter,
    std::uint32_t image,
    VulkanPresentStats* stats
) noexcept;

VulkanPresentResult read_vulkan_presenter_pixels(
    const VulkanPresenter& presenter,
    std::span<SoftwarePixel> output
) noexcept;

VulkanPresentResult
resize_vulkan_presenter(VulkanPresenter& presenter, std::size_t width, std::size_t height) noexcept;

VulkanPresentResult recover_vulkan_presenter(VulkanPresenter& presenter) noexcept;

bool vulkan_presenter_ready(const VulkanPresenter& presenter) noexcept;

bool vulkan_presenter_device_lost(const VulkanPresenter& presenter) noexcept;

std::uint64_t vulkan_presenter_generation(const VulkanPresenter& presenter) noexcept;

}

#endif
