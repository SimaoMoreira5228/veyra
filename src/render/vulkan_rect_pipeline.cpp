#include "veyra/render/vulkan_rect_pipeline.hpp"

#include "software_renderer_internal.hpp"
#include "vulkan_internal.hpp"

#include "rect_frag_spv.hpp"
#include "rect_vert_spv.hpp"

#include <vulkan/vulkan.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <new>
#include <variant>

namespace veyra {
namespace {

bool create_shader_module(
    VkDevice device,
    const std::uint32_t* code,
    std::size_t word_count,
    VkShaderModule& module
) noexcept {
    const auto module_info = VkShaderModuleCreateInfo{
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = word_count * sizeof(std::uint32_t),
        .pCode = code,
    };

    if (vkCreateShaderModule(device, &module_info, nullptr, &module) != VK_SUCCESS) {
        module = VK_NULL_HANDLE;

        return false;
    }

    return true;
}

bool create_pass(VkDevice device, VkRenderPass& pass) noexcept {
    const auto color_ref = VkAttachmentReference{
        .attachment = 0,
        .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
    };

    const auto subpass = VkSubpassDescription{
        .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
        .colorAttachmentCount = 1,
        .pColorAttachments = &color_ref,
    };

    const auto attachment = VkAttachmentDescription{
        .format = VK_FORMAT_R8G8B8A8_UNORM,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
    };

    const auto pass_info = VkRenderPassCreateInfo{
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
        .attachmentCount = 1,
        .pAttachments = &attachment,
        .subpassCount = 1,
        .pSubpasses = &subpass,
    };

    if (vkCreateRenderPass(device, &pass_info, nullptr, &pass) != VK_SUCCESS) {
        pass = VK_NULL_HANDLE;

        return false;
    }

    return true;
}

bool create_pipeline(
    VkDevice device,
    VkRenderPass pass,
    VkPipelineLayout& layout,
    VkPipeline& pipeline
) noexcept {
    auto vertex_module = VkShaderModule{VK_NULL_HANDLE};
    auto fragment_module = VkShaderModule{VK_NULL_HANDLE};

    if (!create_shader_module(
            device,
            vulkan_detail::kRectVertSpv,
            vulkan_detail::kRectVertSpvWords,
            vertex_module
        )) {
        return false;
    }

    const auto vertex_stage = VkPipelineShaderStageCreateInfo{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        .stage = VK_SHADER_STAGE_VERTEX_BIT,
        .module = vertex_module,
        .pName = "main",
    };

    if (!create_shader_module(
            device,
            vulkan_detail::kRectFragSpv,
            vulkan_detail::kRectFragSpvWords,
            fragment_module
        )) {
        vkDestroyShaderModule(device, vertex_module, nullptr);

        return false;
    }

    const auto fragment_stage = VkPipelineShaderStageCreateInfo{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
        .module = fragment_module,
        .pName = "main",
    };

    const auto stages =
        std::array<VkPipelineShaderStageCreateInfo, 2>{vertex_stage, fragment_stage};

    const auto binding = VkVertexInputBindingDescription{
        .binding = 0,
        .stride = sizeof(vulkan_detail::RectInstance),
        .inputRate = VK_VERTEX_INPUT_RATE_INSTANCE,
    };

    const auto attributes = std::array<VkVertexInputAttributeDescription, 2>{
        VkVertexInputAttributeDescription{
            .location = 0,
            .binding = 0,
            .format = VK_FORMAT_R32G32B32A32_SFLOAT,
            .offset = 0,
        },
        VkVertexInputAttributeDescription{
            .location = 1,
            .binding = 0,
            .format = VK_FORMAT_R32G32B32A32_SFLOAT,
            .offset = 16,
        },
    };

    const auto vertex_input = VkPipelineVertexInputStateCreateInfo{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount = 1,
        .pVertexBindingDescriptions = &binding,
        .vertexAttributeDescriptionCount = 2,
        .pVertexAttributeDescriptions = attributes.data(),
    };

    const auto assembly = VkPipelineInputAssemblyStateCreateInfo{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
    };

    const auto viewport_state = VkPipelineViewportStateCreateInfo{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1,
        .scissorCount = 1,
    };

    const auto rasterizer = VkPipelineRasterizationStateCreateInfo{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .polygonMode = VK_POLYGON_MODE_FILL,
        .cullMode = VK_CULL_MODE_NONE,
        .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
        .lineWidth = 1.0F,
    };

    const auto sampling = VkPipelineMultisampleStateCreateInfo{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
    };

    const auto blend_attachment = VkPipelineColorBlendAttachmentState{
        .blendEnable = VK_FALSE,
        .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
            VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
    };

    const auto blending = VkPipelineColorBlendStateCreateInfo{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .attachmentCount = 1,
        .pAttachments = &blend_attachment,
    };

    const auto dynamic_states =
        std::array<VkDynamicState, 2>{VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};

    const auto dynamic = VkPipelineDynamicStateCreateInfo{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .dynamicStateCount = 2,
        .pDynamicStates = dynamic_states.data(),
    };

    const auto push_range = VkPushConstantRange{
        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
        .offset = 0,
        .size = 8,
    };

    const auto layout_info = VkPipelineLayoutCreateInfo{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .pushConstantRangeCount = 1,
        .pPushConstantRanges = &push_range,
    };

    auto created_layout = VkPipelineLayout{VK_NULL_HANDLE};

    if (vkCreatePipelineLayout(device, &layout_info, nullptr, &created_layout) != VK_SUCCESS) {
        vkDestroyShaderModule(device, vertex_module, nullptr);
        vkDestroyShaderModule(device, fragment_module, nullptr);

        return false;
    }

    const auto pipeline_info = VkGraphicsPipelineCreateInfo{
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .stageCount = 2,
        .pStages = stages.data(),
        .pVertexInputState = &vertex_input,
        .pInputAssemblyState = &assembly,
        .pViewportState = &viewport_state,
        .pRasterizationState = &rasterizer,
        .pMultisampleState = &sampling,
        .pColorBlendState = &blending,
        .pDynamicState = &dynamic,
        .layout = created_layout,
        .renderPass = pass,
        .subpass = 0,
    };

    auto created_pipeline = VkPipeline{VK_NULL_HANDLE};

    if (vkCreateGraphicsPipelines(
            device,
            VK_NULL_HANDLE,
            1,
            &pipeline_info,
            nullptr,
            &created_pipeline
        ) != VK_SUCCESS) {
        vkDestroyPipelineLayout(device, created_layout, nullptr);
        vkDestroyShaderModule(device, vertex_module, nullptr);
        vkDestroyShaderModule(device, fragment_module, nullptr);

        return false;
    }

    vkDestroyShaderModule(device, vertex_module, nullptr);
    vkDestroyShaderModule(device, fragment_module, nullptr);

    layout = created_layout;
    pipeline = created_pipeline;

    return true;
}

bool create_instance_buffer(
    vulkan_detail::ContextState& state,
    vulkan_detail::PipelineState& pipeline
) noexcept {
    constexpr auto buffer_bytes = kVulkanMaxRects * sizeof(vulkan_detail::RectInstance);

    const auto buffer_info = VkBufferCreateInfo{
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = buffer_bytes,
        .usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };

    if (vkCreateBuffer(state.device, &buffer_info, nullptr, &pipeline.instances) != VK_SUCCESS) {
        return false;
    }

    auto requirements = VkMemoryRequirements{};

    vkGetBufferMemoryRequirements(state.device, pipeline.instances, &requirements);

    auto memory_index = std::uint32_t{};

    const auto flags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

    if (!vulkan_detail::find_memory_type(
            state.physical,
            requirements.memoryTypeBits,
            flags,
            memory_index
        )) {
        vkDestroyBuffer(state.device, pipeline.instances, nullptr);

        pipeline.instances = VK_NULL_HANDLE;

        return false;
    }

    const auto alloc_info = VkMemoryAllocateInfo{
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = requirements.size,
        .memoryTypeIndex = memory_index,
    };

    if (vkAllocateMemory(state.device, &alloc_info, nullptr, &pipeline.instance_memory) !=
        VK_SUCCESS) {
        vkDestroyBuffer(state.device, pipeline.instances, nullptr);

        pipeline.instances = VK_NULL_HANDLE;

        return false;
    }

    if (vkBindBufferMemory(state.device, pipeline.instances, pipeline.instance_memory, 0) !=
        VK_SUCCESS) {
        vkFreeMemory(state.device, pipeline.instance_memory, nullptr);
        vkDestroyBuffer(state.device, pipeline.instances, nullptr);

        pipeline.instance_memory = VK_NULL_HANDLE;
        pipeline.instances = VK_NULL_HANDLE;

        return false;
    }

    if (vkMapMemory(
            state.device,
            pipeline.instance_memory,
            0,
            requirements.size,
            0,
            &pipeline.instance_mapped
        ) != VK_SUCCESS) {
        vkFreeMemory(state.device, pipeline.instance_memory, nullptr);
        vkDestroyBuffer(state.device, pipeline.instances, nullptr);

        pipeline.instance_memory = VK_NULL_HANDLE;
        pipeline.instances = VK_NULL_HANDLE;

        return false;
    }

    return true;
}

void destroy_pipeline_objects(
    vulkan_detail::ContextState& state,
    vulkan_detail::PipelineState& pipeline
) noexcept {
    if (pipeline.instance_mapped != nullptr) {
        vkUnmapMemory(state.device, pipeline.instance_memory);

        pipeline.instance_mapped = nullptr;
    }

    if (pipeline.instances != VK_NULL_HANDLE) {
        vkDestroyBuffer(state.device, pipeline.instances, nullptr);

        pipeline.instances = VK_NULL_HANDLE;
    }

    if (pipeline.instance_memory != VK_NULL_HANDLE) {
        vkFreeMemory(state.device, pipeline.instance_memory, nullptr);

        pipeline.instance_memory = VK_NULL_HANDLE;
    }

    if (pipeline.pipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(state.device, pipeline.pipeline, nullptr);

        pipeline.pipeline = VK_NULL_HANDLE;
    }

    if (pipeline.layout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(state.device, pipeline.layout, nullptr);

        pipeline.layout = VK_NULL_HANDLE;
    }
}

bool record_submit(
    vulkan_detail::ContextState& state,
    vulkan_detail::PipelineState& pipeline,
    std::size_t rect_count
) noexcept {
    if (vkResetCommandBuffer(state.commands, 0) != VK_SUCCESS) {
        return false;
    }

    const auto begin_info = VkCommandBufferBeginInfo{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };

    if (vkBeginCommandBuffer(state.commands, &begin_info) != VK_SUCCESS) {
        return false;
    }

    const auto clear = VkClearValue{
        .color = {.float32 = {0.0F, 0.0F, 0.0F, 1.0F}},
    };

    const auto pass_info = VkRenderPassBeginInfo{
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
        .renderPass = pipeline.pass,
        .framebuffer = state.framebuffer,
        .renderArea =
            {
                .extent =
                    {
                        .width = static_cast<std::uint32_t>(state.width),
                        .height = static_cast<std::uint32_t>(state.height),
                    },
            },
        .clearValueCount = 1,
        .pClearValues = &clear,
    };

    vkCmdBeginRenderPass(state.commands, &pass_info, VK_SUBPASS_CONTENTS_INLINE);

    const auto viewport = VkViewport{
        .width = static_cast<float>(state.width),
        .height = static_cast<float>(state.height),
        .maxDepth = 1.0F,
    };

    vkCmdSetViewport(state.commands, 0, 1, &viewport);

    const auto scissor = VkRect2D{
        .extent = {
            .width = static_cast<std::uint32_t>(state.width),
            .height = static_cast<std::uint32_t>(state.height),
        },
    };

    vkCmdSetScissor(state.commands, 0, 1, &scissor);

    if (rect_count != 0) {
        vkCmdBindPipeline(state.commands, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.pipeline);

        const auto offset = VkDeviceSize{};

        vkCmdBindVertexBuffers(state.commands, 0, 1, &pipeline.instances, &offset);

        const auto extent =
            std::array<float, 2>{static_cast<float>(state.width), static_cast<float>(state.height)};

        vkCmdPushConstants(
            state.commands,
            pipeline.layout,
            VK_SHADER_STAGE_VERTEX_BIT,
            0,
            sizeof(extent),
            extent.data()
        );

        vkCmdDraw(state.commands, 6, static_cast<std::uint32_t>(rect_count), 0, 0);
    }

    vkCmdEndRenderPass(state.commands);

    const auto to_copy = VkImageMemoryBarrier{
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = state.target.image,
        .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .levelCount = 1,
            .layerCount = 1,
        },
    };

    vkCmdPipelineBarrier(
        state.commands,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        0,
        0,
        nullptr,
        0,
        nullptr,
        1,
        &to_copy
    );

    const auto copy = VkBufferImageCopy{
        .imageSubresource =
            {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .layerCount = 1,
            },
        .imageExtent = {
            .width = static_cast<std::uint32_t>(state.width),
            .height = static_cast<std::uint32_t>(state.height),
            .depth = 1,
        },
    };

    vkCmdCopyImageToBuffer(
        state.commands,
        state.target.image,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        state.target.staging,
        1,
        &copy
    );

    if (vkEndCommandBuffer(state.commands) != VK_SUCCESS) {
        return false;
    }

    vkResetFences(state.device, 1, &state.fence);

    const auto submit = VkSubmitInfo{
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1,
        .pCommandBuffers = &state.commands,
    };

    if (vkQueueSubmit(state.queue, 1, &submit, state.fence) != VK_SUCCESS) {
        return false;
    }

    constexpr auto timeout_ns = 10'000'000'000ULL;

    if (vkWaitForFences(state.device, 1, &state.fence, VK_TRUE, timeout_ns) != VK_SUCCESS) {
        return false;
    }

    return true;
}

}

VulkanRenderResult
create_vulkan_rect_pipeline(VulkanContext& context, VulkanRectPipeline& pipeline) noexcept {
    auto* state = vulkan_detail::state_of(context);

    if (state == nullptr || !context.ready) {
        return VulkanRenderResult::invalid_input;
    }

    if (pipeline.ready) {
        return VulkanRenderResult::invalid_input;
    }

    auto* stored = new (std::nothrow) vulkan_detail::PipelineState{};

    if (stored == nullptr) {
        return VulkanRenderResult::device_error;
    }

    if (!create_pass(state->device, stored->pass)) {
        delete stored;

        return VulkanRenderResult::device_error;
    }

    if (!create_pipeline(state->device, stored->pass, stored->layout, stored->pipeline)) {
        vkDestroyRenderPass(state->device, stored->pass, nullptr);

        delete stored;

        return VulkanRenderResult::device_error;
    }

    if (!create_instance_buffer(*state, *stored)) {
        destroy_pipeline_objects(*state, *stored);
        vkDestroyRenderPass(state->device, stored->pass, nullptr);

        delete stored;

        return VulkanRenderResult::device_error;
    }

    state->shared_pass = stored->pass;

    if (!vulkan_detail::create_framebuffer(*state)) {
        state->shared_pass = VK_NULL_HANDLE;
        destroy_pipeline_objects(*state, *stored);
        vkDestroyRenderPass(state->device, stored->pass, nullptr);

        delete stored;

        return VulkanRenderResult::device_error;
    }

    pipeline.pipeline_state = stored;
    pipeline.ready = true;

    return VulkanRenderResult::complete;
}

void destroy_vulkan_rect_pipeline(VulkanContext& context, VulkanRectPipeline& pipeline) noexcept {
    auto* stored = vulkan_detail::pipeline_of(pipeline);

    if (stored == nullptr) {
        return;
    }

    auto* state = vulkan_detail::state_of(context);

    if (state != nullptr && state->device != VK_NULL_HANDLE) {
        vulkan_detail::destroy_framebuffer(*state);

        state->shared_pass = VK_NULL_HANDLE;

        destroy_pipeline_objects(*state, *stored);
        vkDestroyRenderPass(state->device, stored->pass, nullptr);
    }

    delete stored;

    pipeline.pipeline_state = nullptr;
    pipeline.ready = false;
}

VulkanRenderResult render_vulkan_rects(
    VulkanContext& context,
    VulkanRectPipeline& pipeline,
    std::span<const DisplayCommand> commands,
    std::span<SoftwarePixel> output,
    VulkanRectStats& stats
) noexcept {
    auto* state = vulkan_detail::state_of(context);
    auto* stored = vulkan_detail::pipeline_of(pipeline);

    if (state == nullptr || stored == nullptr || !context.ready || !pipeline.ready) {
        return VulkanRenderResult::invalid_input;
    }

    if (output.size() != state->width * state->height) {
        return VulkanRenderResult::invalid_input;
    }

    if (state->framebuffer == VK_NULL_HANDLE) {
        return VulkanRenderResult::device_error;
    }

    stats = VulkanRectStats{};

    auto instances = static_cast<vulkan_detail::RectInstance*>(stored->instance_mapped);
    auto count = std::size_t{};
    auto translation = LayoutPoint{};
    auto translation_stack = std::array<LayoutPoint, 8>{};
    auto translation_depth = std::size_t{};

    for (const auto& command : commands) {
        if (const auto* push = std::get_if<PushTranslation>(&command)) {
            if (translation_depth >= translation_stack.size()) {
                return VulkanRenderResult::insufficient_capacity;
            }

            translation_stack[translation_depth] = translation;
            translation.x += push->offset.x;
            translation.y += push->offset.y;
            ++translation_depth;

            continue;
        }

        if (std::holds_alternative<PopTranslation>(command)) {
            if (translation_depth == 0) {
                return VulkanRenderResult::invalid_input;
            }

            --translation_depth;
            translation = translation_stack[translation_depth];

            continue;
        }

        const auto* fill = std::get_if<FillRect>(&command);

        if (fill == nullptr) {
            return VulkanRenderResult::unsupported_commands;
        }

        if (!render_detail::valid_rect(fill->bounds)) {
            return VulkanRenderResult::invalid_input;
        }

        if (fill->color.alpha != 255) {
            ++stats.skipped_translucent;

            continue;
        }

        const auto translated = LayoutRect{
            .x = fill->bounds.x + translation.x,
            .y = fill->bounds.y + translation.y,
            .width = fill->bounds.width,
            .height = fill->bounds.height,
        };

        const auto bounds = render_detail::pixel_bounds(translated, state->width, state->height);

        if (bounds.left >= bounds.right || bounds.top >= bounds.bottom) {
            continue;
        }

        if (count >= kVulkanMaxRects) {
            return VulkanRenderResult::insufficient_capacity;
        }

        constexpr auto channel_scale = 1.0F / 255.0F;

        instances[count] = vulkan_detail::RectInstance{
            .min_x = static_cast<float>(bounds.left),
            .min_y = static_cast<float>(bounds.top),
            .max_x = static_cast<float>(bounds.right),
            .max_y = static_cast<float>(bounds.bottom),
            .red = static_cast<float>(fill->color.red) * channel_scale,
            .green = static_cast<float>(fill->color.green) * channel_scale,
            .blue = static_cast<float>(fill->color.blue) * channel_scale,
            .alpha = 1.0F,
        };

        ++count;
    }

    if (translation_depth != 0) {
        return VulkanRenderResult::invalid_input;
    }

    if (!record_submit(*state, *stored, count)) {
        return VulkanRenderResult::device_error;
    }

    const auto byte_count = state->width * state->height * sizeof(SoftwarePixel);

    std::memcpy(output.data(), state->target.staging_mapped, byte_count);

    stats.submitted = count;

    return VulkanRenderResult::complete;
}

}
