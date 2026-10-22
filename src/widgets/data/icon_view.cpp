#include "veyra/widgets/data/icon_view.hpp"

#include <cmath>

namespace veyra {
namespace {

constexpr float kOutlineThickness = 2.0F;

bool valid_box(LayoutRect rect) noexcept {
    if (!std::isfinite(rect.x) || !std::isfinite(rect.y)) {
        return false;
    }

    if (!std::isfinite(rect.width) || !std::isfinite(rect.height)) {
        return false;
    }

    return rect.width > 0.0F && rect.height > 0.0F;
}

bool valid_fit(ImageFitMode fit) noexcept {
    return fit == ImageFitMode::fill || fit == ImageFitMode::contain ||
        fit == ImageFitMode::cover || fit == ImageFitMode::none || fit == ImageFitMode::scale_down;
}

bool push_command(
    std::span<DisplayCommand> output,
    std::size_t& placed,
    DisplayCommand command
) noexcept {
    if (placed >= output.size()) {
        return false;
    }

    output[placed] = command;
    ++placed;

    return true;
}

std::size_t badge_digits(std::uint32_t count) noexcept {
    if (count > kIconBadgeCap) {
        return kIconBadgeTextMax;
    }

    if (count > 9) {
        return 2;
    }

    return 1;
}

const SoftwareImage* pick_variant(const IconContent& content) noexcept {
    const auto* preferred = content.dark_ui ? content.dark : content.light;
    const auto* fallback = content.dark_ui ? content.light : content.dark;

    return preferred != nullptr ? preferred : fallback;
}

}

IconAppearance icon_appearance(const Theme& theme, IconStyle style) noexcept {
    auto box = theme.color(tokens::color_surface);

    if (style.tintable) {
        box = theme.color(tokens::color_action_disabled);
    }

    auto badge = theme.color(tokens::color_surface);

    if (style.has_badge) {
        badge = theme.color(tokens::color_error);
    }

    return {
        .box = box,
        .badge = badge,
        .outline = theme.color(tokens::color_action_primary),
    };
}

std::size_t icon_badge_text(std::uint32_t count, std::span<char> out) noexcept {
    if (count == 0) {
        return 0;
    }

    if (count > kIconBadgeCap) {
        if (out.size() < kIconBadgeTextMax) {
            return 0;
        }

        out[0] = '9';
        out[1] = '9';
        out[2] = '+';

        return kIconBadgeTextMax;
    }

    auto digits = std::size_t{};
    auto rest = count;

    do {
        ++digits;
        rest /= 10;
    } while (rest > 0);

    if (out.size() < digits) {
        return 0;
    }

    for (auto index = digits; index > 0; --index) {
        out[index - 1] = static_cast<char>('0' + (count % 10));
        count /= 10;
    }

    return digits;
}

LayoutRect icon_badge_rect(const Theme& theme, LayoutRect box, std::uint32_t count) noexcept {
    if (count == 0) {
        return {};
    }

    if (!valid_box(box)) {
        return {};
    }

    const auto unit = theme.space(tokens::space_small).value;
    const auto height = unit * 4.0F;
    const auto width = unit * 2.0F * static_cast<float>(badge_digits(count)) + unit * 2.0F;
    const auto pad = theme.space(tokens::space_extra_small).value;

    return {
        .x = box.x + box.width - width - pad,
        .y = box.y + pad,
        .width = width,
        .height = height,
    };
}

IconOutput emit_icon(
    const Theme& theme,
    const IconContent& content,
    const IconGeometry& geometry,
    std::span<DisplayCommand> commands_out
) noexcept {
    if (!valid_box(geometry.box) || !valid_fit(geometry.fit)) {
        return {
            .result = IconEmitResult::invalid_input,
        };
    }

    const auto appearance = icon_appearance(
        theme,
        {.tintable = content.tintable, .has_badge = content.badge_count > 0}
    );
    const auto* image = pick_variant(content);
    const auto wants_badge = content.badge_count > 0;
    const auto required = std::size_t{1} + (wants_badge ? std::size_t{1} : std::size_t{}) +
        (content.selected ? std::size_t{1} : std::size_t{});

    if (image == nullptr) {
        auto placed = std::size_t{};

        if (!push_command(
                commands_out,
                placed,
                FillRect{.bounds = geometry.box, .color = appearance.box}
            )) {
            return {
                .placed = placed,
                .required = required,
                .result = IconEmitResult::insufficient_output,
            };
        }

        if (content.selected) {
            if (!push_command(
                    commands_out,
                    placed,
                    StrokeRect{
                        .bounds = geometry.box,
                        .thickness = kOutlineThickness,
                        .color = appearance.outline,
                    }
                )) {
                return {
                    .placed = placed,
                    .required = required,
                    .result = IconEmitResult::insufficient_output,
                };
            }
        }

        return {
            .placed = placed,
            .required = required,
            .result = IconEmitResult::complete,
        };
    }

    if (image->width == 0 || image->height == 0) {
        return {
            .result = IconEmitResult::invalid_input,
        };
    }

    ImagePlacement placement{};
    const auto fit_result = fit_image(
        geometry.box,
        {.width = static_cast<float>(image->width), .height = static_cast<float>(image->height)},
        geometry.fit,
        placement
    );

    if (fit_result != ImageFitResult::complete) {
        return {
            .result = IconEmitResult::invalid_input,
        };
    }

    const auto badge_rect =
        wants_badge ? icon_badge_rect(theme, geometry.box, content.badge_count) : LayoutRect{};
    const auto radius = theme.radius(tokens::radius_small).value;

    auto placed = std::size_t{};
    auto exhausted = false;

    auto push = [&](DisplayCommand command) noexcept {
        if (exhausted) {
            return;
        }

        if (!push_command(commands_out, placed, command)) {
            exhausted = true;
        }
    };

    push(
        DrawImage{
            .source = placement.source,
            .destination = placement.destination,
            .image = image,
            .sampling = content.sampling,
        }
    );

    if (wants_badge) {
        push(
            FillRoundedRect{
                .bounds = badge_rect,
                .radius = radius,
                .color = appearance.badge,
            }
        );
    }

    if (content.selected) {
        push(
            StrokeRect{
                .bounds = geometry.box,
                .thickness = kOutlineThickness,
                .color = appearance.outline,
            }
        );
    }

    if (exhausted) {
        return {
            .placed = placed,
            .required = required,
            .badge_rect = badge_rect,
            .result = IconEmitResult::insufficient_output,
        };
    }

    return {
        .placed = placed,
        .required = required,
        .badge_rect = badge_rect,
        .result = IconEmitResult::complete,
    };
}

}
