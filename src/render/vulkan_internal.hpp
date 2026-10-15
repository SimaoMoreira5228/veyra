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
    VkQueryPool timestamp_pool{VK_NULL_HANDLE};
    float timestamp_period{};
    std::uint64_t timestamp_mask{~0ULL};
    bool target_valid{};
    bool device_lost{};
    VkRenderPass shared_pass{VK_NULL_HANDLE};
    VkRenderPass shared_load_pass{VK_NULL_HANDLE};
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

struct RoundedRectInstance {
    float min_x{};
    float min_y{};
    float max_x{};
    float max_y{};
    float red{};
    float green{};
    float blue{};
    float alpha{};
    float center_x{};
    float center_y{};
    float half_x{};
    float half_y{};
    float radius{};
    float pad_a{};
    float pad_b{};
    float pad_c{};
};

struct ImageInstance {
    float min_x{};
    float min_y{};
    float max_x{};
    float max_y{};
    float min_u{};
    float min_v{};
    float max_u{};
    float max_v{};
    float opacity{};
    float pad_a{};
    float pad_b{};
    float pad_c{};
};

struct MaskInstance {
    float min_x{};
    float min_y{};
    float max_x{};
    float max_y{};
    float start_x{};
    float start_y{};
    float pad_a{};
    float pad_b{};
    float red{};
    float green{};
    float blue{};
    float alpha{};
};

constexpr std::size_t kVulkanImageStagingBytes = 1 << 20;

struct ImageCacheEntry {
    const SoftwareImage* key{};
    std::uint64_t generation{};
    std::uint64_t frame_tag{};
    std::size_t width{};
    std::size_t height{};
    VkImage image{VK_NULL_HANDLE};
    VkDeviceMemory memory{VK_NULL_HANDLE};
    VkImageView view{VK_NULL_HANDLE};
    VkDescriptorSet nearest{VK_NULL_HANDLE};
    VkDescriptorSet linear{VK_NULL_HANDLE};
    bool occupied{};
};

struct MaskCacheEntry {
    const SoftwareMask* key{};
    std::uint64_t generation{};
    std::uint64_t frame_tag{};
    std::size_t width{};
    std::size_t height{};
    VkImage image{VK_NULL_HANDLE};
    VkDeviceMemory memory{VK_NULL_HANDLE};
    VkImageView view{VK_NULL_HANDLE};
    VkDescriptorSet set{VK_NULL_HANDLE};
    bool occupied{};
};

struct StrokeInstance {
    float min_x{};
    float min_y{};
    float max_x{};
    float max_y{};
    float red{};
    float green{};
    float blue{};
    float alpha{};
    float start_x{};
    float start_y{};
    float end_x{};
    float end_y{};
    float thickness{};
    float pad_a{};
    float pad_b{};
    float pad_c{};
};

struct RoundedStrokeInstance {
    float min_x{};
    float min_y{};
    float max_x{};
    float max_y{};
    float red{};
    float green{};
    float blue{};
    float alpha{};
    float center_x{};
    float center_y{};
    float half_x{};
    float half_y{};
    float radius{};
    float thickness{};
    float pad_b{};
    float pad_c{};
};

struct GlyphInstance {
    float min_x{};
    float min_y{};
    float max_x{};
    float max_y{};
    float red{};
    float green{};
    float blue{};
    float alpha{};
    float origin_u{};
    float origin_v{};
    float size_w{};
    float size_h{};
};

struct PipelineState {
    VkPipelineLayout layout{VK_NULL_HANDLE};
    VkPipeline pipeline{VK_NULL_HANDLE};
    VkBuffer instances{VK_NULL_HANDLE};
    VkDeviceMemory instance_memory{VK_NULL_HANDLE};
    void* instance_mapped{nullptr};
    VkPipelineLayout rounded_layout{VK_NULL_HANDLE};
    VkPipeline rounded_pipeline{VK_NULL_HANDLE};
    VkBuffer rounded_instances{VK_NULL_HANDLE};
    VkDeviceMemory rounded_instance_memory{VK_NULL_HANDLE};
    void* rounded_instance_mapped{nullptr};
    bool unified{};
    VkPipelineLayout image_layout{VK_NULL_HANDLE};
    VkPipeline image_pipeline{VK_NULL_HANDLE};
    VkBuffer image_instances{VK_NULL_HANDLE};
    VkDeviceMemory image_instance_memory{VK_NULL_HANDLE};
    void* image_instance_mapped{nullptr};
    VkPipelineLayout mask_layout{VK_NULL_HANDLE};
    VkPipeline mask_pipeline{VK_NULL_HANDLE};
    VkBuffer mask_instances{VK_NULL_HANDLE};
    VkDeviceMemory mask_instance_memory{VK_NULL_HANDLE};
    void* mask_instance_mapped{nullptr};
    VkPipelineLayout stroke_layout{VK_NULL_HANDLE};
    VkPipeline stroke_pipeline{VK_NULL_HANDLE};
    VkBuffer stroke_instances{VK_NULL_HANDLE};
    VkDeviceMemory stroke_instance_memory{VK_NULL_HANDLE};
    void* stroke_instance_mapped{nullptr};
    VkPipelineLayout rounded_stroke_layout{VK_NULL_HANDLE};
    VkPipeline rounded_stroke_pipeline{VK_NULL_HANDLE};
    VkBuffer rounded_stroke_instances{VK_NULL_HANDLE};
    VkDeviceMemory rounded_stroke_instance_memory{VK_NULL_HANDLE};
    void* rounded_stroke_instance_mapped{nullptr};
    VkPipelineLayout glyph_layout{VK_NULL_HANDLE};
    VkPipeline glyph_pipeline{VK_NULL_HANDLE};
    VkBuffer glyph_instances{VK_NULL_HANDLE};
    VkDeviceMemory glyph_instance_memory{VK_NULL_HANDLE};
    void* glyph_instance_mapped{nullptr};
    VkSampler nearest_sampler{VK_NULL_HANDLE};
    VkSampler linear_sampler{VK_NULL_HANDLE};
    VkDescriptorSetLayout descriptor_layout{VK_NULL_HANDLE};
    VkDescriptorPool descriptor_pool{VK_NULL_HANDLE};
    VkBuffer staging{VK_NULL_HANDLE};
    VkDeviceMemory staging_memory{VK_NULL_HANDLE};
    void* staging_mapped{nullptr};
    std::size_t staging_used{};
    std::uint64_t frame_seq{};
    ImageCacheEntry images[kVulkanMaxFrameImages]{};
    MaskCacheEntry masks[kVulkanMaxFrameImages]{};
    std::size_t evict_cursor{};
    std::size_t mask_evict_cursor{};
};

ContextState* state_of(VulkanContext& context) noexcept;
const ContextState* state_of(const VulkanContext& context) noexcept;

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

bool create_instance_buffer(
    ContextState& state,
    VkBuffer& buffer,
    VkDeviceMemory& memory,
    void*& mapped,
    std::size_t bytes
) noexcept;

void destroy_instance_buffer(
    ContextState& state,
    VkBuffer& buffer,
    VkDeviceMemory& memory,
    void*& mapped
) noexcept;

bool create_pass(VkDevice device, VkRenderPass& pass) noexcept;
bool create_load_pass(VkDevice device, VkRenderPass& pass) noexcept;

enum class ImageResolveResult : std::uint8_t {
    ready,
    too_large,
    device_error,
};

bool create_texture_cache(ContextState& state, PipelineState& pipeline) noexcept;

void destroy_texture_cache(ContextState& state, PipelineState& pipeline) noexcept;

VulkanRenderResult upload_textures(
    ContextState& state,
    PipelineState& pipeline,
    std::span<const DisplayCommand> commands,
    std::size_t& uploads,
    std::size_t& upload_bytes
) noexcept;

void evict_mask_entry(ContextState& state, PipelineState& pipeline, MaskCacheEntry& entry) noexcept;

void evict_frame_images(ContextState& state, PipelineState& pipeline) noexcept;

VkDescriptorSet lookup_mask(const PipelineState& pipeline, const SoftwareMask& mask) noexcept;

enum class MaskResolveResult : std::uint8_t {
    ready,
    too_large,
    device_error,
};

MaskResolveResult resolve_mask(
    ContextState& state,
    PipelineState& pipeline,
    const SoftwareMask& mask,
    VkDescriptorSet& descriptor,
    std::size_t& uploaded_bytes
) noexcept;

ImageResolveResult resolve_image(
    ContextState& state,
    PipelineState& pipeline,
    const SoftwareImage& image,
    SoftwareImageSampling sampling,
    VkDescriptorSet& descriptor,
    std::size_t& uploaded_bytes
) noexcept;

VkDescriptorSet lookup_image(
    const PipelineState& pipeline,
    const SoftwareImage& image,
    SoftwareImageSampling sampling
) noexcept;

void copy_device_name(char (&output)[256], const char* source) noexcept;

void copy_icd_hint(char (&output)[64]) noexcept;

bool create_rect_pipeline(
    VkDevice device,
    VkRenderPass pass,
    VkPipelineLayout& layout,
    VkPipeline& pipeline
) noexcept;

bool create_rounded_pipeline(
    VkDevice device,
    VkRenderPass pass,
    VkPipelineLayout& layout,
    VkPipeline& pipeline
) noexcept;

bool create_image_pipeline(
    VkDevice device,
    VkRenderPass pass,
    VkDescriptorSetLayout descriptor_layout,
    VkPipelineLayout& layout,
    VkPipeline& pipeline
) noexcept;

bool create_mask_pipeline(
    VkDevice device,
    VkRenderPass pass,
    VkDescriptorSetLayout descriptor_layout,
    VkPipelineLayout& layout,
    VkPipeline& pipeline
) noexcept;

bool create_stroke_pipeline(
    VkDevice device,
    VkRenderPass pass,
    VkPipelineLayout& layout,
    VkPipeline& pipeline
) noexcept;

bool create_rounded_stroke_pipeline(
    VkDevice device,
    VkRenderPass pass,
    VkPipelineLayout& layout,
    VkPipeline& pipeline
) noexcept;

bool create_glyph_pipeline(
    VkDevice device,
    VkRenderPass pass,
    VkDescriptorSetLayout descriptor_layout,
    VkPipelineLayout& layout,
    VkPipeline& pipeline
) noexcept;

bool create_stroke_pipeline(
    VkDevice device,
    VkRenderPass pass,
    VkDescriptorSetLayout descriptor_layout,
    VkPipelineLayout& layout,
    VkPipeline& pipeline
) noexcept;

}
#endif
