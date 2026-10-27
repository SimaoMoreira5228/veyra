#include "veyra/render/common/display_command_hash.hpp"

#include <bit>
#include <cstddef>
#include <cstdint>
#include <span>
#include <variant>

namespace veyra {
namespace {

std::uint64_t mix_hash(std::uint64_t hash, std::uint64_t value) noexcept {
    hash ^= value;
    hash *= 1099511628211ULL;

    return hash;
}

std::uint64_t mix_float(std::uint64_t hash, float value) noexcept {
    return mix_hash(hash, std::bit_cast<std::uint32_t>(value));
}

std::uint64_t mix_rect(std::uint64_t hash, LayoutRect rect) noexcept {
    hash = mix_float(hash, rect.x);
    hash = mix_float(hash, rect.y);
    hash = mix_float(hash, rect.width);

    return mix_float(hash, rect.height);
}

std::uint64_t mix_point(std::uint64_t hash, LayoutPoint point) noexcept {
    hash = mix_float(hash, point.x);

    return mix_float(hash, point.y);
}

std::uint64_t mix_color(std::uint64_t hash, Srgba8 color) noexcept {
    hash = mix_hash(hash, color.red);
    hash = mix_hash(hash, color.green);
    hash = mix_hash(hash, color.blue);

    return mix_hash(hash, color.alpha);
}

std::uint64_t mix_mask(std::uint64_t hash, const SoftwareMask& mask) noexcept {
    hash = mix_hash(hash, mask.width);
    hash = mix_hash(hash, mask.height);

    for (const auto byte : mask.coverage) {
        hash = mix_hash(hash, byte);
    }

    return mix_hash(hash, mask.generation);
}

std::uint64_t mix_segment(std::uint64_t hash, const SoftwarePathSegment& segment) noexcept {
    hash = mix_hash(hash, segment.index());

    if (const auto* line = std::get_if<SoftwareLineTo>(&segment)) {
        return mix_point(hash, line->end);
    }

    if (const auto* quad = std::get_if<SoftwareQuadraticTo>(&segment)) {
        hash = mix_point(hash, quad->control);

        return mix_point(hash, quad->end);
    }

    if (const auto* cubic = std::get_if<SoftwareCubicTo>(&segment)) {
        hash = mix_point(hash, cubic->control_1);
        hash = mix_point(hash, cubic->control_2);

        return mix_point(hash, cubic->end);
    }

    return hash;
}

std::uint64_t mix_image(std::uint64_t hash, const SoftwareImage& image) noexcept {
    hash = mix_hash(hash, image.width);
    hash = mix_hash(hash, image.height);

    for (const auto& pixel : image.pixels) {
        hash = mix_color(
            hash,
            Srgba8{
                .red = pixel.red,
                .green = pixel.green,
                .blue = pixel.blue,
                .alpha = pixel.alpha,
            }
        );
    }

    return mix_hash(hash, image.generation);
}

std::uint64_t hash_command(std::uint64_t hash, const DisplayCommand& command) noexcept {
    hash = mix_hash(hash, command.index());

    if (const auto* value = std::get_if<FillRect>(&command)) {
        return mix_color(mix_rect(hash, value->bounds), value->color);
    }

    if (const auto* value = std::get_if<FillRoundedRect>(&command)) {
        hash = mix_rect(hash, value->bounds);
        hash = mix_float(hash, value->radius);

        return mix_color(hash, value->color);
    }

    if (const auto* value = std::get_if<StrokeRect>(&command)) {
        hash = mix_rect(hash, value->bounds);
        hash = mix_float(hash, value->thickness);

        return mix_color(hash, value->color);
    }

    if (const auto* value = std::get_if<StrokeRoundedRect>(&command)) {
        hash = mix_rect(hash, value->bounds);
        hash = mix_float(hash, value->radius);
        hash = mix_float(hash, value->thickness);

        return mix_color(hash, value->color);
    }

    if (const auto* value = std::get_if<StrokeLine>(&command)) {
        hash = mix_point(hash, value->start);
        hash = mix_point(hash, value->end);
        hash = mix_float(hash, value->thickness);

        return mix_color(hash, value->color);
    }

    if (const auto* value = std::get_if<StrokePolyline>(&command)) {
        for (const auto& point : value->points) {
            hash = mix_point(hash, point);
        }

        hash = mix_float(hash, value->thickness);
        hash = mix_hash(hash, value->closed ? 1U : 0U);

        return mix_color(hash, value->color);
    }

    if (const auto* value = std::get_if<DrawMask>(&command)) {
        hash = mix_float(hash, value->x);
        hash = mix_float(hash, value->y);
        hash = mix_mask(hash, *value->mask);

        return mix_color(hash, value->color);
    }

    if (const auto* value = std::get_if<DrawGlyph>(&command)) {
        hash = mix_hash(hash, reinterpret_cast<std::uint64_t>(value->atlas));
        hash = mix_hash(hash, value->u);
        hash = mix_hash(hash, value->v);
        hash = mix_hash(hash, value->width);
        hash = mix_hash(hash, value->height);
        hash = mix_float(hash, value->x);
        hash = mix_float(hash, value->y);

        return mix_color(hash, value->color);
    }

    if (const auto* value = std::get_if<FillPath>(&command)) {
        for (const auto& contour : value->contours) {
            hash = mix_point(hash, contour.start);

            for (const auto& segment : contour.segments) {
                hash = mix_segment(hash, segment);
            }
        }

        hash = mix_hash(hash, static_cast<std::uint64_t>(value->fill_rule));

        return mix_color(hash, value->color);
    }

    if (const auto* value = std::get_if<DrawImage>(&command)) {
        hash = mix_rect(hash, value->source);
        hash = mix_rect(hash, value->destination);
        hash = mix_image(hash, *value->image);
        hash = mix_hash(hash, static_cast<std::uint64_t>(value->sampling));

        return mix_hash(hash, value->opacity);
    }

    if (const auto* value = std::get_if<PushTranslation>(&command)) {
        return mix_point(hash, value->offset);
    }

    if (const auto* value = std::get_if<PushClip>(&command)) {
        hash = mix_rect(hash, value->bounds);

        return mix_hash(hash, value->clip_id);
    }

    return hash;
}

}

std::uint64_t hash_display_commands(std::span<const DisplayCommand> commands) noexcept {
    auto hash = std::uint64_t{14695981039346656037ULL};

    for (const auto& command : commands) {
        hash = hash_command(hash, command);
    }

    return mix_hash(hash, commands.size());
}

}
