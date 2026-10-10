#include "software_renderer_internal.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace veyra::render_detail {
namespace {

bool valid_mask(const SoftwareMask& mask) noexcept {
    if (mask.stride < mask.width ||
        (mask.height != 0 && mask.stride > std::numeric_limits<std::size_t>::max() / mask.height)) {
        return false;
    }

    return mask.coverage.size() >= mask.height * mask.stride;
}

}

bool valid_commands(
    std::span<const DisplayCommand> commands,
    std::size_t& maximum_clip_depth,
    std::size_t& maximum_translation_depth,
    std::size_t& maximum_path_intersections,
    std::size_t& maximum_path_windings,
    bool& uses_polyline
) noexcept {
    std::size_t clip_depth = 0;
    std::size_t translation_depth = 0;
    uses_polyline = false;
    for (const auto& command : commands) {
        if (const auto* fill = std::get_if<FillRect>(&command)) {
            if (!valid_rect(fill->bounds)) {
                return false;
            }
            continue;
        }
        if (const auto* rounded = std::get_if<FillRoundedRect>(&command)) {
            if (!valid_rect(rounded->bounds) || !std::isfinite(rounded->radius) ||
                rounded->radius < 0.0F) {
                return false;
            }
            continue;
        }
        if (const auto* stroke = std::get_if<StrokeRect>(&command)) {
            if (!valid_rect(stroke->bounds) || !std::isfinite(stroke->thickness) ||
                stroke->thickness < 0.0F) {
                return false;
            }
            continue;
        }
        if (const auto* stroke = std::get_if<StrokeRoundedRect>(&command)) {
            if (!valid_rect(stroke->bounds) || !std::isfinite(stroke->radius) ||
                stroke->radius < 0.0F || !std::isfinite(stroke->thickness) ||
                stroke->thickness < 0.0F) {
                return false;
            }
            continue;
        }
        if (const auto* line = std::get_if<StrokeLine>(&command)) {
            if (!std::isfinite(line->start.x) || !std::isfinite(line->start.y) ||
                !std::isfinite(line->end.x) || !std::isfinite(line->end.y) ||
                !std::isfinite(line->thickness) || line->thickness < 0.0F) {
                return false;
            }
            continue;
        }
        if (const auto* polyline = std::get_if<StrokePolyline>(&command)) {
            if (polyline->points.size() < 2 || !std::isfinite(polyline->thickness) ||
                polyline->thickness < 0.0F) {
                return false;
            }
            for (const auto point : polyline->points) {
                if (!std::isfinite(point.x) || !std::isfinite(point.y)) {
                    return false;
                }
            }
            uses_polyline = uses_polyline ||
                (polyline->points.size() > 2 && polyline->thickness > 0.0F &&
                 polyline->color.alpha != 0);
            continue;
        }
        if (const auto* mask = std::get_if<DrawMask>(&command)) {
            if (mask->mask == nullptr || !std::isfinite(mask->x) || !std::isfinite(mask->y) ||
                !valid_mask(*mask->mask)) {
                return false;
            }
            continue;
        }
        if (const auto* path = std::get_if<FillPath>(&command)) {
            std::size_t path_intersection_capacity = 0;
            std::size_t path_winding_capacity = 0;
            if (!valid_path(*path, path_intersection_capacity, path_winding_capacity)) {
                return false;
            }
            maximum_path_intersections =
                std::max(maximum_path_intersections, path_intersection_capacity);
            maximum_path_windings = std::max(maximum_path_windings, path_winding_capacity);
            continue;
        }
        if (const auto* image = std::get_if<DrawImage>(&command)) {
            if (image->image == nullptr || !valid_image(*image->image) ||
                !valid_image_placement(*image) ||
                (image->sampling != SoftwareImageSampling::nearest &&
                 image->sampling != SoftwareImageSampling::linear)) {
                return false;
            }
            continue;
        }
        if (const auto* clip = std::get_if<PushClip>(&command)) {
            if (!valid_rect(clip->bounds)) {
                return false;
            }
            ++clip_depth;
            maximum_clip_depth = std::max(maximum_clip_depth, clip_depth);
            continue;
        }
        if (std::holds_alternative<PopClip>(command)) {
            if (clip_depth == 0) {
                return false;
            }
            --clip_depth;
            continue;
        }
        if (const auto* translation = std::get_if<PushTranslation>(&command)) {
            if (!std::isfinite(translation->offset.x) || !std::isfinite(translation->offset.y)) {
                return false;
            }
            ++translation_depth;
            maximum_translation_depth = std::max(maximum_translation_depth, translation_depth);
            continue;
        }
        if (std::holds_alternative<PopTranslation>(command)) {
            if (translation_depth == 0) {
                return false;
            }
            --translation_depth;
            continue;
        }
        return false;
    }

    return clip_depth == 0 && translation_depth == 0;
}

bool valid_translated_commands(
    std::span<const DisplayCommand> commands,
    std::span<LayoutPoint> translations
) noexcept {
    LayoutPoint offset{};
    std::size_t depth = 0;
    for (const auto& command : commands) {
        if (const auto* translation = std::get_if<PushTranslation>(&command)) {
            translations[depth++] = offset;
            offset.x += translation->offset.x;
            offset.y += translation->offset.y;
            if (!std::isfinite(offset.x) || !std::isfinite(offset.y)) {
                return false;
            }
            continue;
        }
        if (std::holds_alternative<PopTranslation>(command)) {
            offset = translations[--depth];
            continue;
        }

        const auto finite = [&offset](float x, float y) {
            return std::isfinite(x + offset.x) && std::isfinite(y + offset.y);
        };
        if (const auto* fill = std::get_if<FillRect>(&command)) {
            if (!finite(fill->bounds.x, fill->bounds.y)) {
                return false;
            }
        } else if (const auto* rounded = std::get_if<FillRoundedRect>(&command)) {
            if (!finite(rounded->bounds.x, rounded->bounds.y)) {
                return false;
            }
        } else if (const auto* stroke = std::get_if<StrokeRect>(&command)) {
            if (!finite(stroke->bounds.x, stroke->bounds.y)) {
                return false;
            }
        } else if (const auto* stroke = std::get_if<StrokeRoundedRect>(&command)) {
            if (!finite(stroke->bounds.x, stroke->bounds.y)) {
                return false;
            }
        } else if (const auto* line = std::get_if<StrokeLine>(&command)) {
            if (!finite(line->start.x, line->start.y) || !finite(line->end.x, line->end.y)) {
                return false;
            }
        } else if (const auto* polyline = std::get_if<StrokePolyline>(&command)) {
            for (const auto point : polyline->points) {
                if (!finite(point.x, point.y)) {
                    return false;
                }
            }
        } else if (const auto* mask = std::get_if<DrawMask>(&command)) {
            if (!finite(mask->x, mask->y)) {
                return false;
            }
        } else if (const auto* path = std::get_if<FillPath>(&command)) {
            if (!valid_translated_path(*path, offset)) {
                return false;
            }
        } else if (const auto* image = std::get_if<DrawImage>(&command)) {
            if (!finite(image->destination.x, image->destination.y)) {
                return false;
            }
        } else if (const auto* clip = std::get_if<PushClip>(&command)) {
            if (!finite(clip->bounds.x, clip->bounds.y)) {
                return false;
            }
        }
    }

    return true;
}

}
