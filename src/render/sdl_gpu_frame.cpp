#include "sdl_gpu_internal.hpp"

#include "software_renderer_internal.hpp"

#include <array>
#include <cstddef>
#include <variant>

namespace veyra::sdl_gpu_detail {
WindowResult emit_sdl_frame(
    SdlGpuState& state,
    std::span<const DisplayCommand> commands,
    SdlGpuRenderStats& stats
) noexcept {
    auto batch = WalkBatch{
        .state = state,
        .stats = stats,
        .clip = SoftwareClipRect{
            .left = 0,
            .top = 0,
            .right = state.width,
            .bottom = state.height,
        },
    };

    auto emitter = SdlFrameEmitter{batch, state};
    auto translation = LayoutPoint{};
    auto translation_stack = std::array<LayoutPoint, 8>{};
    auto translation_depth = std::size_t{};
    auto clip_stack = std::array<SoftwareClipRect, 8>{};
    auto clip_depth = std::size_t{};

    for (const auto& command : commands) {
        if (const auto* push = std::get_if<PushTranslation>(&command)) {
            if (translation_depth >= translation_stack.size()) {
                return WindowResult::insufficient_capacity;
            }

            translation_stack[translation_depth] = translation;
            translation.x += push->offset.x;
            translation.y += push->offset.y;
            ++translation_depth;

            continue;
        }

        if (std::holds_alternative<PopTranslation>(command)) {
            if (translation_depth == 0) {
                return WindowResult::invalid_input;
            }

            --translation_depth;
            translation = translation_stack[translation_depth];

            continue;
        }

        if (const auto* push_clip = std::get_if<PushClip>(&command)) {
            if (!render_detail::valid_rect(push_clip->bounds)) {
                return WindowResult::invalid_input;
            }

            if (clip_depth >= clip_stack.size()) {
                return WindowResult::insufficient_capacity;
            }

            clip_stack[clip_depth++] = batch.clip;

            auto translated = push_clip->bounds;
            translated.x += translation.x;
            translated.y += translation.y;

            const auto pushed = render_detail::pixel_bounds(translated, state.width, state.height);

            batch.set_clip(render_detail::intersect(batch.clip, pushed));

            continue;
        }

        if (std::holds_alternative<PopClip>(command)) {
            if (clip_depth == 0) {
                return WindowResult::invalid_input;
            }

            batch.set_clip(clip_stack[--clip_depth]);

            continue;
        }

        const auto* fill = std::get_if<FillRect>(&command);
        const auto* rounded_fill = std::get_if<FillRoundedRect>(&command);
        const auto* drawn = std::get_if<DrawImage>(&command);
        const auto* masked = std::get_if<DrawMask>(&command);
        const auto* glyphed = std::get_if<DrawGlyph>(&command);
        const auto* stroked = std::get_if<StrokeRect>(&command);
        const auto* pathed = std::get_if<FillPath>(&command);
        const auto* rounded_stroked = std::get_if<StrokeRoundedRect>(&command);
        const auto* lined = std::get_if<StrokeLine>(&command);
        const auto* polylined = std::get_if<StrokePolyline>(&command);

        if (fill == nullptr && rounded_fill == nullptr && drawn == nullptr && masked == nullptr &&
            glyphed == nullptr && stroked == nullptr && pathed == nullptr &&
            rounded_stroked == nullptr && lined == nullptr && polylined == nullptr) {
            return WindowResult::unsupported;
        }

        if (stroked != nullptr) {
            const auto emitted = emitter.emit_stroke_rect(*stroked, translation);

            if (emitted != WindowResult::ready) {
                return emitted;
            }

            continue;
        }

        if (lined != nullptr) {
            const auto emitted = emitter.emit_line(*lined, translation);

            if (emitted != WindowResult::ready) {
                return emitted;
            }

            continue;
        }

        if (polylined != nullptr) {
            const auto emitted = emitter.emit_polyline(*polylined, translation);

            if (emitted != WindowResult::ready) {
                return emitted;
            }

            continue;
        }

        if (pathed != nullptr) {
            const auto emitted = emitter.emit_path(*pathed, translation);

            if (emitted != WindowResult::ready) {
                return emitted;
            }

            continue;
        }

        auto style = ShapeStyle{};

        if (drawn != nullptr) {
            const auto resolved = emitter.resolve_image(*drawn, style);

            if (resolved != WindowResult::ready) {
                return resolved;
            }
        } else if (masked != nullptr) {
            const auto resolved = emitter.resolve_mask(*masked, style);

            if (resolved != WindowResult::ready) {
                return resolved;
            }
        } else if (glyphed != nullptr) {
            const auto resolved = emitter.resolve_glyph(*glyphed, style);

            if (resolved != WindowResult::ready) {
                return resolved;
            }
        } else if (rounded_stroked != nullptr) {
            const auto resolved = emitter.resolve_rounded_stroke(*rounded_stroked, style);

            if (resolved != WindowResult::ready) {
                return resolved;
            }
        } else {
            const auto resolved = emitter.resolve_fill(fill, rounded_fill, style);

            if (resolved != WindowResult::ready) {
                return resolved;
            }
        }

        if (style.skip) {
            continue;
        }

        const auto emitted = emitter.emit_shape(style, translation);

        if (emitted != WindowResult::ready) {
            return emitted;
        }
    }

    if (translation_depth != 0) {
        return WindowResult::invalid_input;
    }

    if (clip_depth != 0) {
        return WindowResult::invalid_input;
    }

    batch.close_run();

    return batch.result;
}

}
