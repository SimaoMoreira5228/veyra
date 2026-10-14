#include "vulkan_internal.hpp"

#include "glyph_frag_spv.hpp"
#include "glyph_vert_spv.hpp"
#include "image_frag_spv.hpp"
#include "image_vert_spv.hpp"
#include "mask_frag_spv.hpp"
#include "mask_vert_spv.hpp"
#include "rect_frag_spv.hpp"
#include "rect_vert_spv.hpp"
#include "rounded_rect_frag_spv.hpp"
#include "rounded_rect_vert_spv.hpp"
#include "rounded_stroke_frag_spv.hpp"
#include "rounded_stroke_vert_spv.hpp"
#include "stroke_frag_spv.hpp"
#include "stroke_vert_spv.hpp"

#include <vulkan/vulkan.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

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

}

namespace vulkan_detail {

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

bool assemble_pipeline(
    VkDevice device,
    VkRenderPass pass,
    VkVertexInputBindingDescription binding,
    std::span<const VkVertexInputAttributeDescription> attributes,
    VkShaderModule vertex_module,
    VkShaderModule fragment_module,
    VkDescriptorSetLayout descriptor_layout,
    VkPipelineLayout& layout,
    VkPipeline& pipeline
) noexcept {
    const auto vertex_stage = VkPipelineShaderStageCreateInfo{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        .stage = VK_SHADER_STAGE_VERTEX_BIT,
        .module = vertex_module,
        .pName = "main",
    };

    const auto fragment_stage = VkPipelineShaderStageCreateInfo{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
        .module = fragment_module,
        .pName = "main",
    };

    const auto stages =
        std::array<VkPipelineShaderStageCreateInfo, 2>{vertex_stage, fragment_stage};

    const auto vertex_input = VkPipelineVertexInputStateCreateInfo{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount = 1,
        .pVertexBindingDescriptions = &binding,
        .vertexAttributeDescriptionCount = static_cast<std::uint32_t>(attributes.size()),
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
        .blendEnable = VK_TRUE,
        .srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
        .dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        .colorBlendOp = VK_BLEND_OP_ADD,
        .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
        .dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        .alphaBlendOp = VK_BLEND_OP_ADD,
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
        .setLayoutCount = descriptor_layout == VK_NULL_HANDLE ? 0U : 1U,
        .pSetLayouts = descriptor_layout == VK_NULL_HANDLE ? nullptr : &descriptor_layout,
        .pushConstantRangeCount = 1,
        .pPushConstantRanges = &push_range,
    };

    auto created_layout = VkPipelineLayout{VK_NULL_HANDLE};

    if (vkCreatePipelineLayout(device, &layout_info, nullptr, &created_layout) != VK_SUCCESS) {
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

        return false;
    }

    layout = created_layout;
    pipeline = created_pipeline;

    return true;
}

bool create_pipeline_with_modules(
    VkDevice device,
    VkRenderPass pass,
    const std::uint32_t* vertex_words,
    std::size_t vertex_word_count,
    const std::uint32_t* fragment_words,
    std::size_t fragment_word_count,
    VkVertexInputBindingDescription binding,
    std::span<const VkVertexInputAttributeDescription> attributes,
    VkDescriptorSetLayout descriptor_layout,
    VkPipelineLayout& layout,
    VkPipeline& pipeline
) noexcept {
    auto vertex_module = VkShaderModule{VK_NULL_HANDLE};
    auto fragment_module = VkShaderModule{VK_NULL_HANDLE};

    if (!create_shader_module(device, vertex_words, vertex_word_count, vertex_module)) {
        return false;
    }

    if (!create_shader_module(device, fragment_words, fragment_word_count, fragment_module)) {
        vkDestroyShaderModule(device, vertex_module, nullptr);

        return false;
    }

    const auto assembled = assemble_pipeline(
        device,
        pass,
        binding,
        attributes,
        vertex_module,
        fragment_module,
        descriptor_layout,
        layout,
        pipeline
    );

    vkDestroyShaderModule(device, vertex_module, nullptr);
    vkDestroyShaderModule(device, fragment_module, nullptr);

    return assembled;
}

bool create_rect_pipeline(
    VkDevice device,
    VkRenderPass pass,
    VkPipelineLayout& layout,
    VkPipeline& pipeline
) noexcept {
    const auto binding = VkVertexInputBindingDescription{
        .binding = 0,
        .stride = sizeof(RectInstance),
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

    return create_pipeline_with_modules(
        device,
        pass,
        kRectVertSpv,
        kRectVertSpvWords,
        kRectFragSpv,
        kRectFragSpvWords,
        binding,
        attributes,
        VK_NULL_HANDLE,
        layout,
        pipeline
    );
}

bool create_rounded_pipeline(
    VkDevice device,
    VkRenderPass pass,
    VkPipelineLayout& layout,
    VkPipeline& pipeline
) noexcept {
    const auto binding = VkVertexInputBindingDescription{
        .binding = 0,
        .stride = sizeof(RoundedRectInstance),
        .inputRate = VK_VERTEX_INPUT_RATE_INSTANCE,
    };

    const auto attributes = std::array<VkVertexInputAttributeDescription, 4>{
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
        VkVertexInputAttributeDescription{
            .location = 2,
            .binding = 0,
            .format = VK_FORMAT_R32G32B32A32_SFLOAT,
            .offset = 32,
        },
        VkVertexInputAttributeDescription{
            .location = 3,
            .binding = 0,
            .format = VK_FORMAT_R32_SFLOAT,
            .offset = 48,
        },
    };

    return create_pipeline_with_modules(
        device,
        pass,
        kRoundedRectVertSpv,
        kRoundedRectVertSpvWords,
        kRoundedRectFragSpv,
        kRoundedRectFragSpvWords,
        binding,
        attributes,
        VK_NULL_HANDLE,
        layout,
        pipeline
    );
}

bool create_image_pipeline(
    VkDevice device,
    VkRenderPass pass,
    VkDescriptorSetLayout descriptor_layout,
    VkPipelineLayout& layout,
    VkPipeline& pipeline
) noexcept {
    const auto binding = VkVertexInputBindingDescription{
        .binding = 0,
        .stride = sizeof(ImageInstance),
        .inputRate = VK_VERTEX_INPUT_RATE_INSTANCE,
    };

    const auto attributes = std::array<VkVertexInputAttributeDescription, 3>{
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
        VkVertexInputAttributeDescription{
            .location = 2,
            .binding = 0,
            .format = VK_FORMAT_R32_SFLOAT,
            .offset = 32,
        },
    };

    return create_pipeline_with_modules(
        device,
        pass,
        kImageVertSpv,
        kImageVertSpvWords,
        kImageFragSpv,
        kImageFragSpvWords,
        binding,
        attributes,
        descriptor_layout,
        layout,
        pipeline
    );
}

bool create_mask_pipeline(
    VkDevice device,
    VkRenderPass pass,
    VkDescriptorSetLayout descriptor_layout,
    VkPipelineLayout& layout,
    VkPipeline& pipeline
) noexcept {
    const auto binding = VkVertexInputBindingDescription{
        .binding = 0,
        .stride = sizeof(MaskInstance),
        .inputRate = VK_VERTEX_INPUT_RATE_INSTANCE,
    };

    const auto attributes = std::array<VkVertexInputAttributeDescription, 3>{
        VkVertexInputAttributeDescription{
            .location = 0,
            .binding = 0,
            .format = VK_FORMAT_R32G32B32A32_SFLOAT,
            .offset = 0,
        },
        VkVertexInputAttributeDescription{
            .location = 1,
            .binding = 0,
            .format = VK_FORMAT_R32G32_SFLOAT,
            .offset = 16,
        },
        VkVertexInputAttributeDescription{
            .location = 2,
            .binding = 0,
            .format = VK_FORMAT_R32G32B32A32_SFLOAT,
            .offset = 32,
        },
    };

    return create_pipeline_with_modules(
        device,
        pass,
        kMaskVertSpv,
        kMaskVertSpvWords,
        kMaskFragSpv,
        kMaskFragSpvWords,
        binding,
        attributes,
        descriptor_layout,
        layout,
        pipeline
    );
}

bool create_glyph_pipeline(
    VkDevice device,
    VkRenderPass pass,
    VkDescriptorSetLayout descriptor_layout,
    VkPipelineLayout& layout,
    VkPipeline& pipeline
) noexcept {
    const auto binding = VkVertexInputBindingDescription{
        .binding = 0,
        .stride = sizeof(GlyphInstance),
        .inputRate = VK_VERTEX_INPUT_RATE_INSTANCE,
    };

    const auto attributes = std::array<VkVertexInputAttributeDescription, 3>{
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
        VkVertexInputAttributeDescription{
            .location = 2,
            .binding = 0,
            .format = VK_FORMAT_R32G32B32A32_SFLOAT,
            .offset = 32,
        },
    };

    return create_pipeline_with_modules(
        device,
        pass,
        kGlyphVertSpv,
        kGlyphVertSpvWords,
        kGlyphFragSpv,
        kGlyphFragSpvWords,
        binding,
        attributes,
        descriptor_layout,
        layout,
        pipeline
    );
}
bool create_stroke_pipeline(
    VkDevice device,
    VkRenderPass pass,
    VkPipelineLayout& layout,
    VkPipeline& pipeline
) noexcept {
    const auto binding = VkVertexInputBindingDescription{
        .binding = 0,
        .stride = sizeof(StrokeInstance),
        .inputRate = VK_VERTEX_INPUT_RATE_INSTANCE,
    };

    const auto attributes = std::array<VkVertexInputAttributeDescription, 4>{
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
        VkVertexInputAttributeDescription{
            .location = 2,
            .binding = 0,
            .format = VK_FORMAT_R32G32B32A32_SFLOAT,
            .offset = 32,
        },
        VkVertexInputAttributeDescription{
            .location = 3,
            .binding = 0,
            .format = VK_FORMAT_R32_SFLOAT,
            .offset = 48,
        },
    };

    return create_pipeline_with_modules(
        device,
        pass,
        kStrokeVertSpv,
        kStrokeVertSpvWords,
        kStrokeFragSpv,
        kStrokeFragSpvWords,
        binding,
        attributes,
        VK_NULL_HANDLE,
        layout,
        pipeline
    );
}

bool create_rounded_stroke_pipeline(
    VkDevice device,
    VkRenderPass pass,
    VkPipelineLayout& layout,
    VkPipeline& pipeline
) noexcept {
    const auto binding = VkVertexInputBindingDescription{
        .binding = 0,
        .stride = sizeof(RoundedStrokeInstance),
        .inputRate = VK_VERTEX_INPUT_RATE_INSTANCE,
    };

    const auto attributes = std::array<VkVertexInputAttributeDescription, 5>{
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
        VkVertexInputAttributeDescription{
            .location = 2,
            .binding = 0,
            .format = VK_FORMAT_R32G32B32A32_SFLOAT,
            .offset = 32,
        },
        VkVertexInputAttributeDescription{
            .location = 3,
            .binding = 0,
            .format = VK_FORMAT_R32_SFLOAT,
            .offset = 48,
        },
        VkVertexInputAttributeDescription{
            .location = 4,
            .binding = 0,
            .format = VK_FORMAT_R32_SFLOAT,
            .offset = 52,
        },
    };

    return create_pipeline_with_modules(
        device,
        pass,
        kRoundedStrokeVertSpv,
        kRoundedStrokeVertSpvWords,
        kRoundedStrokeFragSpv,
        kRoundedStrokeFragSpvWords,
        binding,
        attributes,
        VK_NULL_HANDLE,
        layout,
        pipeline
    );
}

}
}
