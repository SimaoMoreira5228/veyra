#ifndef VEYRA_TESTS_E0_FIXED_SCENE_HPP
#define VEYRA_TESTS_E0_FIXED_SCENE_HPP

#include "veyra/render/software_renderer.hpp"

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <span>

namespace veyra {

// NOTE: synthetic glyph stand-ins. A real TextShaper needs font bytes and
// HarfBuzz; E0 forbids I/O, so two DrawMask commands with fixed advances and
// a fixed 4x3 coverage pattern stand in for shaped runs.

constexpr std::size_t kE0Width = 64;
constexpr std::size_t kE0Height = 48;
constexpr std::size_t kE0BandHeight = 6;
constexpr std::size_t kE0BandCount = 9;
constexpr std::size_t kE0ScrollStep = 8;
constexpr std::size_t kE0FrameCount = 2;
constexpr std::uint64_t kE0Seed = 0x9E3779B97F4A7C15ULL;
constexpr std::uint64_t kE0TimeBaseMs = 100;
constexpr std::uint64_t kE0TimeStepMs = 16;
constexpr std::size_t kE0MaxCommands = 40;

constexpr std::uint64_t e0_splitmix(std::uint64_t state) noexcept {
    state += 0x9E3779B97F4A7C15ULL;

    auto mixed = state;

    mixed = (mixed ^ (mixed >> 30U)) * 0xBF58476D1CE4E5B9ULL;
    mixed = (mixed ^ (mixed >> 27U)) * 0x94D049BB133111EBULL;

    return mixed ^ (mixed >> 31U);
}

constexpr std::uint64_t e0_frame_time_ms(std::size_t frame) noexcept {
    return kE0TimeBaseMs + static_cast<std::uint64_t>(frame) * kE0TimeStepMs;
}

struct E0SceneStorage {
    std::array<DisplayCommand, kE0MaxCommands> commands{};
    std::array<LayoutPoint, 4> polyline_points{};
    std::array<SoftwarePathSegment, 3> path_segments{};
    std::array<SoftwarePathContour, 1> path_contours{};
    std::array<std::uint8_t, 12> mask_coverage{};
    SoftwareMask mask{};
    std::array<SoftwarePixel, 12> image_pixels{};
    SoftwareImage image{};
    std::size_t command_count{};
};

inline Srgba8 e0_color(std::uint64_t& state) noexcept {
    state = e0_splitmix(state);

    return Srgba8{
        .red = static_cast<std::uint8_t>((state >> 8U) & 0xFFU),
        .green = static_cast<std::uint8_t>((state >> 24U) & 0xFFU),
        .blue = static_cast<std::uint8_t>((state >> 40U) & 0xFFU),
        .alpha = 255,
    };
}

// NOTE: pure function of (seed, frame). Same inputs rebuild identical lists.
inline std::span<const DisplayCommand>
e0_build_scene(std::uint64_t seed, std::size_t frame, E0SceneStorage& storage) noexcept {
    auto random = static_cast<std::uint64_t>(seed ^ 0xD1B54A32D192ED03ULL);

    auto palette = std::array<Srgba8, kE0BandCount>{};

    for (auto& color : palette) {
        color = e0_color(random);
    }

    const auto background = e0_color(random);
    const auto accent = e0_color(random);
    const auto line_color = e0_color(random);
    const auto path_color = e0_color(random);
    const auto mask_color = e0_color(random);
    const auto image_tint = e0_color(random);

    const auto scroll = static_cast<float>(frame * kE0ScrollStep);
    const auto shift = static_cast<float>(frame);

    for (std::size_t index = 0; index < storage.mask_coverage.size(); ++index) {
        random = e0_splitmix(random);

        storage.mask_coverage[index] = static_cast<std::uint8_t>(128U + (random & 0x7FU));
    }

    for (std::size_t index = 0; index < storage.image_pixels.size(); ++index) {
        random = e0_splitmix(random);

        storage.image_pixels[index] = SoftwarePixel{
            .red = static_cast<std::uint8_t>((random >> 8U) & 0xFFU),
            .green = static_cast<std::uint8_t>((random >> 24U) & 0xFFU),
            .blue = image_tint.blue,
            .alpha = 255,
        };
    }

    storage.polyline_points[0] = LayoutPoint{.x = 0.0F, .y = 40.0F};
    storage.polyline_points[1] = LayoutPoint{.x = 16.0F, .y = 44.0F};
    storage.polyline_points[2] = LayoutPoint{.x = 32.0F, .y = 40.0F};
    storage.polyline_points[3] = LayoutPoint{.x = 48.0F, .y = 44.0F};

    storage.path_segments[0] = SoftwareLineTo{.end = {.x = 60.0F, .y = 20.0F}};
    storage.path_segments[1] = SoftwareLineTo{.end = {.x = 44.0F, .y = 20.0F}};
    storage.path_segments[2] = SoftwareLineTo{.end = {.x = 52.0F, .y = 4.0F}};

    storage.path_contours[0] = SoftwarePathContour{
        .start = {.x = 52.0F, .y = 4.0F},
        .segments = storage.path_segments,
    };

    storage.mask = SoftwareMask{
        .width = 4,
        .height = 3,
        .stride = 4,
        .coverage = storage.mask_coverage,
        .generation = seed,
    };

    storage.image = SoftwareImage{
        .width = 6,
        .height = 2,
        .stride = 6,
        .pixels = storage.image_pixels,
        .generation = seed,
    };

    auto count = std::size_t{0};
    auto emit = [&count, &storage](DisplayCommand command) noexcept {
        storage.commands[count] = command;

        ++count;
    };

    emit(
        FillRect{
            .bounds =
                {
                    .width = static_cast<float>(kE0Width),
                    .height = static_cast<float>(kE0Height),
                },
            .color = background,
        }
    );

    for (std::size_t band = 0; band < kE0BandCount; ++band) {
        const auto top = static_cast<int>(band * kE0BandHeight) - static_cast<int>(scroll);
        const auto bottom = top + static_cast<int>(kE0BandHeight);

        if (top >= static_cast<int>(kE0Height) || bottom <= 0) {
            continue;
        }

        const auto visible_top = top < 0 ? 0 : top;
        const auto visible_bottom =
            bottom > static_cast<int>(kE0Height) ? static_cast<int>(kE0Height) : bottom;

        emit(
            FillRect{
                .bounds =
                    {
                        .y = static_cast<float>(visible_top),
                        .width = static_cast<float>(kE0Width),
                        .height = static_cast<float>(visible_bottom - visible_top),
                    },
                .color = palette[band],
            }
        );
    }

    emit(PushTranslation{.offset = {.x = 4.0F, .y = shift * 2.0F}});

    emit(
        FillRoundedRect{
            .bounds = {.x = 10.0F, .y = 26.0F, .width = 20.0F, .height = 10.0F},
            .radius = 3.0F,
            .color = accent,
        }
    );

    emit(
        StrokeLine{
            .start = {.x = 12.0F, .y = 28.0F},
            .end = {.x = 28.0F, .y = 34.0F},
            .thickness = 1.5F,
            .color = line_color,
        }
    );

    emit(PopTranslation{});

    emit(
        PushClip{
            .bounds = {.x = 8.0F, .y = 30.0F, .width = 48.0F, .height = 12.0F},
            .clip_id = 1000U + (seed & 0xFFU),
        }
    );

    emit(
        FillRect{
            .bounds = {.x = 8.0F, .y = 30.0F, .width = 48.0F, .height = 12.0F},
            .color = accent,
        }
    );

    emit(
        StrokeRect{
            .bounds = {.x = 10.0F, .y = 32.0F, .width = 44.0F, .height = 8.0F},
            .thickness = 1.0F,
            .color = line_color,
        }
    );

    emit(PopClip{});

    emit(
        StrokeRoundedRect{
            .bounds = {.x = 40.0F, .y = 8.0F, .width = 16.0F, .height = 12.0F},
            .radius = 3.0F,
            .thickness = 1.0F,
            .color = line_color,
        }
    );

    emit(
        StrokePolyline{
            .points = storage.polyline_points,
            .thickness = 1.5F,
            .color = line_color,
            .closed = false,
        }
    );

    emit(
        FillPath{
            .contours = std::span<const SoftwarePathContour>{storage.path_contours.data(), 1},
            .color = path_color,
            .fill_rule = SoftwarePathFillRule::even_odd,
        }
    );

    emit(
        DrawMask{
            .x = 2.0F + shift,
            .y = 44.0F,
            .mask = &storage.mask,
            .color = mask_color,
        }
    );

    emit(
        DrawMask{
            .x = 10.0F + shift,
            .y = 44.0F,
            .mask = &storage.mask,
            .color = mask_color,
        }
    );

    emit(
        DrawImage{
            .source =
                {
                    .width = 6.0F,
                    .height = 2.0F,
                },
            .destination =
                {
                    .x = 52.0F,
                    .y = 36.0F,
                    .width = 6.0F,
                    .height = 2.0F,
                },
            .image = &storage.image,
            .sampling = SoftwareImageSampling::nearest,
            .opacity = 255,
        }
    );

    storage.command_count = count;

    return std::span<const DisplayCommand>{storage.commands.data(), count};
}

inline std::uint64_t e0_mix_hash(std::uint64_t hash, std::uint64_t value) noexcept {
    hash ^= value;
    hash *= 1099511628211ULL;

    return hash;
}

inline std::uint64_t e0_hash_float(std::uint64_t hash, float value) noexcept {
    return e0_mix_hash(hash, std::bit_cast<std::uint32_t>(value));
}

inline std::uint64_t e0_mix_rect(std::uint64_t hash, LayoutRect rect) noexcept {
    hash = e0_hash_float(hash, rect.x);
    hash = e0_hash_float(hash, rect.y);
    hash = e0_hash_float(hash, rect.width);

    return e0_hash_float(hash, rect.height);
}

inline std::uint64_t e0_mix_point(std::uint64_t hash, LayoutPoint point) noexcept {
    hash = e0_hash_float(hash, point.x);

    return e0_hash_float(hash, point.y);
}

inline std::uint64_t e0_mix_color(std::uint64_t hash, Srgba8 color) noexcept {
    hash = e0_mix_hash(hash, color.red);
    hash = e0_mix_hash(hash, color.green);
    hash = e0_mix_hash(hash, color.blue);

    return e0_mix_hash(hash, color.alpha);
}

inline std::uint64_t
e0_mix_segment(std::uint64_t hash, const SoftwarePathSegment& segment) noexcept {
    hash = e0_mix_hash(hash, segment.index());

    if (const auto* line = std::get_if<SoftwareLineTo>(&segment)) {
        return e0_mix_point(hash, line->end);
    }

    if (const auto* quad = std::get_if<SoftwareQuadraticTo>(&segment)) {
        hash = e0_mix_point(hash, quad->control);

        return e0_mix_point(hash, quad->end);
    }

    if (const auto* cubic = std::get_if<SoftwareCubicTo>(&segment)) {
        hash = e0_mix_point(hash, cubic->control_1);
        hash = e0_mix_point(hash, cubic->control_2);

        return e0_mix_point(hash, cubic->end);
    }

    return hash;
}

inline std::uint64_t e0_mix_mask(std::uint64_t hash, const SoftwareMask& mask) noexcept {
    hash = e0_mix_hash(hash, mask.width);
    hash = e0_mix_hash(hash, mask.height);

    for (const auto byte : mask.coverage) {
        hash = e0_mix_hash(hash, byte);
    }

    return hash;
}

inline std::uint64_t e0_mix_image(std::uint64_t hash, const SoftwareImage& image) noexcept {
    for (const auto& pixel : image.pixels) {
        hash = e0_mix_hash(hash, pixel.red);
        hash = e0_mix_hash(hash, pixel.green);
        hash = e0_mix_hash(hash, pixel.blue);
        hash = e0_mix_hash(hash, pixel.alpha);
    }

    return hash;
}

// NOTE: content hash, not pointer hash. Two runs rebuild into different
// backing stores, so resource identity must come from bytes, not addresses.
inline std::uint64_t e0_hash_command(std::uint64_t hash, const DisplayCommand& command) noexcept {
    hash = e0_mix_hash(hash, command.index());

    if (const auto* value = std::get_if<FillRect>(&command)) {
        return e0_mix_color(e0_mix_rect(hash, value->bounds), value->color);
    }

    if (const auto* value = std::get_if<FillRoundedRect>(&command)) {
        hash = e0_mix_rect(hash, value->bounds);
        hash = e0_hash_float(hash, value->radius);

        return e0_mix_color(hash, value->color);
    }

    if (const auto* value = std::get_if<StrokeRect>(&command)) {
        hash = e0_mix_rect(hash, value->bounds);
        hash = e0_hash_float(hash, value->thickness);

        return e0_mix_color(hash, value->color);
    }

    if (const auto* value = std::get_if<StrokeRoundedRect>(&command)) {
        hash = e0_mix_rect(hash, value->bounds);
        hash = e0_hash_float(hash, value->radius);
        hash = e0_hash_float(hash, value->thickness);

        return e0_mix_color(hash, value->color);
    }

    if (const auto* value = std::get_if<StrokeLine>(&command)) {
        hash = e0_mix_point(hash, value->start);
        hash = e0_mix_point(hash, value->end);
        hash = e0_hash_float(hash, value->thickness);

        return e0_mix_color(hash, value->color);
    }

    if (const auto* value = std::get_if<StrokePolyline>(&command)) {
        for (const auto& point : value->points) {
            hash = e0_mix_point(hash, point);
        }

        hash = e0_hash_float(hash, value->thickness);
        hash = e0_mix_hash(hash, value->closed ? 1U : 0U);

        return e0_mix_color(hash, value->color);
    }

    if (const auto* value = std::get_if<DrawMask>(&command)) {
        hash = e0_hash_float(hash, value->x);
        hash = e0_hash_float(hash, value->y);
        hash = e0_mix_mask(hash, *value->mask);

        return e0_mix_color(hash, value->color);
    }

    if (const auto* value = std::get_if<FillPath>(&command)) {
        for (const auto& contour : value->contours) {
            hash = e0_mix_point(hash, contour.start);

            for (const auto& segment : contour.segments) {
                hash = e0_mix_segment(hash, segment);
            }
        }

        hash = e0_mix_hash(hash, static_cast<std::uint64_t>(value->fill_rule));

        return e0_mix_color(hash, value->color);
    }

    if (const auto* value = std::get_if<DrawImage>(&command)) {
        hash = e0_mix_rect(hash, value->source);
        hash = e0_mix_rect(hash, value->destination);
        hash = e0_mix_image(hash, *value->image);
        hash = e0_mix_hash(hash, static_cast<std::uint64_t>(value->sampling));

        return e0_mix_hash(hash, value->opacity);
    }

    if (const auto* value = std::get_if<PushTranslation>(&command)) {
        return e0_mix_point(hash, value->offset);
    }

    if (const auto* value = std::get_if<PushClip>(&command)) {
        hash = e0_mix_rect(hash, value->bounds);

        return e0_mix_hash(hash, value->clip_id);
    }

    return hash;
}

inline std::uint64_t e0_hash_commands(std::span<const DisplayCommand> commands) noexcept {
    auto hash = std::uint64_t{14695981039346656037ULL};

    for (const auto& command : commands) {
        hash = e0_hash_command(hash, command);
    }

    return e0_mix_hash(hash, commands.size());
}

}

#endif
