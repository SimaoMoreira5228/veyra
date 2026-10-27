#include "veyra/render/display/display_batches.hpp"

#include <variant>

namespace veyra {
namespace {

bool same_draw_run(const BatchKey& left, const BatchKey& right) noexcept {
    if (left.primitive != right.primitive) {
        return false;
    }

    if (left.resource != right.resource) {
        return false;
    }

    if (left.clip_depth != right.clip_depth) {
        return false;
    }

    if (left.clip_id != right.clip_id) {
        return false;
    }

    if (left.primitive == BatchPrimitive::draw_image && left.sampling != right.sampling) {
        return false;
    }

    return true;
}

bool draw_key(const DisplayCommand& command, BatchKey& key) noexcept {
    if (std::get_if<FillRect>(&command) != nullptr) {
        key.primitive = BatchPrimitive::fill_rect;
        key.resource = nullptr;
        return true;
    }

    if (std::get_if<FillRoundedRect>(&command) != nullptr) {
        key.primitive = BatchPrimitive::fill_rounded_rect;
        key.resource = nullptr;
        return true;
    }

    if (std::get_if<StrokeRect>(&command) != nullptr) {
        key.primitive = BatchPrimitive::stroke_rect;
        key.resource = nullptr;
        return true;
    }

    if (std::get_if<StrokeRoundedRect>(&command) != nullptr) {
        key.primitive = BatchPrimitive::stroke_rounded_rect;
        key.resource = nullptr;
        return true;
    }

    if (std::get_if<StrokeLine>(&command) != nullptr) {
        key.primitive = BatchPrimitive::stroke_line;
        key.resource = nullptr;
        return true;
    }

    if (std::get_if<StrokePolyline>(&command) != nullptr) {
        key.primitive = BatchPrimitive::stroke_polyline;
        key.resource = nullptr;
        return true;
    }

    if (const auto* mask = std::get_if<DrawMask>(&command)) {
        key.primitive = BatchPrimitive::draw_mask;
        key.resource = mask->mask;
        return true;
    }

    if (const auto* glyph = std::get_if<DrawGlyph>(&command)) {
        key.primitive = BatchPrimitive::draw_glyph;
        key.resource = glyph->atlas;
        return true;
    }

    if (std::get_if<FillPath>(&command) != nullptr) {
        key.primitive = BatchPrimitive::fill_path;
        key.resource = nullptr;
        return true;
    }

    if (const auto* image = std::get_if<DrawImage>(&command)) {
        key.primitive = BatchPrimitive::draw_image;
        key.resource = image->image;
        key.sampling = image->sampling;
        return true;
    }

    return false;
}

}

bool same_batch_key(const BatchKey& left, const BatchKey& right) noexcept {
    return same_draw_run(left, right);
}

BatchAnalysisResult analyze_display_batches(
    std::span<const DisplayCommand> commands,
    std::span<BatchRun> output,
    std::span<std::uint64_t> clip_scopes,
    std::size_t& produced
) noexcept {
    auto clip_depth = std::size_t{0};
    auto clip_id = std::uint64_t{0};
    auto scopes_fit = true;
    auto has_open = false;
    auto open_key = BatchKey{};
    auto open_start = std::size_t{0};
    auto open_count = std::size_t{0};
    auto total = std::size_t{0};
    auto stored = std::size_t{0};

    const auto close_run = [&]() noexcept {
        if (!has_open) {
            return;
        }

        has_open = false;

        if (stored < output.size()) {
            output[stored] = BatchRun{
                .key = open_key,
                .start = open_start,
                .count = open_count,
                .sequence = stored,
            };
            ++stored;
        }

        ++total;
    };

    for (std::size_t index = 0; index < commands.size(); ++index) {
        const auto& command = commands[index];

        if (const auto* push = std::get_if<PushClip>(&command)) {
            close_run();

            if (clip_depth < clip_scopes.size()) {
                clip_scopes[clip_depth] = push->clip_id;
            } else {
                scopes_fit = false;
            }
            ++clip_depth;
            clip_id = push->clip_id;
            continue;
        }

        if (std::holds_alternative<PopClip>(command)) {
            close_run();

            if (clip_depth != 0) {
                --clip_depth;
            }

            if (clip_depth == 0) {
                clip_id = 0;
            } else if (clip_depth - 1 < clip_scopes.size()) {
                clip_id = clip_scopes[clip_depth - 1];
            } else {
                clip_id = 0;
                scopes_fit = false;
            }

            continue;
        }

        if (std::holds_alternative<PushTranslation>(command) ||
            std::holds_alternative<PopTranslation>(command)) {
            close_run();
            continue;
        }

        auto key = BatchKey{};
        const auto is_draw = draw_key(command, key);

        if (!is_draw) {
            close_run();
            continue;
        }

        key.clip_depth = clip_depth;
        key.clip_id = clip_id;

        if (has_open && same_draw_run(open_key, key)) {
            ++open_count;
            continue;
        }

        close_run();

        open_key = key;
        open_start = index;
        open_count = 1;
        has_open = true;
    }

    close_run();

    produced = total;

    if (stored != total || !scopes_fit) {
        return BatchAnalysisResult::insufficient_output;
    }

    return BatchAnalysisResult::complete;
}

}
