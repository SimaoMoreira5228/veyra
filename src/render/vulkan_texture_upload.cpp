#include "vulkan_internal.hpp"

#include "software_renderer_internal.hpp"
#include "veyra/render/glyph_atlas.hpp"

#include <vulkan/vulkan.h>

#include <cstddef>
#include <span>

namespace veyra::vulkan_detail {

VulkanRenderResult upload_textures(
    vulkan_detail::ContextState& state,
    vulkan_detail::PipelineState& stored,
    std::span<const DisplayCommand> commands,
    std::size_t& uploads,
    std::size_t& upload_bytes
) noexcept {
    uploads = 0;
    upload_bytes = 0;

    for (const auto& command : commands) {
        if (const auto* drawn = std::get_if<DrawImage>(&command)) {
            if (drawn->image == nullptr || !render_detail::valid_image(*drawn->image) ||
                !render_detail::valid_image_placement(*drawn)) {
                return VulkanRenderResult::invalid_input;
            }

            if (drawn->opacity == 0) {
                continue;
            }

            if (drawn->source.width <= 0.0F || drawn->source.height <= 0.0F) {
                continue;
            }

            auto descriptor = VkDescriptorSet{VK_NULL_HANDLE};
            auto bytes = std::size_t{};

            const auto resolved = vulkan_detail::resolve_image(
                state,
                stored,
                *drawn->image,
                drawn->sampling,
                descriptor,
                bytes
            );

            if (resolved == vulkan_detail::ImageResolveResult::too_large) {
                return VulkanRenderResult::insufficient_capacity;
            }

            if (resolved != vulkan_detail::ImageResolveResult::ready) {
                return VulkanRenderResult::device_error;
            }

            if (bytes > 0) {
                ++uploads;
                upload_bytes += bytes;
            }

            continue;
        }

        const auto* masked = std::get_if<DrawMask>(&command);
        const auto* glyphed = std::get_if<DrawGlyph>(&command);

        if (masked == nullptr && glyphed == nullptr) {
            continue;
        }

        if (masked != nullptr) {
            if (masked->mask == nullptr || masked->mask->stride < masked->mask->width ||
                masked->mask->coverage.size() < masked->mask->height * masked->mask->stride) {
                return VulkanRenderResult::invalid_input;
            }

            if (masked->color.alpha == 0 || masked->mask->width == 0 || masked->mask->height == 0) {
                continue;
            }

            auto descriptor = VkDescriptorSet{VK_NULL_HANDLE};
            auto bytes = std::size_t{};

            const auto resolved =
                vulkan_detail::resolve_mask(state, stored, *masked->mask, descriptor, bytes);

            if (resolved == vulkan_detail::MaskResolveResult::too_large) {
                return VulkanRenderResult::insufficient_capacity;
            }

            if (resolved != vulkan_detail::MaskResolveResult::ready) {
                return VulkanRenderResult::device_error;
            }

            if (bytes > 0) {
                ++uploads;
                upload_bytes += bytes;
            }

            continue;
        }

        if (glyphed->atlas == nullptr) {
            return VulkanRenderResult::invalid_input;
        }

        const auto& atlas_view = glyphed->atlas->view();

        if (atlas_view.stride < atlas_view.width ||
            atlas_view.coverage.size() < atlas_view.height * atlas_view.stride) {
            return VulkanRenderResult::invalid_input;
        }

        if (glyphed->color.alpha == 0 || glyphed->width == 0 || glyphed->height == 0) {
            continue;
        }

        auto glyph_descriptor = VkDescriptorSet{VK_NULL_HANDLE};
        auto glyph_bytes = std::size_t{};

        const auto glyph_resolved =
            vulkan_detail::resolve_mask(state, stored, atlas_view, glyph_descriptor, glyph_bytes);

        if (glyph_resolved == vulkan_detail::MaskResolveResult::too_large) {
            return VulkanRenderResult::insufficient_capacity;
        }

        if (glyph_resolved != vulkan_detail::MaskResolveResult::ready) {
            return VulkanRenderResult::device_error;
        }

        if (glyph_bytes > 0) {
            ++uploads;
            upload_bytes += glyph_bytes;
        }
    }

    return VulkanRenderResult::complete;
}

}
