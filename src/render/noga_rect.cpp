#include "veyra/render/noga_rect.hpp"

#include "noga/rect_shared.h"
#include "noga_internal.hpp"
#include "software_renderer_internal.hpp"

#include "rect_frag_spv.hpp"
#include "rect_mesh_spv.hpp"

#include <NoGraphicsAPI/NoGraphicsAPI.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <new>
#include <span>
#include <variant>

namespace veyra {
namespace noga_detail {

void copy_name(char (&output)[256], const char* source) noexcept {
    auto index = std::size_t{};

    while (index + 1 < sizeof(output) && source[index] != '\0') {
        output[index] = source[index];
        ++index;
    }

    output[index] = '\0';
}

void destroy_target(State& state) noexcept {
    if (state.view != nullptr) {
        gpu::destroy_render_view(state.view);
        state.view = nullptr;
    }

    if (state.target != nullptr) {
        gpu::destroy_texture(state.target);
        state.target = nullptr;
    }

    if (state.texture_heap.owner != nullptr) {
        gpu::destroy_texture_heap(state.texture_heap);
        state.texture_heap = {};
    }

    if (state.readback.owner != nullptr) {
        gpu::destroy_gpu_heap(state.readback);
        state.readback = {};
    }

    state.target_ready = false;
}

bool initialize_target(State& state) noexcept {
    gpu::CommandBuffer* commands = gpu::begin_commands(state.pool);

    if (commands == nullptr) {
        return false;
    }

    gpu::initialize_texture(commands, state.target);
    gpu::end_commands(commands);

    state.completed += 1;
    gpu::submit(
        state.device,
        {.commands = {commands},
         .completion = {.semaphore = state.semaphore, .value = state.completed}}
    );
    gpu::wait_timeline({.semaphore = state.semaphore, .value = state.completed});

    return true;
}

bool create_target(State& state, std::size_t width, std::size_t height) noexcept {
    const gpu::TextureDesc target_desc{
        .extent =
            {
                .x = static_cast<gpu::uint32>(width),
                .y = static_cast<gpu::uint32>(height),
                .z = 1,
            },
        .usage = gpu::TextureUsage::color_attachment | gpu::TextureUsage::transfer_source,
    };

    state.texture_heap = gpu::create_texture_heap(
        state.device,
        gpu::get_texture_size_align(state.device, target_desc).size
    );

    if (state.texture_heap.owner == nullptr) {
        return false;
    }

    state.target = gpu::create_texture(state.device, target_desc, state.texture_heap, 0);

    if (state.target == nullptr) {
        return false;
    }

    state.view = gpu::create_render_view(state.target);

    if (state.view == nullptr) {
        return false;
    }

    state.readback = gpu::create_gpu_heap(
        state.device,
        static_cast<gpu::uint64>(width * height * sizeof(SoftwarePixel)),
        gpu::MemoryType::readback
    );

    if (state.readback.owner == nullptr) {
        return false;
    }

    if (!initialize_target(state)) {
        return false;
    }

    state.width = width;
    state.height = height;
    state.target_ready = true;

    return true;
}

NogaContextResult
create_state(NogaContext& context, std::size_t width, std::size_t height) noexcept {
    auto* state = new (std::nothrow) State{};

    if (state == nullptr) {
        return NogaContextResult::creation_failed;
    }

    const gpu::DeviceInit initialized = gpu::create_device({.timestamp_query_count = 2});

    if (initialized.error == gpu::Error::device_lost) {
        delete state;

        return NogaContextResult::device_lost;
    }

    if (initialized.error == gpu::Error::unsupported || initialized.device == nullptr) {
        delete state;

        return NogaContextResult::unsupported;
    }

    if (initialized.error != gpu::Error::none) {
        delete state;

        return NogaContextResult::creation_failed;
    }

    state->device = initialized.device;

    const gpu::DeviceCaps& caps = gpu::get_device_caps(state->device);

    copy_name(context.info.device_name, caps.device_name);
    context.info.timestamp_period_ns = caps.timestamp_period_ns;
    context.info.queue_count = caps.queue_count;
    context.info.indirect_mesh_draw = caps.indirect_mesh_draw;

    state->timestamp_period_ns = caps.timestamp_period_ns;
    state->epoch = 1;

    state->pool = gpu::create_command_pool(state->device);

    if (state->pool == nullptr) {
        gpu::destroy_device(state->device);
        delete state;

        return NogaContextResult::creation_failed;
    }

    state->semaphore = gpu::create_timeline_semaphore(state->device);

    if (state->semaphore == nullptr) {
        gpu::destroy_command_pool(state->pool);
        gpu::destroy_device(state->device);
        delete state;

        return NogaContextResult::creation_failed;
    }

    state->upload = gpu::create_gpu_heap(state->device, kUploadBytes);

    if (state->upload.owner == nullptr) {
        gpu::destroy_timeline_semaphore(state->semaphore);
        gpu::destroy_command_pool(state->pool);
        gpu::destroy_device(state->device);
        delete state;

        return NogaContextResult::creation_failed;
    }

    if (!create_target(*state, width, height)) {
        destroy_target(*state);
        gpu::destroy_gpu_heap(state->upload);
        gpu::destroy_timeline_semaphore(state->semaphore);
        gpu::destroy_command_pool(state->pool);
        gpu::destroy_device(state->device);
        delete state;

        return NogaContextResult::creation_failed;
    }

    context.context_state = state;
    context.width = width;
    context.height = height;
    context.ready = true;

    return NogaContextResult::ready;
}

NogaRenderResult
collect_rects(State& state, std::span<const DisplayCommand> commands, std::size_t& count) noexcept {
    auto* slots = static_cast<NogaRect*>(static_cast<void*>(state.upload.range.cpu));

    count = 0;

    auto translation = LayoutPoint{};
    auto stack = std::array<LayoutPoint, kTranslationDepth>{};
    auto depth = std::size_t{};

    for (const auto& command : commands) {
        if (const auto* push = std::get_if<PushTranslation>(&command)) {
            if (depth >= stack.size()) {
                return NogaRenderResult::insufficient_capacity;
            }

            stack[depth] = translation;
            translation.x += push->offset.x;
            translation.y += push->offset.y;
            ++depth;

            continue;
        }

        if (std::holds_alternative<PopTranslation>(command)) {
            if (depth == 0) {
                return NogaRenderResult::invalid_input;
            }

            --depth;
            translation = stack[depth];

            continue;
        }

        const auto* fill = std::get_if<FillRect>(&command);

        if (fill == nullptr) {
            return NogaRenderResult::unsupported_commands;
        }

        if (fill->color.alpha == 0) {
            continue;
        }

        const auto translated = LayoutRect{
            .x = fill->bounds.x + translation.x,
            .y = fill->bounds.y + translation.y,
            .width = fill->bounds.width,
            .height = fill->bounds.height,
        };

        if (!render_detail::valid_rect(translated)) {
            return NogaRenderResult::invalid_input;
        }

        const auto bounds = render_detail::pixel_bounds(translated, state.width, state.height);

        if (bounds.left >= bounds.right || bounds.top >= bounds.bottom) {
            continue;
        }

        if (count >= kNogaMaxRects) {
            return NogaRenderResult::insufficient_capacity;
        }

        slots[count].bounds.x = static_cast<float>(bounds.left);
        slots[count].bounds.y = static_cast<float>(bounds.top);
        slots[count].bounds.z = static_cast<float>(bounds.right);
        slots[count].bounds.w = static_cast<float>(bounds.bottom);
        slots[count].color.x = static_cast<float>(fill->color.red) * kChannelScale;
        slots[count].color.y = static_cast<float>(fill->color.green) * kChannelScale;
        slots[count].color.z = static_cast<float>(fill->color.blue) * kChannelScale;
        slots[count].color.w = static_cast<float>(fill->color.alpha) * kChannelScale;
        ++count;
    }

    if (depth != 0) {
        return NogaRenderResult::invalid_input;
    }

    return NogaRenderResult::complete;
}

void write_root(State& state, std::size_t count) noexcept {
    auto* bytes = static_cast<unsigned char*>(static_cast<void*>(state.upload.range.cpu));
    auto* root = reinterpret_cast<NogaRectRoot*>(bytes + root_offset());

    root->rects = reinterpret_cast<NogaRect*>(static_cast<void*>(state.upload.range.gpu));
    root->extent.x = static_cast<float>(state.width);
    root->extent.y = static_cast<float>(state.height);
    root->count = static_cast<std::uint32_t>(count);
    root->pad[0] = 0;
    root->pad[1] = 0;
    root->pad[2] = 0;
}

NogaRenderResult submit_frame(State& state, gpu::PSO* pso, std::size_t count) noexcept {
    gpu::wait_timeline({.semaphore = state.semaphore, .value = state.completed});
    gpu::reset_command_pool(state.pool);

    gpu::CommandBuffer* commands = gpu::begin_commands(state.pool);

    if (commands == nullptr) {
        return NogaRenderResult::device_error;
    }

    const auto width = static_cast<float>(state.width);
    const auto height = static_cast<float>(state.height);

    gpu::begin_render_pass(
        commands,
        {.colors = {{
             .render_view = state.view,
             .load = gpu::LoadOp::clear,
             .clear = {.x = 0.0F, .y = 0.0F, .z = 0.0F, .w = 1.0F},
         }}}
    );
    gpu::set_viewport(commands, {.width = width, .height = height});
    gpu::set_scissor(
        commands,
        {
            .width = static_cast<gpu::uint32>(state.width),
            .height = static_cast<gpu::uint32>(state.height),
        }
    );
    gpu::bind_pso(commands, pso);

    if (count > 0) {
        const void* root_gpu = state.upload.range.gpu + root_offset();

        gpu::draw_meshlets(
            commands,
            root_gpu,
            {.x = static_cast<gpu::uint32>(count), .y = 1, .z = 1}
        );
    }

    gpu::end_render_pass(commands);
    gpu::barrier(
        commands,
        gpu::Stage::color_output,
        gpu::Access::color_write,
        gpu::Stage::transfer,
        gpu::Access::transfer_read
    );
    gpu::copy_texture_to_memory(
        commands,
        state.target,
        {.gpu = state.readback.range.gpu, .size = state.readback.range.size}
    );
    gpu::barrier(
        commands,
        gpu::Stage::transfer,
        gpu::Access::transfer_write,
        gpu::Stage::host,
        gpu::Access::host_read
    );
    gpu::end_commands(commands);

    state.completed += 1;
    gpu::submit(
        state.device,
        {.commands = {commands},
         .completion = {.semaphore = state.semaphore, .value = state.completed}}
    );
    gpu::wait_timeline({.semaphore = state.semaphore, .value = state.completed});

    return NogaRenderResult::complete;
}

}

NogaContextResult
create_noga_context(NogaContext& context, std::size_t width, std::size_t height) noexcept {
    if (context.ready || width == 0 || height == 0) {
        return NogaContextResult::invalid_input;
    }

    return noga_detail::create_state(context, width, height);
}

void destroy_noga_context(NogaContext& context) noexcept {
    auto* state = noga_detail::state_of(context);

    if (state == nullptr) {
        return;
    }

    if (state->device != nullptr) {
        gpu::wait_idle(state->device);
        noga_detail::destroy_target(*state);

        if (state->upload.owner != nullptr) {
            gpu::destroy_gpu_heap(state->upload);
        }

        if (state->semaphore != nullptr) {
            gpu::destroy_timeline_semaphore(state->semaphore);
        }

        if (state->pool != nullptr) {
            gpu::destroy_command_pool(state->pool);
        }

        gpu::destroy_device(state->device);
    }

    delete state;

    context.context_state = nullptr;
    context.width = 0;
    context.height = 0;
    context.info = NogaDeviceInfo{};
    context.ready = false;
}

NogaContextResult
resize_noga_target(NogaContext& context, std::size_t width, std::size_t height) noexcept {
    auto* state = noga_detail::state_of(context);

    if (state == nullptr || !context.ready) {
        return NogaContextResult::invalid_input;
    }

    if (width == 0 || height == 0) {
        return NogaContextResult::invalid_input;
    }

    if (width == state->width && height == state->height) {
        return NogaContextResult::ready;
    }

    gpu::wait_idle(state->device);
    noga_detail::destroy_target(*state);

    if (!noga_detail::create_target(*state, width, height)) {
        noga_detail::destroy_target(*state);

        return NogaContextResult::creation_failed;
    }

    context.width = width;
    context.height = height;

    return NogaContextResult::ready;
}

bool noga_context_ready(const NogaContext& context) noexcept {
    return context.ready && context.context_state != nullptr;
}

bool noga_context_device_lost(const NogaContext& context) noexcept {
    const auto* state = noga_detail::state_of(context);

    return state != nullptr && state->device_lost;
}

std::uint64_t noga_context_generation(const NogaContext& context) noexcept {
    const auto* state = noga_detail::state_of(context);

    if (state == nullptr) {
        return 0;
    }

    return state->epoch;
}

NogaRenderResult
create_noga_rect_pipeline(NogaContext& context, NogaRectPipeline& pipeline) noexcept {
    auto* state = noga_detail::state_of(context);

    if (state == nullptr || !context.ready) {
        return NogaRenderResult::invalid_input;
    }

    if (pipeline.ready) {
        return NogaRenderResult::invalid_input;
    }

    gpu::PSO* pso = gpu::create_mesh_pso(
        state->device,
        {
            .mesh =
                {
                    .code = {noga_detail::kNogaRectMeshSpv, sizeof(noga_detail::kNogaRectMeshSpv)},
                    .entry_point = "meshMain",
                    .threadgroup_size = {.x = 1, .y = 1, .z = 1},
                },
            .fragment =
                {
                    .code = {noga_detail::kNogaRectFragSpv, sizeof(noga_detail::kNogaRectFragSpv)},
                    .entry_point = "fragmentMain",
                },
            .color_targets = {{
                .format = gpu::Format::rgba8_unorm,
                .blend = {
                    .enabled = true,
                    .color =
                        {
                            .source = gpu::BlendFactor::source_alpha,
                            .destination = gpu::BlendFactor::one_minus_source_alpha,
                            .operation = gpu::BlendOp::add,
                        },
                    .alpha = {
                        .source = gpu::BlendFactor::one,
                        .destination = gpu::BlendFactor::one_minus_source_alpha,
                        .operation = gpu::BlendOp::add,
                    },
                },
            }},
        }
    );

    if (pso == nullptr) {
        return NogaRenderResult::device_error;
    }

    pipeline.pipeline_state = pso;
    pipeline.ready = true;

    return NogaRenderResult::complete;
}

void destroy_noga_rect_pipeline(NogaContext& context, NogaRectPipeline& pipeline) noexcept {
    auto* pso = noga_detail::pso_of(pipeline);

    if (pso == nullptr) {
        return;
    }

    auto* state = noga_detail::state_of(context);

    if (state != nullptr && state->device != nullptr) {
        gpu::wait_idle(state->device);
        gpu::destroy_pso(pso);
    }

    pipeline.pipeline_state = nullptr;
    pipeline.ready = false;
}

NogaRenderResult render_noga_rects(
    NogaContext& context,
    NogaRectPipeline& pipeline,
    std::span<const DisplayCommand> commands,
    std::span<SoftwarePixel> output,
    NogaRectStats& stats
) noexcept {
    auto* state = noga_detail::state_of(context);
    auto* pso = noga_detail::pso_of(pipeline);

    if (state == nullptr || pso == nullptr || !context.ready || !pipeline.ready) {
        return NogaRenderResult::invalid_input;
    }

    if (output.size() != state->width * state->height || !state->target_ready) {
        return NogaRenderResult::invalid_input;
    }

    if (state->device_lost) {
        return NogaRenderResult::device_lost;
    }

    stats = NogaRectStats{};

    auto count = std::size_t{};
    const auto collected = noga_detail::collect_rects(*state, commands, count);

    if (collected != NogaRenderResult::complete) {
        return collected;
    }

    noga_detail::write_root(*state, count);

    const auto submitted = noga_detail::submit_frame(*state, pso, count);

    if (submitted != NogaRenderResult::complete) {
        return submitted;
    }

    std::memcpy(
        output.data(),
        static_cast<const void*>(state->readback.range.cpu),
        output.size() * sizeof(SoftwarePixel)
    );

    stats.submitted = count;
    stats.draws = count > 0 ? 1 : 0;

    return NogaRenderResult::complete;
}

}
