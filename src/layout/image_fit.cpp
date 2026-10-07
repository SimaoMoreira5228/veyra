#include "veyra/layout/image_fit.hpp"

#include <algorithm>
#include <cmath>

namespace veyra {
namespace {

bool valid_rect(const LayoutRect& rect) noexcept {
    return std::isfinite(rect.x) && std::isfinite(rect.y) && std::isfinite(rect.width) &&
        std::isfinite(rect.height) && rect.width >= 0.0F && rect.height >= 0.0F;
}

bool valid_mode(ImageFitMode mode) noexcept {
    return mode == ImageFitMode::fill || mode == ImageFitMode::contain ||
        mode == ImageFitMode::cover || mode == ImageFitMode::none ||
        mode == ImageFitMode::scale_down;
}

}

ImageFitResult fit_image(
    LayoutRect bounds,
    ImageSize intrinsic_size,
    ImageFitMode mode,
    ImagePlacement& placement
) noexcept {
    if (!valid_rect(bounds) || !std::isfinite(intrinsic_size.width) ||
        !std::isfinite(intrinsic_size.height) || intrinsic_size.width <= 0.0F ||
        intrinsic_size.height <= 0.0F || !valid_mode(mode)) {
        return ImageFitResult::invalid_input;
    }
    if (bounds.width == 0.0F || bounds.height == 0.0F) {
        return ImageFitResult::empty;
    }

    auto result = ImagePlacement{
        .source =
            {
                .width = intrinsic_size.width,
                .height = intrinsic_size.height,
            },
        .destination = bounds,
    };
    if (mode == ImageFitMode::fill) {
        placement = result;
        return ImageFitResult::complete;
    }

    const auto width_scale = static_cast<double>(bounds.width) / intrinsic_size.width;
    const auto height_scale = static_cast<double>(bounds.height) / intrinsic_size.height;
    auto scale = 1.0;
    if (mode == ImageFitMode::contain || mode == ImageFitMode::scale_down) {
        scale = std::min(width_scale, height_scale);
        if (mode == ImageFitMode::scale_down) {
            scale = std::min(scale, 1.0);
        }
    } else if (mode == ImageFitMode::cover) {
        scale = std::max(width_scale, height_scale);
    }

    if (mode == ImageFitMode::cover) {
        result.source.width =
            std::min(static_cast<float>(bounds.width / scale), intrinsic_size.width);
        result.source.height =
            std::min(static_cast<float>(bounds.height / scale), intrinsic_size.height);
        result.source.x = std::min(
            (intrinsic_size.width - result.source.width) * 0.5F,
            intrinsic_size.width - result.source.width
        );
        result.source.y = std::min(
            (intrinsic_size.height - result.source.height) * 0.5F,
            intrinsic_size.height - result.source.height
        );
    } else {
        result.destination.width = static_cast<float>(intrinsic_size.width * scale);
        result.destination.height = static_cast<float>(intrinsic_size.height * scale);
        if (mode != ImageFitMode::none) {
            result.destination.width = std::min(result.destination.width, bounds.width);
            result.destination.height = std::min(result.destination.height, bounds.height);
        }
        result.destination.x += (bounds.width - result.destination.width) * 0.5F;
        result.destination.y += (bounds.height - result.destination.height) * 0.5F;
    }

    if (mode == ImageFitMode::none) {
        const auto visible_left = std::max(bounds.x, result.destination.x);
        const auto visible_top = std::max(bounds.y, result.destination.y);
        const auto visible_right = std::min(
            static_cast<double>(bounds.x) + bounds.width,
            static_cast<double>(result.destination.x) + result.destination.width
        );
        const auto visible_bottom = std::min(
            static_cast<double>(bounds.y) + bounds.height,
            static_cast<double>(result.destination.y) + result.destination.height
        );
        const auto visible_width = visible_right - visible_left;
        const auto visible_height = visible_bottom - visible_top;
        if (visible_width <= 0.0 || visible_height <= 0.0) {
            return ImageFitResult::empty;
        }

        const auto source_scale_x = result.source.width / result.destination.width;
        const auto source_scale_y = result.source.height / result.destination.height;
        result.source.x += static_cast<float>(visible_left - result.destination.x) * source_scale_x;
        result.source.y += static_cast<float>(visible_top - result.destination.y) * source_scale_y;
        result.source.width = static_cast<float>(visible_width) * source_scale_x;
        result.source.height = static_cast<float>(visible_height) * source_scale_y;
        result.destination = {
            .x = visible_left,
            .y = visible_top,
            .width = static_cast<float>(visible_width),
            .height = static_cast<float>(visible_height),
        };
    }

    if (!valid_rect(result.source) || !valid_rect(result.destination) ||
        static_cast<double>(result.source.x) + result.source.width > intrinsic_size.width ||
        static_cast<double>(result.source.y) + result.source.height > intrinsic_size.height) {
        return ImageFitResult::invalid_input;
    }
    if (result.destination.width == 0.0F || result.destination.height == 0.0F) {
        return ImageFitResult::empty;
    }

    placement = result;
    return ImageFitResult::complete;
}

}
