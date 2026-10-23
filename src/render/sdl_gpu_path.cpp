#include "sdl_gpu_internal.hpp"

#include "software_renderer_internal.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace veyra::sdl_gpu_detail {
namespace {
struct PathRectSink {
    WalkBatch& batch;
    SdlGpuState& state;
    SoftwareClipRect clip{};
    float red{};
    float green{};
    float blue{};
    float alpha{};
    WindowResult result{WindowResult::ready};

    void emit_span(render_detail::PathPixelSpan span) noexcept {
        if (result != WindowResult::ready) {
            return;
        }

        if (span.left >= clip.right || span.right <= clip.left) {
            return;
        }

        const auto instance = RectInstance{
            .min_x = static_cast<float>(span.left),
            .min_y = static_cast<float>(span.y),
            .max_x = static_cast<float>(span.right),
            .max_y = static_cast<float>(span.y + 1),
            .red = red,
            .green = green,
            .blue = blue,
            .alpha = alpha,
        };

        result = batch.emit(
            SdlDrawPipe::rect,
            kSdlNoTexture,
            0,
            instance,
            state.rect_hosts.data(),
            sizeof(RectInstance)
        );
    }
};

}

WindowResult
SdlFrameEmitter::emit_path(const FillPath& pathed, const LayoutPoint& translation) noexcept {
    auto intersection_capacity = std::size_t{};
    auto winding_capacity = std::size_t{};

    if (!render_detail::valid_path(pathed, intersection_capacity, winding_capacity) ||
        !render_detail::valid_translated_path(pathed, translation)) {
        return WindowResult::invalid_input;
    }

    if (pathed.color.alpha == 0) {
        return WindowResult::ready;
    }

    const auto needs_windings = pathed.fill_rule == SoftwarePathFillRule::non_zero;

    if (intersection_capacity > kSdlGpuPathSpanScratch ||
        (needs_windings && winding_capacity > kSdlGpuPathSpanScratch)) {
        return WindowResult::insufficient_capacity;
    }

    auto intersections = std::array<double, kSdlGpuPathSpanScratch>{};
    auto windings = std::array<std::int8_t, kSdlGpuPathSpanScratch>{};

    auto sink = PathRectSink{
        .batch = batch,
        .state = state,
        .clip = batch.clip,
        .red = static_cast<float>(pathed.color.red) * kSdlChannelScale,
        .green = static_cast<float>(pathed.color.green) * kSdlChannelScale,
        .blue = static_cast<float>(pathed.color.blue) * kSdlChannelScale,
        .alpha = static_cast<float>(pathed.color.alpha) * kSdlChannelScale,
    };

    const auto emit_span = [](void* context, render_detail::PathPixelSpan span) noexcept {
        static_cast<PathRectSink*>(context)->emit_span(span);
    };

    render_detail::fill_path_spans(
        pathed,
        translation,
        state.width,
        state.height,
        SoftwareClipRect{
            .left = 0,
            .top = 0,
            .right = state.width,
            .bottom = state.height,
        },
        intersections,
        windings,
        &sink,
        emit_span
    );

    return sink.result;
}
}
