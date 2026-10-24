#include "veyra/render/noga_frame.hpp"

#include "noga_internal.hpp"
#include "software_renderer_internal.hpp"
#include "veyra/render/glyph_atlas.hpp"

#include <NoGraphicsAPI/NoGraphicsAPI.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <variant>

namespace veyra {
namespace noga_detail {

namespace {

void destroy_entry(State& state, NogaImageEntry& entry) noexcept {
    if (entry.texture != nullptr) {
        gpu::destroy_texture(entry.texture);
        entry.texture = nullptr;
    }

    if (entry.heap.owner != nullptr) {
        gpu::destroy_texture_heap(entry.heap);
        entry.heap = {};
    }

    entry.occupied = false;
}

void destroy_entry(State& state, NogaMaskEntry& entry) noexcept {
    static_cast<void>(state);

    if (entry.texture != nullptr) {
        gpu::destroy_texture(entry.texture);
        entry.texture = nullptr;
    }

    if (entry.heap.owner != nullptr) {
        gpu::destroy_texture_heap(entry.heap);
        entry.heap = {};
    }

    entry.occupied = false;
}

bool upload_image_entry(
    State& state,
    FramePipelines& stored,
    gpu::CommandBuffer* commands,
    const SoftwareImage& image,
    NogaImageEntry& entry,
    std::size_t& uploaded_bytes
) noexcept {
    uploaded_bytes = 0;

    const auto row_bytes = image.width * sizeof(SoftwarePixel);
    const auto upload_bytes = row_bytes * image.height;

    stored.staging_used =
        (stored.staging_used + static_cast<std::size_t>(3)) & ~static_cast<std::size_t>(3);

    if (upload_bytes == 0 || upload_bytes > kFrameStagingBytes ||
        stored.staging_used + upload_bytes > kFrameStagingBytes) {
        return false;
    }

    const auto desc = gpu::TextureDesc{
        .extent =
            {.x = static_cast<gpu::uint32>(image.width),
             .y = static_cast<gpu::uint32>(image.height),
             .z = 1},
        .format = gpu::Format::rgba8_unorm,
        .usage = gpu::TextureUsage::sampled | gpu::TextureUsage::transfer_destination,
    };

    const auto placement = gpu::get_texture_size_align(state.device, desc);
    auto heap = gpu::create_texture_heap(state.device, placement.size);

    if (heap.owner == nullptr) {
        return false;
    }

    auto* texture = gpu::create_texture(state.device, desc, heap, 0);

    if (texture == nullptr) {
        gpu::destroy_texture_heap(heap);

        return false;
    }

    const auto staging_offset = stored.staging_base + stored.staging_used;
    auto* staging =
        static_cast<std::uint8_t*>(static_cast<void*>(stored.upload.range.cpu)) + staging_offset;

    for (auto y = std::size_t{}; y < image.height; ++y) {
        std::memcpy(staging + y * row_bytes, image.pixels.data() + y * image.stride, row_bytes);
    }

    stored.staging_used += upload_bytes;
    uploaded_bytes = upload_bytes;

    gpu::initialize_texture(commands, texture);
    gpu::copy_memory_to_texture(
        commands,
        {.gpu = static_cast<std::uint8_t*>(static_cast<void*>(stored.upload.range.gpu)) +
             staging_offset,
         .size = static_cast<gpu::uint64>(upload_bytes)},
        texture,
        {}
    );
    gpu::write_texture_descriptor(
        stored.textures,
        entry.slot,
        texture,
        gpu::TextureDescriptorType::sampled
    );

    entry.key = &image;
    entry.generation = image.generation;
    entry.frame_tag = stored.frame_seq;
    entry.width = image.width;
    entry.height = image.height;
    entry.heap = heap;
    entry.texture = texture;
    entry.occupied = true;

    return true;
}

bool upload_mask_entry(
    State& state,
    FramePipelines& stored,
    gpu::CommandBuffer* commands,
    const SoftwareMask& mask,
    NogaMaskEntry& entry,
    std::size_t& uploaded_bytes
) noexcept {
    uploaded_bytes = 0;

    const auto upload_bytes = mask.width * mask.height;

    stored.staging_used =
        (stored.staging_used + static_cast<std::size_t>(3)) & ~static_cast<std::size_t>(3);

    if (upload_bytes == 0 || upload_bytes > kFrameStagingBytes ||
        stored.staging_used + upload_bytes > kFrameStagingBytes) {
        return false;
    }

    const auto desc = gpu::TextureDesc{
        .extent =
            {.x = static_cast<gpu::uint32>(mask.width),
             .y = static_cast<gpu::uint32>(mask.height),
             .z = 1},
        .format = gpu::Format::r8_unorm,
        .usage = gpu::TextureUsage::sampled | gpu::TextureUsage::transfer_destination,
    };

    const auto placement = gpu::get_texture_size_align(state.device, desc);
    auto heap = gpu::create_texture_heap(state.device, placement.size);

    if (heap.owner == nullptr) {
        return false;
    }

    auto* texture = gpu::create_texture(state.device, desc, heap, 0);

    if (texture == nullptr) {
        gpu::destroy_texture_heap(heap);

        return false;
    }

    const auto staging_offset = stored.staging_base + stored.staging_used;
    auto* staging =
        static_cast<std::uint8_t*>(static_cast<void*>(stored.upload.range.cpu)) + staging_offset;

    for (auto y = std::size_t{}; y < mask.height; ++y) {
        std::memcpy(staging + y * mask.width, mask.coverage.data() + y * mask.stride, mask.width);
    }

    stored.staging_used += upload_bytes;
    uploaded_bytes = upload_bytes;

    gpu::initialize_texture(commands, texture);
    gpu::copy_memory_to_texture(
        commands,
        {.gpu = static_cast<std::uint8_t*>(static_cast<void*>(stored.upload.range.gpu)) +
             staging_offset,
         .size = static_cast<gpu::uint64>(upload_bytes)},
        texture,
        {}
    );
    gpu::write_texture_descriptor(
        stored.textures,
        entry.slot,
        texture,
        gpu::TextureDescriptorType::sampled
    );

    entry.key = &mask;
    entry.generation = mask.generation;
    entry.frame_tag = stored.frame_seq;
    entry.width = mask.width;
    entry.height = mask.height;
    entry.heap = heap;
    entry.texture = texture;
    entry.occupied = true;

    return true;
}

NogaRenderResult resolve_image_upload(
    State& state,
    FramePipelines& stored,
    gpu::CommandBuffer* commands,
    const SoftwareImage& image,
    const NogaImageEntry*& entry,
    std::size_t& uploaded_bytes
) noexcept {
    uploaded_bytes = 0;

    for (auto& cached : stored.images) {
        if (!cached.occupied || cached.key != &image || cached.generation != image.generation ||
            cached.width != image.width || cached.height != image.height) {
            continue;
        }

        entry = &cached;

        return NogaRenderResult::complete;
    }

    auto slot = stored.image_cursor % kNogaMaxFrameImages;

    stored.image_cursor += 1;

    for (auto index = std::size_t{}; index < kNogaMaxFrameImages; ++index) {
        auto& candidate = stored.images[(slot + index) % kNogaMaxFrameImages];

        if (candidate.occupied) {
            continue;
        }

        candidate.slot = static_cast<std::uint32_t>((slot + index) % kNogaMaxFrameImages);

        if (!upload_image_entry(state, stored, commands, image, candidate, uploaded_bytes)) {
            return NogaRenderResult::insufficient_capacity;
        }

        entry = &candidate;

        return NogaRenderResult::complete;
    }

    auto& victim = stored.images[slot];

    destroy_entry(state, victim);

    victim.slot = static_cast<std::uint32_t>(slot);

    if (!upload_image_entry(state, stored, commands, image, victim, uploaded_bytes)) {
        return NogaRenderResult::insufficient_capacity;
    }

    entry = &victim;

    return NogaRenderResult::complete;
}

NogaRenderResult resolve_mask_upload(
    State& state,
    FramePipelines& stored,
    gpu::CommandBuffer* commands,
    const SoftwareMask& mask,
    const NogaMaskEntry*& entry,
    std::size_t& uploaded_bytes
) noexcept {
    uploaded_bytes = 0;

    for (auto& cached : stored.masks) {
        if (!cached.occupied || cached.key != &mask || cached.generation != mask.generation ||
            cached.width != mask.width || cached.height != mask.height) {
            continue;
        }

        entry = &cached;

        return NogaRenderResult::complete;
    }

    auto slot = stored.mask_cursor % kNogaMaxFrameImages;

    stored.mask_cursor += 1;

    for (auto index = std::size_t{}; index < kNogaMaxFrameImages; ++index) {
        auto& candidate = stored.masks[(slot + index) % kNogaMaxFrameImages];

        if (candidate.occupied) {
            continue;
        }

        candidate.slot =
            static_cast<std::uint32_t>(kNogaMaxFrameImages + (slot + index) % kNogaMaxFrameImages);

        if (!upload_mask_entry(state, stored, commands, mask, candidate, uploaded_bytes)) {
            return NogaRenderResult::insufficient_capacity;
        }

        entry = &candidate;

        return NogaRenderResult::complete;
    }

    auto& victim = stored.masks[slot];

    destroy_entry(state, victim);

    victim.slot = static_cast<std::uint32_t>(kNogaMaxFrameImages + slot);

    if (!upload_mask_entry(state, stored, commands, mask, victim, uploaded_bytes)) {
        return NogaRenderResult::insufficient_capacity;
    }

    entry = &victim;

    return NogaRenderResult::complete;
}

}

const NogaImageEntry* lookup_image(
    const FramePipelines& stored,
    const SoftwareImage& image,
    SoftwareImageSampling sampling
) noexcept {
    static_cast<void>(sampling);

    for (const auto& entry : stored.images) {
        if (!entry.occupied || entry.key != &image || entry.generation != image.generation ||
            entry.width != image.width || entry.height != image.height) {
            continue;
        }

        return &entry;
    }

    return nullptr;
}

const NogaMaskEntry* lookup_mask(const FramePipelines& stored, const SoftwareMask& mask) noexcept {
    for (const auto& entry : stored.masks) {
        if (!entry.occupied || entry.key != &mask || entry.generation != mask.generation ||
            entry.width != mask.width || entry.height != mask.height) {
            continue;
        }

        return &entry;
    }

    return nullptr;
}

void evict_frame_entries(State& state, FramePipelines& stored) noexcept {
    for (auto& entry : stored.images) {
        if (entry.occupied && entry.frame_tag == stored.frame_seq) {
            destroy_entry(state, entry);
        }
    }

    for (auto& entry : stored.masks) {
        if (entry.occupied && entry.frame_tag == stored.frame_seq) {
            destroy_entry(state, entry);
        }
    }
}

NogaRenderResult upload_frame_textures(
    State& state,
    FramePipelines& stored,
    gpu::CommandBuffer* commands,
    std::span<const DisplayCommand> frame_commands,
    std::size_t& uploads,
    std::size_t& upload_bytes
) noexcept {
    uploads = 0;
    upload_bytes = 0;

    for (const auto& command : frame_commands) {
        if (const auto* drawn = std::get_if<DrawImage>(&command)) {
            if (drawn->image == nullptr || !render_detail::valid_image(*drawn->image) ||
                !render_detail::valid_image_placement(*drawn)) {
                return NogaRenderResult::invalid_input;
            }

            if (drawn->opacity == 0) {
                continue;
            }

            if (drawn->source.width <= 0.0F || drawn->source.height <= 0.0F) {
                continue;
            }

            const NogaImageEntry* entry = nullptr;
            auto bytes = std::size_t{};

            const auto resolved =
                resolve_image_upload(state, stored, commands, *drawn->image, entry, bytes);

            if (resolved != NogaRenderResult::complete) {
                return resolved;
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
                return NogaRenderResult::invalid_input;
            }

            if (masked->color.alpha == 0 || masked->mask->width == 0 || masked->mask->height == 0) {
                continue;
            }

            const NogaMaskEntry* entry = nullptr;
            auto bytes = std::size_t{};

            const auto resolved =
                resolve_mask_upload(state, stored, commands, *masked->mask, entry, bytes);

            if (resolved != NogaRenderResult::complete) {
                return resolved;
            }

            if (bytes > 0) {
                ++uploads;
                upload_bytes += bytes;
            }

            continue;
        }

        const auto* atlas = glyphed->atlas;

        if (atlas == nullptr) {
            return NogaRenderResult::invalid_input;
        }

        const auto& view = atlas->view();

        if (view.width == 0 || view.height == 0) {
            return NogaRenderResult::invalid_input;
        }

        if (glyphed->color.alpha == 0) {
            continue;
        }

        const NogaMaskEntry* entry = nullptr;
        auto bytes = std::size_t{};

        const auto resolved = resolve_mask_upload(state, stored, commands, view, entry, bytes);

        if (resolved != NogaRenderResult::complete) {
            return resolved;
        }

        if (bytes > 0) {
            ++uploads;
            upload_bytes += bytes;
        }
    }

    return NogaRenderResult::complete;
}

void destroy_frame_entries(State& state, FramePipelines& stored) noexcept {
    for (auto& entry : stored.images) {
        if (entry.occupied) {
            destroy_entry(state, entry);
        }
    }

    for (auto& entry : stored.masks) {
        if (entry.occupied) {
            destroy_entry(state, entry);
        }
    }
}

}

}