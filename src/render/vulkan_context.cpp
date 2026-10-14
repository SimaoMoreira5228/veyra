#include "veyra/render/vulkan_context.hpp"

#include "vulkan_internal.hpp"

#include <vulkan/vulkan.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <new>

namespace veyra {
namespace {

bool has_validation_layer() noexcept {
    VkLayerProperties layers[vulkan_detail::kMaxLayers]{};
    auto count = std::uint32_t{static_cast<std::uint32_t>(sizeof(layers) / sizeof(layers[0]))};

    if (vkEnumerateInstanceLayerProperties(&count, layers) != VK_SUCCESS) {
        return false;
    }

    for (auto i = std::uint32_t{}; i < count; ++i) {
        auto match = true;
        const char* want = "VK_LAYER_KHRONOS_validation";
        const char* have = layers[i].layerName;

        for (auto j = std::size_t{}; want[j] != '\0'; ++j) {
            if (have[j] != want[j]) {
                match = false;

                break;
            }
        }

        if (match && have[27] == '\0') {
            return true;
        }
    }

    return false;
}

VulkanContextResult create_instance(vulkan_detail::ContextState& state, bool use_layers) noexcept {
    const auto app_info = VkApplicationInfo{
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = "veyra",
        .applicationVersion = VK_MAKE_VERSION(0, 1, 0),
        .pEngineName = "veyra",
        .engineVersion = VK_MAKE_VERSION(0, 1, 0),
        .apiVersion = VK_API_VERSION_1_0,
    };

    const char* layer_names[] = {"VK_LAYER_KHRONOS_validation"};

    const auto instance_info = VkInstanceCreateInfo{
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pApplicationInfo = &app_info,
        .enabledLayerCount = use_layers ? 1U : 0U,
        .ppEnabledLayerNames = use_layers ? layer_names : nullptr,
    };

    if (vkCreateInstance(&instance_info, nullptr, &state.instance) != VK_SUCCESS) {
        state.instance = VK_NULL_HANDLE;

        return VulkanContextResult::creation_failed;
    }

    state.info.validation_layers = use_layers;

    return VulkanContextResult::ready;
}

bool pick_physical_device(vulkan_detail::ContextState& state) noexcept {
    VkPhysicalDevice devices[vulkan_detail::kMaxPhysicalDevices]{};
    auto count = std::uint32_t{static_cast<std::uint32_t>(sizeof(devices) / sizeof(devices[0]))};

    if (vkEnumeratePhysicalDevices(state.instance, &count, nullptr) != VK_SUCCESS) {
        return false;
    }

    if (count == 0) {
        return false;
    }

    if (count > vulkan_detail::kMaxPhysicalDevices) {
        count = vulkan_detail::kMaxPhysicalDevices;
    }

    if (vkEnumeratePhysicalDevices(state.instance, &count, devices) != VK_SUCCESS) {
        return false;
    }

    state.physical = devices[0];

    for (auto i = std::uint32_t{}; i < count; ++i) {
        auto properties = VkPhysicalDeviceProperties{};

        vkGetPhysicalDeviceProperties(devices[i], &properties);

        if (properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
            state.physical = devices[i];

            break;
        }
    }

    auto properties = VkPhysicalDeviceProperties{};

    vkGetPhysicalDeviceProperties(state.physical, &properties);

    vulkan_detail::copy_device_name(state.info.device_name, properties.deviceName);

    vulkan_detail::copy_icd_hint(state.info.icd_hint);

    state.info.api_version = properties.apiVersion;
    state.info.driver_version = properties.driverVersion;
    state.info.vendor_id = properties.vendorID;
    state.info.device_id = properties.deviceID;
    state.info.discrete = properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU;

    return true;
}

bool pick_queue_family(vulkan_detail::ContextState& state) noexcept {
    VkQueueFamilyProperties families[vulkan_detail::kMaxQueueFamilies]{};
    auto count = std::uint32_t{static_cast<std::uint32_t>(sizeof(families) / sizeof(families[0]))};

    vkGetPhysicalDeviceQueueFamilyProperties(state.physical, &count, nullptr);

    if (count == 0) {
        return false;
    }

    if (count > vulkan_detail::kMaxQueueFamilies) {
        count = vulkan_detail::kMaxQueueFamilies;
    }

    vkGetPhysicalDeviceQueueFamilyProperties(state.physical, &count, families);

    for (auto i = std::uint32_t{}; i < count; ++i) {
        if ((families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0U) {
            state.queue_family = i;

            return true;
        }
    }

    return false;
}

bool create_device(vulkan_detail::ContextState& state) noexcept {
    const auto priority = 1.0F;

    const auto queue_info = VkDeviceQueueCreateInfo{
        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = state.queue_family,
        .queueCount = 1,
        .pQueuePriorities = &priority,
    };

    const auto device_info = VkDeviceCreateInfo{
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .queueCreateInfoCount = 1,
        .pQueueCreateInfos = &queue_info,
    };

    if (vkCreateDevice(state.physical, &device_info, nullptr, &state.device) != VK_SUCCESS) {
        state.device = VK_NULL_HANDLE;

        return false;
    }

    vkGetDeviceQueue(state.device, state.queue_family, 0, &state.queue);

    return true;
}

bool create_commands(vulkan_detail::ContextState& state) noexcept {
    const auto pool_info = VkCommandPoolCreateInfo{
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        .queueFamilyIndex = state.queue_family,
    };

    if (vkCreateCommandPool(state.device, &pool_info, nullptr, &state.pool) != VK_SUCCESS) {
        return false;
    }

    const auto buffer_info = VkCommandBufferAllocateInfo{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = state.pool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };

    if (vkAllocateCommandBuffers(state.device, &buffer_info, &state.commands) != VK_SUCCESS) {
        vkDestroyCommandPool(state.device, state.pool, nullptr);

        state.pool = VK_NULL_HANDLE;

        return false;
    }

    const auto fence_info = VkFenceCreateInfo{
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
    };

    if (vkCreateFence(state.device, &fence_info, nullptr, &state.fence) != VK_SUCCESS) {
        vkFreeCommandBuffers(state.device, state.pool, 1, &state.commands);
        vkDestroyCommandPool(state.device, state.pool, nullptr);

        state.commands = VK_NULL_HANDLE;
        state.pool = VK_NULL_HANDLE;

        return false;
    }

    auto properties = VkPhysicalDeviceProperties{};

    vkGetPhysicalDeviceProperties(state.physical, &properties);

    const auto valid_bits = [&] {
        auto count = std::uint32_t{};

        vkGetPhysicalDeviceQueueFamilyProperties(state.physical, &count, nullptr);

        auto families = std::array<VkQueueFamilyProperties, 16>{};

        if (count == 0 || count > static_cast<std::uint32_t>(families.size()) ||
            state.queue_family >= count) {
            return std::uint32_t{};
        }

        vkGetPhysicalDeviceQueueFamilyProperties(state.physical, &count, families.data());

        return families[state.queue_family].timestampValidBits;
    }();

    if (valid_bits == 0) {
        return true;
    }

    state.timestamp_period = properties.limits.timestampPeriod;
    state.timestamp_mask =
        valid_bits >= 64 ? ~0ULL : (valid_bits == 0 ? 0ULL : ((1ULL << valid_bits) - 1ULL));

    const auto query_info = VkQueryPoolCreateInfo{
        .sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,
        .queryType = VK_QUERY_TYPE_TIMESTAMP,
        .queryCount = 2,
    };

    if (vkCreateQueryPool(state.device, &query_info, nullptr, &state.timestamp_pool) !=
        VK_SUCCESS) {
        state.timestamp_pool = VK_NULL_HANDLE;
        state.timestamp_period = 0.0F;
    }

    return true;
}

void destroy_commands(vulkan_detail::ContextState& state) noexcept {
    if (state.timestamp_pool != VK_NULL_HANDLE) {
        vkDestroyQueryPool(state.device, state.timestamp_pool, nullptr);

        state.timestamp_pool = VK_NULL_HANDLE;
    }

    if (state.fence != VK_NULL_HANDLE) {
        vkDestroyFence(state.device, state.fence, nullptr);

        state.fence = VK_NULL_HANDLE;
    }

    if (state.commands != VK_NULL_HANDLE) {
        vkFreeCommandBuffers(state.device, state.pool, 1, &state.commands);

        state.commands = VK_NULL_HANDLE;
    }

    if (state.pool != VK_NULL_HANDLE) {
        vkDestroyCommandPool(state.device, state.pool, nullptr);

        state.pool = VK_NULL_HANDLE;
    }
}

}

VulkanContextResult
create_vulkan_context(VulkanContext& context, std::size_t width, std::size_t height) noexcept {
    if (context.ready) {
        return VulkanContextResult::invalid_input;
    }

    if (width == 0 || height == 0) {
        return VulkanContextResult::invalid_input;
    }

    auto* state = new (std::nothrow) vulkan_detail::ContextState{};

    if (state == nullptr) {
        return VulkanContextResult::creation_failed;
    }

    auto result = create_instance(*state, has_validation_layer());

    if (result == VulkanContextResult::creation_failed) {
        result = create_instance(*state, false);
    }

    if (result != VulkanContextResult::ready) {
        delete state;

        return result;
    }

    if (!pick_physical_device(*state)) {
        vkDestroyInstance(state->instance, nullptr);

        delete state;

        return VulkanContextResult::unsupported;
    }

    if (!pick_queue_family(*state)) {
        vkDestroyInstance(state->instance, nullptr);

        delete state;

        return VulkanContextResult::unsupported;
    }

    if (!create_device(*state)) {
        vkDestroyInstance(state->instance, nullptr);

        delete state;

        return VulkanContextResult::creation_failed;
    }

    if (!create_commands(*state)) {
        vkDestroyDevice(state->device, nullptr);
        vkDestroyInstance(state->instance, nullptr);

        delete state;

        return VulkanContextResult::creation_failed;
    }

    state->width = width;
    state->height = height;

    if (!vulkan_detail::create_target(*state, width, height)) {
        vulkan_detail::destroy_target(*state);
        destroy_commands(*state);
        vkDestroyDevice(state->device, nullptr);
        vkDestroyInstance(state->instance, nullptr);

        delete state;

        return VulkanContextResult::creation_failed;
    }

    if (!vulkan_detail::create_pass(state->device, state->shared_pass)) {
        vulkan_detail::destroy_target(*state);
        destroy_commands(*state);
        vkDestroyDevice(state->device, nullptr);
        vkDestroyInstance(state->instance, nullptr);

        delete state;

        return VulkanContextResult::creation_failed;
    }

    if (!vulkan_detail::create_load_pass(state->device, state->shared_load_pass)) {
        vkDestroyRenderPass(state->device, state->shared_pass, nullptr);

        state->shared_pass = VK_NULL_HANDLE;

        vulkan_detail::destroy_target(*state);
        destroy_commands(*state);
        vkDestroyDevice(state->device, nullptr);
        vkDestroyInstance(state->instance, nullptr);

        delete state;

        return VulkanContextResult::creation_failed;
    }

    if (!vulkan_detail::create_framebuffer(*state)) {
        vkDestroyRenderPass(state->device, state->shared_load_pass, nullptr);

        state->shared_load_pass = VK_NULL_HANDLE;

        vkDestroyRenderPass(state->device, state->shared_pass, nullptr);

        state->shared_pass = VK_NULL_HANDLE;

        vulkan_detail::destroy_target(*state);
        destroy_commands(*state);
        vkDestroyDevice(state->device, nullptr);
        vkDestroyInstance(state->instance, nullptr);

        delete state;

        return VulkanContextResult::creation_failed;
    }

    context.instance_state = state;
    context.width = width;
    context.height = height;
    context.info = state->info;
    context.ready = true;

    return VulkanContextResult::ready;
}

void destroy_vulkan_context(VulkanContext& context) noexcept {
    auto* state = vulkan_detail::state_of(context);

    if (state == nullptr) {
        return;
    }

    vulkan_detail::destroy_framebuffer(*state);

    if (state->shared_load_pass != VK_NULL_HANDLE) {
        vkDestroyRenderPass(state->device, state->shared_load_pass, nullptr);

        state->shared_load_pass = VK_NULL_HANDLE;
    }

    if (state->shared_pass != VK_NULL_HANDLE) {
        vkDestroyRenderPass(state->device, state->shared_pass, nullptr);

        state->shared_pass = VK_NULL_HANDLE;
    }

    vulkan_detail::destroy_target(*state);
    destroy_commands(*state);

    if (state->device != VK_NULL_HANDLE) {
        vkDestroyDevice(state->device, nullptr);
    }

    if (state->instance != VK_NULL_HANDLE) {
        vkDestroyInstance(state->instance, nullptr);
    }

    delete state;

    context.instance_state = nullptr;
    context.width = 0;
    context.height = 0;
    context.ready = false;
}

VulkanContextResult
resize_vulkan_target(VulkanContext& context, std::size_t width, std::size_t height) noexcept {
    auto* state = vulkan_detail::state_of(context);

    if (state == nullptr || !context.ready) {
        return VulkanContextResult::invalid_input;
    }

    if (width == 0 || height == 0) {
        return VulkanContextResult::invalid_input;
    }

    vulkan_detail::destroy_framebuffer(*state);
    vulkan_detail::destroy_target(*state);

    state->width = width;
    state->height = height;
    state->target_valid = false;

    if (!vulkan_detail::create_target(*state, width, height)) {
        context.ready = false;

        return VulkanContextResult::creation_failed;
    }

    if (!vulkan_detail::create_framebuffer(*state)) {
        context.ready = false;

        return VulkanContextResult::creation_failed;
    }

    context.width = width;
    context.height = height;

    return VulkanContextResult::ready;
}

bool vulkan_context_ready(const VulkanContext& context) noexcept {
    return context.ready && context.instance_state != nullptr;
}

bool vulkan_context_device_lost(const VulkanContext& context) noexcept {
    const auto* state = vulkan_detail::state_of(context);

    return state != nullptr && state->device_lost;
}

}
