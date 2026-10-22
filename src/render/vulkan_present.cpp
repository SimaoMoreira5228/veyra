#include "veyra/render/vulkan_present.hpp"

#include "vulkan_present_internal.hpp"

#include <vulkan/vulkan.h>

#include <cstddef>
#include <cstdint>
#include <new>

namespace veyra {
namespace {

bool sync_context_extent(VulkanContext& context, std::size_t width, std::size_t height) noexcept {
    if (context.width == width && context.height == height) {
        return true;
    }

    return resize_vulkan_target(context, width, height) == VulkanContextResult::ready;
}

void mirror_public(VulkanPresenter& presenter, const PresenterState& state) noexcept {
    presenter.width = state.width;
    presenter.height = state.height;
    presenter.generation = state.generation;
    presenter.ready = state.swapchain != VK_NULL_HANDLE;
}

}

PresenterState* state_of(VulkanPresenter& presenter) noexcept {
    return static_cast<PresenterState*>(presenter.presenter_state);
}

const PresenterState* state_of(const VulkanPresenter& presenter) noexcept {
    return static_cast<const PresenterState*>(presenter.presenter_state);
}

void mark_device_lost(PresenterState& state) noexcept {
    state.device_lost = true;

    if (state.context != nullptr) {
        state.context->device_lost = true;
    }
}

VulkanPresentResult create_vulkan_presenter(
    VulkanContext& context,
    std::size_t width,
    std::size_t height,
    void* surface_or_null,
    VulkanPresenter& presenter
) noexcept {
    if (presenter.ready) {
        return VulkanPresentResult::invalid_input;
    }

    if (width == 0 || height == 0) {
        return VulkanPresentResult::invalid_input;
    }

    auto* target = vulkan_detail::state_of(context);

    if (target == nullptr || !context.ready || target->device_lost) {
        return VulkanPresentResult::invalid_input;
    }

    if (!target->surface_ready || !target->swapchain_ready) {
        return VulkanPresentResult::unsupported;
    }

    auto* state = new (std::nothrow) PresenterState{};

    if (state == nullptr) {
        return VulkanPresentResult::creation_failed;
    }

    state->context = target;
    state->owner = &context;

    if (surface_or_null == nullptr) {
        const auto create_headless = reinterpret_cast<PFN_vkCreateHeadlessSurfaceEXT>(
            vkGetInstanceProcAddr(target->instance, "vkCreateHeadlessSurfaceEXT")
        );

        if (create_headless == nullptr) {
            delete state;

            return VulkanPresentResult::unsupported;
        }

        const auto surface_info = VkHeadlessSurfaceCreateInfoEXT{
            .sType = VK_STRUCTURE_TYPE_HEADLESS_SURFACE_CREATE_INFO_EXT,
        };

        if (create_headless(target->instance, &surface_info, nullptr, &state->surface) !=
            VK_SUCCESS) {
            delete state;

            return VulkanPresentResult::creation_failed;
        }

        state->owns_surface = true;
    } else {
        // NOTE: the void* is a caller-created VkSurfaceKHR bit-cast through an
        // opaque handle so this header stays Vk-free; the platform owns it and
        // must outlive the presenter.
        state->surface = reinterpret_cast<VkSurfaceKHR>(surface_or_null);
        state->owns_surface = false;
    }

    auto supported = VkBool32{VK_FALSE};

    if (vkGetPhysicalDeviceSurfaceSupportKHR(
            target->physical,
            target->queue_family,
            state->surface,
            &supported
        ) != VK_SUCCESS ||
        supported == VK_FALSE) {
        destroy_surface(*state);

        delete state;

        return VulkanPresentResult::unsupported;
    }

    if (!create_swapchain(*state, width, height, VK_NULL_HANDLE)) {
        destroy_surface(*state);

        delete state;

        return VulkanPresentResult::creation_failed;
    }

    if (!create_readback(*state, width, height)) {
        destroy_swapchain(*state);
        destroy_surface(*state);

        delete state;

        return VulkanPresentResult::creation_failed;
    }

    if (!create_sync(*state)) {
        destroy_readback(*state);
        destroy_swapchain(*state);
        destroy_surface(*state);

        delete state;

        return VulkanPresentResult::creation_failed;
    }

    if (!sync_context_extent(context, width, height)) {
        destroy_sync(*state);
        destroy_readback(*state);
        destroy_swapchain(*state);
        destroy_surface(*state);

        delete state;

        return VulkanPresentResult::creation_failed;
    }

    state->width = width;
    state->height = height;

    presenter.presenter_state = state;
    mirror_public(presenter, *state);

    return VulkanPresentResult::ready;
}

void destroy_vulkan_presenter(VulkanPresenter& presenter) noexcept {
    auto* state = state_of(presenter);

    if (state == nullptr) {
        return;
    }

    destroy_sync(*state);
    destroy_readback(*state);
    destroy_swapchain(*state);
    destroy_surface(*state);

    delete state;

    presenter.presenter_state = nullptr;
    presenter.width = 0;
    presenter.height = 0;
    presenter.generation = 0;
    presenter.ready = false;
}

VulkanPresentResult acquire_vulkan_frame(
    VulkanPresenter& presenter,
    std::uint64_t timeout_ns,
    std::uint32_t& image
) noexcept {
    auto* state = state_of(presenter);

    if (state == nullptr || state->context == nullptr || !presenter.ready) {
        return VulkanPresentResult::invalid_input;
    }

    if (state->device_lost || state->context->device_lost) {
        mark_device_lost(*state);

        return VulkanPresentResult::device_lost;
    }

    if (state->swapchain == VK_NULL_HANDLE) {
        return VulkanPresentResult::invalid_input;
    }

    auto* context = state->context;
    auto acquired = std::uint32_t{};

    const auto status = vkAcquireNextImageKHR(
        context->device,
        state->swapchain,
        timeout_ns,
        VK_NULL_HANDLE,
        state->fence,
        &acquired
    );

    if (status == VK_ERROR_DEVICE_LOST) {
        mark_device_lost(*state);

        return VulkanPresentResult::device_lost;
    }

    if (status == VK_ERROR_OUT_OF_DATE_KHR) {
        image = 0;

        return VulkanPresentResult::needs_recreate;
    }

    if (status == VK_TIMEOUT || status == VK_NOT_READY) {
        return VulkanPresentResult::creation_failed;
    }

    if (status != VK_SUCCESS && status != VK_SUBOPTIMAL_KHR) {
        return VulkanPresentResult::creation_failed;
    }

    const auto waited =
        vkWaitForFences(context->device, 1, &state->fence, VK_TRUE, kPresentFenceTimeoutNs);

    if (waited == VK_ERROR_DEVICE_LOST) {
        mark_device_lost(*state);

        return VulkanPresentResult::device_lost;
    }

    if (waited != VK_SUCCESS) {
        return VulkanPresentResult::creation_failed;
    }

    vkResetFences(context->device, 1, &state->fence);

    if (acquired >= state->image_count) {
        return VulkanPresentResult::creation_failed;
    }

    image = acquired;

    if (status == VK_SUBOPTIMAL_KHR) {
        return VulkanPresentResult::needs_recreate;
    }

    return VulkanPresentResult::ready;
}

VulkanPresentResult resize_vulkan_presenter(
    VulkanPresenter& presenter,
    std::size_t width,
    std::size_t height
) noexcept {
    auto* state = state_of(presenter);

    if (state == nullptr || state->context == nullptr) {
        return VulkanPresentResult::invalid_input;
    }

    if (width == 0 || height == 0) {
        return VulkanPresentResult::invalid_input;
    }

    if (state->device_lost || state->context->device_lost) {
        mark_device_lost(*state);

        return VulkanPresentResult::device_lost;
    }

    auto* context = state->context;
    const auto old_swapchain = state->swapchain;

    state->swapchain = VK_NULL_HANDLE;
    state->image_count = 0;

    for (auto i = std::uint32_t{}; i < kMaxPresentImages; ++i) {
        if (state->views[i] != VK_NULL_HANDLE) {
            vkDestroyImageView(context->device, state->views[i], nullptr);

            state->views[i] = VK_NULL_HANDLE;
        }

        state->images[i] = VK_NULL_HANDLE;
    }

    destroy_readback(*state);

    if (old_swapchain != VK_NULL_HANDLE) {
        if (!create_swapchain(*state, width, height, old_swapchain)) {
            vkDestroySwapchainKHR(context->device, old_swapchain, nullptr);

            presenter.ready = false;

            return VulkanPresentResult::creation_failed;
        }

        vkDestroySwapchainKHR(context->device, old_swapchain, nullptr);
    } else if (!create_swapchain(*state, width, height, VK_NULL_HANDLE)) {
        presenter.ready = false;

        return VulkanPresentResult::creation_failed;
    }

    if (!create_readback(*state, width, height)) {
        destroy_swapchain(*state);

        presenter.ready = false;

        return VulkanPresentResult::creation_failed;
    }

    if (!sync_context_extent(*state->owner, width, height)) {
        destroy_swapchain(*state);
        destroy_readback(*state);

        presenter.ready = false;

        return VulkanPresentResult::creation_failed;
    }

    state->width = width;
    state->height = height;
    state->generation += 1;

    mirror_public(presenter, *state);

    return VulkanPresentResult::ready;
}

VulkanPresentResult recover_vulkan_presenter(VulkanPresenter& presenter) noexcept {
    auto* state = state_of(presenter);

    if (state == nullptr || state->context == nullptr) {
        return VulkanPresentResult::invalid_input;
    }

    auto* context = state->context;

    if (state->device_lost || context->device_lost || !vulkan_context_ready(*state->owner)) {
        mark_device_lost(*state);

        return VulkanPresentResult::device_lost;
    }

    destroy_swapchain(*state);
    destroy_readback(*state);

    if (!create_swapchain(*state, state->width, state->height, VK_NULL_HANDLE)) {
        presenter.ready = false;

        return VulkanPresentResult::creation_failed;
    }

    if (!create_readback(*state, state->width, state->height)) {
        destroy_swapchain(*state);

        presenter.ready = false;

        return VulkanPresentResult::creation_failed;
    }

    state->generation += 1;

    mirror_public(presenter, *state);

    return VulkanPresentResult::ready;
}

bool vulkan_presenter_ready(const VulkanPresenter& presenter) noexcept {
    return presenter.ready && presenter.presenter_state != nullptr;
}

bool vulkan_presenter_device_lost(const VulkanPresenter& presenter) noexcept {
    const auto* state = state_of(presenter);

    if (state == nullptr) {
        return false;
    }

    if (state->device_lost) {
        return true;
    }

    return state->context != nullptr && state->context->device_lost;
}

std::uint64_t vulkan_presenter_generation(const VulkanPresenter& presenter) noexcept {
    const auto* state = state_of(presenter);

    if (state == nullptr) {
        return 0;
    }

    return state->generation;
}
}
