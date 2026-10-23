#ifndef VEYRA_SRC_RENDER_SDL_GPU_INTERNAL_HPP
#define VEYRA_SRC_RENDER_SDL_GPU_INTERNAL_HPP

#include "veyra/platform/sdl3_gpu_present.hpp"
#include "veyra/render/sdl_gpu_renderer.hpp"

#include <SDL3/SDL_gpu.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace veyra::sdl_gpu_detail {

constexpr std::size_t kSdlGpuBudget = kSdlGpuMaxRects * kSdlGpuMaxDraws;
constexpr std::size_t kSdlGpuChunk = kSdlGpuMaxRects;
constexpr std::size_t kSdlGpuVertexStagingBytes = 256 * 1024;
constexpr std::size_t kSdlGpuImageStagingBytes = 1 << 20;
constexpr std::size_t kSdlGpuPathSpanScratch = 256;

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

enum class SdlDrawPipe : std::uint8_t {
    rect,
    rounded,
    image,
    mask,
    stroke,
    rounded_stroke,
    glyph,
};

constexpr std::size_t kSdlGpuPipeCount = 7;

struct SdlDrawRun {
    SdlDrawPipe pipe{SdlDrawPipe::rect};
    std::uint8_t sampling{};
    std::uint16_t texture{0xFFFF};
    std::size_t first{};
    std::size_t count{};
    SoftwareClipRect clip{};
};

struct SdlImageEntry {
    const SoftwareImage* key{};
    std::uint64_t generation{};
    std::size_t width{};
    std::size_t height{};
    SDL_GPUTexture* texture{};
    bool occupied{};
};

struct SdlMaskEntry {
    const SoftwareMask* key{};
    std::uint64_t generation{};
    std::size_t width{};
    std::size_t height{};
    SDL_GPUTexture* texture{};
    bool occupied{};
};

struct SdlGpuState {
    SDL_GPUDevice* device{};
    SdlGpuPresenter* presenter{};
    std::size_t width{};
    std::size_t height{};
    SDL_GPUGraphicsPipeline* pipelines[kSdlGpuPipeCount]{};
    SDL_GPUBuffer* vertex_buffers[kSdlGpuPipeCount]{};
    std::size_t vertex_bytes[kSdlGpuPipeCount]{};
    SDL_GPUTransferBuffer* vertex_staging{};
    SDL_GPUTransferBuffer* image_staging{};
    SDL_GPUTransferBuffer* download{};
    SDL_GPUTexture* target{};
    SDL_GPUSampler* nearest_sampler{};
    SDL_GPUSampler* linear_sampler{};
    SdlImageEntry images[kSdlGpuMaxFrameImages]{};
    SdlMaskEntry masks[kSdlGpuMaxFrameImages]{};
    std::size_t image_cursor{};
    std::size_t mask_cursor{};
    std::array<RectInstance, kSdlGpuBudget> rect_hosts{};
    std::array<RoundedRectInstance, kSdlGpuBudget> rounded_hosts{};
    std::array<ImageInstance, kSdlGpuBudget> image_hosts{};
    std::array<MaskInstance, kSdlGpuBudget> mask_hosts{};
    std::array<StrokeInstance, kSdlGpuBudget> stroke_hosts{};
    std::array<RoundedStrokeInstance, kSdlGpuBudget> rounded_stroke_hosts{};
    std::array<GlyphInstance, kSdlGpuBudget> glyph_hosts{};
    std::array<SdlDrawRun, kSdlGpuBudget> runs{};
};

SdlGpuState* state_of(SdlGpuRenderer& renderer) noexcept;

bool create_sdl_pipelines(SdlGpuState& state) noexcept;

void destroy_sdl_pipelines(SdlGpuState& state) noexcept;

bool create_sdl_target(SdlGpuState& state, std::size_t width, std::size_t height) noexcept;

void destroy_sdl_target(SdlGpuState& state) noexcept;

bool create_sdl_samplers(SdlGpuState& state) noexcept;

WindowResult ensure_sdl_image(
    SdlGpuState& state,
    const SoftwareImage& image,
    std::uint16_t& slot,
    std::size_t& uploaded_bytes
) noexcept;

WindowResult ensure_sdl_mask(
    SdlGpuState& state,
    const SoftwareMask& mask,
    std::uint16_t& slot,
    std::size_t& uploaded_bytes
) noexcept;

const SDL_GPUTexture* sdl_image_texture(const SdlGpuState& state, std::uint16_t slot) noexcept;

const SDL_GPUTexture* sdl_mask_texture(const SdlGpuState& state, std::uint16_t slot) noexcept;

constexpr float kSdlChannelScale = 1.0F / 255.0F;
constexpr std::uint16_t kSdlNoTexture = 0xFFFF;

inline bool clip_excludes(SoftwareClipRect clip, SoftwareClipRect bounds) noexcept {
    return bounds.left >= clip.right || bounds.right <= clip.left || bounds.top >= clip.bottom ||
        bounds.bottom <= clip.top;
}

struct WalkBatch {
    SdlGpuState& state;
    SdlGpuRenderStats& stats;
    std::size_t pipe_slots[kSdlGpuPipeCount]{};
    SdlDrawRun open_run{};
    bool open_valid{};
    SoftwareClipRect clip{};
    WindowResult result{WindowResult::ready};

    void close_run() noexcept {
        if (!open_valid) {
            return;
        }

        state.runs[stats.draws] = open_run;
        ++stats.draws;
        open_valid = false;
    }

    void set_clip(SoftwareClipRect next) noexcept {
        close_run();
        clip = next;
    }

    template <typename Instance>
    WindowResult emit(
        SdlDrawPipe pipe,
        std::uint16_t texture,
        std::uint8_t sampling,
        const Instance& instance,
        void* base,
        std::size_t stride
    ) noexcept {
        if (result != WindowResult::ready) {
            return result;
        }

        if (stats.submitted == kSdlGpuBudget || stats.draws == kSdlGpuBudget) {
            result = WindowResult::insufficient_capacity;

            return result;
        }

        const auto index = static_cast<std::size_t>(pipe);

        if (open_valid &&
            (open_run.pipe != pipe || open_run.texture != texture ||
             open_run.sampling != sampling)) {
            close_run();
        }

        if (!open_valid) {
            open_run = SdlDrawRun{
                .pipe = pipe,
                .sampling = sampling,
                .texture = texture,
                .first = pipe_slots[index],
                .count = 0,
                .clip = clip,
            };
            open_valid = true;
        }

        const auto slot = pipe_slots[index]++;

        std::memcpy(static_cast<std::uint8_t*>(base) + slot * stride, &instance, sizeof(Instance));

        ++open_run.count;
        ++stats.submitted;

        return WindowResult::ready;
    }
};

struct ShapeStyle {
    SdlDrawPipe pipe{SdlDrawPipe::rect};
    std::uint16_t texture{kSdlNoTexture};
    LayoutRect shape{};
    DisplayColor color{};
    LayoutRect source{};
    const SoftwareImage* image{};
    const SoftwareMask* mask{};
    std::uint8_t sampling{};
    float radius{};
    float opacity{1.0F};
    float thickness{};
    LayoutPoint glyph_uv{};
    bool skip{false};
};

struct SdlFrameEmitter {
    WalkBatch& batch;
    SdlGpuState& state;

    WindowResult resolve_image(const DrawImage& drawn, ShapeStyle& style) noexcept;
    WindowResult resolve_mask(const DrawMask& masked, ShapeStyle& style) noexcept;
    WindowResult resolve_glyph(const DrawGlyph& glyphed, ShapeStyle& style) noexcept;
    WindowResult
    resolve_rounded_stroke(const StrokeRoundedRect& rounded_stroked, ShapeStyle& style) noexcept;
    WindowResult resolve_fill(
        const FillRect* fill,
        const FillRoundedRect* rounded_fill,
        ShapeStyle& style
    ) noexcept;
    WindowResult emit_shape(const ShapeStyle& style, const LayoutPoint& translation) noexcept;
    WindowResult
    emit_stroke_rect(const StrokeRect& stroked, const LayoutPoint& translation) noexcept;
    WindowResult emit_line(const StrokeLine& lined, const LayoutPoint& translation) noexcept;
    WindowResult
    emit_polyline(const StrokePolyline& polylined, const LayoutPoint& translation) noexcept;
    WindowResult emit_path(const FillPath& pathed, const LayoutPoint& translation) noexcept;
};

std::size_t stroke_rect_bars(
    const LayoutRect& bounds,
    float thickness,
    std::size_t width,
    std::size_t height,
    LayoutRect* bars
) noexcept;

WindowResult emit_stroke_segment(
    WalkBatch& batch,
    SdlGpuState& state,
    LayoutPoint start,
    LayoutPoint end,
    LayoutPoint translation,
    float thickness,
    DisplayColor color
) noexcept;

WindowResult emit_sdl_frame(
    SdlGpuState& state,
    std::span<const DisplayCommand> commands,
    SdlGpuRenderStats& stats
) noexcept;

}
#endif
