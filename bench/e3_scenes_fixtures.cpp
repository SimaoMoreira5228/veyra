#include "e3_scenes_fixtures.hpp"
#include "veyra/render/display_command_hash.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace veyra {
namespace {

constexpr std::uint64_t kStreamStride = 0x9E3779B97F4A7C15ULL;
constexpr std::uint64_t kVeilStride = 0xC2B2AE3D27D4EB4FULL;

std::uint64_t e3_splitmix(std::uint64_t state) noexcept {
    state += 0x9E3779B97F4A7C15ULL;

    auto mixed = state;

    mixed = (mixed ^ (mixed >> 30U)) * 0xBF58476D1CE4E5B9ULL;
    mixed = (mixed ^ (mixed >> 27U)) * 0x94D049BB133111EBULL;

    return mixed ^ (mixed >> 31U);
}

Srgba8 e3_color(std::uint64_t& stream) noexcept {
    stream = e3_splitmix(stream);

    return Srgba8{
        .red = static_cast<std::uint8_t>((stream >> 8U) & 0xFFU),
        .green = static_cast<std::uint8_t>((stream >> 24U) & 0xFFU),
        .blue = static_cast<std::uint8_t>((stream >> 40U) & 0xFFU),
        .alpha = 255,
    };
}

void setup_tiles(std::uint64_t seed, E3Scratch& scratch) noexcept {
    auto stream = seed ^ 0xD1B54A32D192ED03ULL;

    for (auto& byte : scratch.mask_coverage) {
        stream = e3_splitmix(stream);

        byte = static_cast<std::uint8_t>(96U + (stream & 0x7FU));
    }

    for (auto& pixel : scratch.image_pixels) {
        stream = e3_splitmix(stream);

        pixel = SoftwarePixel{
            .red = static_cast<std::uint8_t>((stream >> 8U) & 0xFFU),
            .green = static_cast<std::uint8_t>((stream >> 24U) & 0xFFU),
            .blue = static_cast<std::uint8_t>((stream >> 40U) & 0xFFU),
            .alpha = 255,
        };
    }

    const auto coverage = std::span<std::uint8_t>(scratch.mask_coverage);
    const auto pixels = std::span<SoftwarePixel>(scratch.image_pixels);

    scratch.masks[0] = SoftwareMask{
        .width = 6,
        .height = 12,
        .stride = 6,
        .coverage = coverage.subspan(0, 72),
        .generation = seed,
    };

    scratch.masks[1] = SoftwareMask{
        .width = 10,
        .height = 12,
        .stride = 10,
        .coverage = coverage.subspan(72, 120),
        .generation = seed,
    };

    scratch.masks[2] = SoftwareMask{
        .width = 8,
        .height = 12,
        .stride = 8,
        .coverage = coverage.subspan(192, 96),
        .generation = seed,
    };

    scratch.images[0] = SoftwareImage{
        .width = 8,
        .height = 8,
        .stride = 8,
        .pixels = pixels.subspan(0, 64),
        .generation = seed,
    };

    scratch.images[1] = SoftwareImage{
        .width = 16,
        .height = 16,
        .stride = 16,
        .pixels = pixels.subspan(64, 256),
        .generation = seed ^ 1U,
    };
}

void build_shadowed_boxes(std::uint64_t seed, DisplayCommand* out) noexcept {
    auto index = std::size_t{};

    out[index] = FillRect{
        .bounds = {.width = static_cast<float>(kE3Width), .height = static_cast<float>(kE3Height)},
        .color = {.red = 24, .green = 26, .blue = 32, .alpha = 255},
    };

    ++index;

    constexpr auto columns = std::size_t{30};
    constexpr auto box_count = std::size_t{600};

    for (std::size_t box = 0; box < box_count; ++box) {
        const auto x = static_cast<float>(box % columns * 42 + 2);
        const auto y = static_cast<float>(box / columns * 36 + 2);

        auto stream = seed ^ (static_cast<std::uint64_t>(box) * kStreamStride);
        const auto card = e3_color(stream);

        out[index] = FillRoundedRect{
            .bounds = {.x = x + 2.0F, .y = y + 3.0F, .width = 38.0F, .height = 30.0F},
            .radius = 6.0F,
            .color = {.red = 0, .green = 0, .blue = 0, .alpha = 90},
        };

        ++index;

        out[index] = FillRect{
            .bounds = {.x = x, .y = y, .width = 38.0F, .height = 30.0F},
            .color = card,
        };

        ++index;
    }
}

void build_row_scroll(
    std::uint64_t seed,
    std::size_t frame,
    const E3Scratch& scratch,
    DisplayCommand* out
) noexcept {
    auto index = std::size_t{};

    out[index] = FillRect{
        .bounds = {.width = static_cast<float>(kE3Width), .height = static_cast<float>(kE3Height)},
        .color = {.red = 240, .green = 240, .blue = 244, .alpha = 255},
    };

    ++index;

    constexpr auto visible_rows = std::size_t{31};
    constexpr auto row_height = 24.0F;

    const auto base = (frame * 3) % 970;

    for (std::size_t slot = 0; slot < visible_rows; ++slot) {
        const auto row = base + slot;
        const auto y = static_cast<float>(slot) * row_height;

        auto stream = seed ^ (static_cast<std::uint64_t>(row) * kStreamStride);
        const auto accent = e3_color(stream);

        const auto shaded = (row % 2U) == 0U;

        out[index] = FillRect{
            .bounds = {.y = y, .width = static_cast<float>(kE3Width), .height = row_height},
            .color = shaded ? Srgba8{.red = 228, .green = 228, .blue = 234, .alpha = 255}
                            : Srgba8{.red = 240, .green = 240, .blue = 244, .alpha = 255},
        };

        ++index;

        out[index] = DrawMask{
            .x = 8.0F,
            .y = y + 6.0F,
            .mask = &scratch.masks[row % 3U],
            .color = {.red = 32, .green = 32, .blue = 40, .alpha = 255},
        };

        ++index;

        out[index] = FillRect{
            .bounds = {.x = 1264.0F, .y = y + 8.0F, .width = 8.0F, .height = 8.0F},
            .color = accent,
        };

        ++index;
    }
}

void build_virtual_rows(
    std::uint64_t seed,
    std::size_t frame,
    const E3Scratch& scratch,
    DisplayCommand* out
) noexcept {
    auto index = std::size_t{};

    out[index] = FillRect{
        .bounds = {.width = static_cast<float>(kE3Width), .height = static_cast<float>(kE3Height)},
        .color = {.red = 250, .green = 250, .blue = 252, .alpha = 255},
    };

    ++index;

    constexpr auto visible_rows = std::size_t{40};
    constexpr auto row_height = 20.0F;

    const auto base = (frame * 128) % 99960;

    for (std::size_t slot = 0; slot < visible_rows; ++slot) {
        const auto row = base + slot;
        const auto y = static_cast<float>(slot) * row_height;

        auto stream = seed ^ (static_cast<std::uint64_t>(row) * kStreamStride);
        const auto badge = e3_color(stream);

        const auto shaded = (row % 2U) == 0U;

        out[index] = FillRect{
            .bounds = {.y = y, .width = static_cast<float>(kE3Width), .height = row_height},
            .color = shaded ? Srgba8{.red = 238, .green = 240, .blue = 246, .alpha = 255}
                            : Srgba8{.red = 250, .green = 250, .blue = 252, .alpha = 255},
        };

        ++index;

        out[index] = DrawMask{
            .x = 12.0F,
            .y = y + 4.0F,
            .mask = &scratch.masks[row % 3U],
            .color = {.red = 28, .green = 30, .blue = 38, .alpha = 255},
        };

        ++index;

        out[index] = FillRoundedRect{
            .bounds = {.x = 1216.0F, .y = y + 3.0F, .width = 56.0F, .height = 14.0F},
            .radius = 7.0F,
            .color = badge,
        };

        ++index;
    }
}

void build_dashboard(
    std::uint64_t seed,
    std::size_t frame,
    E3Scratch& scratch,
    DisplayCommand* out
) noexcept {
    auto index = std::size_t{};

    out[index] = FillRect{
        .bounds = {.width = static_cast<float>(kE3Width), .height = static_cast<float>(kE3Height)},
        .color = {.red = 18, .green = 20, .blue = 28, .alpha = 255},
    };

    ++index;

    constexpr auto card_count = std::size_t{12};

    for (std::size_t card = 0; card < card_count; ++card) {
        const auto origin_x = static_cast<float>(card % 4 * 310 + 10);
        const auto origin_y = static_cast<float>(card / 4 * 230 + 10);

        auto stream = seed ^ (static_cast<std::uint64_t>(card) * kStreamStride);
        const auto line = e3_color(stream);
        const auto fill = e3_color(stream);

        out[index] = StrokeRect{
            .bounds = {.x = origin_x, .y = origin_y, .width = 300.0F, .height = 220.0F},
            .thickness = 1.0F,
            .color = line,
        };

        ++index;

        out[index] = FillRect{
            .bounds = {.x = origin_x, .y = origin_y, .width = 300.0F, .height = 28.0F},
            .color = fill,
        };

        ++index;

        for (std::size_t tile = 0; tile < 6; ++tile) {
            out[index] = DrawMask{
                .x = origin_x + 12.0F + static_cast<float>(tile * 10),
                .y = origin_y + 44.0F,
                .mask = &scratch.masks[0],
                .color = {.red = 230, .green = 232, .blue = 240, .alpha = 255},
            };

            ++index;
        }

        const auto first_point = card * 8;

        for (std::size_t point = 0; point < 8; ++point) {
            stream = e3_splitmix(stream);

            scratch.polyline_points[first_point + point] = LayoutPoint{
                .x = origin_x + 12.0F + static_cast<float>(point * 40),
                .y = origin_y + 170.0F - static_cast<float>(stream % 80U),
            };
        }

        out[index] = StrokePolyline{
            .points = std::span<const LayoutPoint>(scratch.polyline_points).subspan(first_point, 8),
            .thickness = 2.0F,
            .color = line,
            .closed = false,
        };

        ++index;

        const auto bar_width = 40.0F + static_cast<float>((frame * 13 + card) % 200U);

        out[index] = FillRect{
            .bounds =
                {.x = origin_x + 12.0F,
                 .y = origin_y + 186.0F,
                 .width = bar_width,
                 .height = 10.0F},
            .color = fill,
        };

        ++index;

        out[index] = FillRect{
            .bounds = {.x = origin_x, .y = origin_y + 204.0F, .width = 300.0F, .height = 16.0F},
            .color = {.red = 30, .green = 33, .blue = 44, .alpha = 255},
        };

        ++index;
    }
}

void build_changing_text(std::uint64_t seed, std::size_t frame, DisplayCommand* out) noexcept {
    auto index = std::size_t{};

    out[index] = FillRect{
        .bounds = {.width = static_cast<float>(kE3Width), .height = static_cast<float>(kE3Height)},
        .color = {.red = 255, .green = 255, .blue = 255, .alpha = 255},
    };

    ++index;

    constexpr auto glyph_count = std::size_t{40};

    auto cursor = 8.0F;
    auto caret_x = 8.0F;
    const auto caret = frame % 41;

    for (std::size_t glyph = 0; glyph < glyph_count; ++glyph) {
        auto stream = seed ^ (static_cast<std::uint64_t>(glyph) * kStreamStride);
        stream = e3_splitmix(stream ^ frame);

        const auto width = 8.0F + static_cast<float>(stream % 9U);

        if (glyph == caret) {
            caret_x = cursor;
        }

        out[index] = FillRect{
            .bounds = {.x = cursor, .y = 340.0F, .width = width, .height = 18.0F},
            .color = {.red = 30, .green = 30, .blue = 36, .alpha = 255},
        };

        ++index;

        cursor += width + 3.0F;
    }

    if (caret == glyph_count) {
        caret_x = cursor;
    }

    out[index] = StrokeRect{
        .bounds = {.x = caret_x, .y = 336.0F, .width = 2.0F, .height = 22.0F},
        .thickness = 2.0F,
        .color = {
            .red = 20,
            .green = 110,
            .blue = 230,
            .alpha = static_cast<std::uint8_t>(frame % 2U == 0U ? 255U : 0U)
        },
    };

    ++index;

    const auto composition = static_cast<float>(frame % 40);

    out[index] = StrokeRect{
        .bounds = {.x = 8.0F + composition * 17.0F, .y = 362.0F, .width = 102.0F, .height = 2.0F},
        .thickness = 2.0F,
        .color = {.red = 20, .green = 110, .blue = 230, .alpha = 255},
    };

    ++index;
}

void build_chat_cjk_arabic(
    std::uint64_t seed,
    std::size_t frame,
    const E3Scratch& scratch,
    DisplayCommand* out
) noexcept {
    auto index = std::size_t{};

    out[index] = FillRect{
        .bounds = {.width = static_cast<float>(kE3Width), .height = static_cast<float>(kE3Height)},
        .color = {.red = 246, .green = 247, .blue = 250, .alpha = 255},
    };

    ++index;

    constexpr auto visible_messages = std::size_t{12};

    const auto base = frame % 244;

    for (std::size_t slot = 0; slot < visible_messages; ++slot) {
        const auto message = base + slot;
        const auto y = static_cast<float>(slot * 58 + 8);

        auto stream = seed ^ (static_cast<std::uint64_t>(message) * kStreamStride);
        const auto avatar = e3_color(stream);

        out[index] = FillRoundedRect{
            .bounds = {.x = 12.0F, .y = y, .width = 32.0F, .height = 32.0F},
            .radius = 16.0F,
            .color = avatar,
        };

        ++index;

        out[index] = DrawMask{
            .x = 56.0F,
            .y = y,
            .mask = &scratch.masks[0],
            .color = {.red = 120, .green = 122, .blue = 132, .alpha = 255},
        };

        ++index;

        out[index] = DrawMask{
            .x = 56.0F,
            .y = y + 18.0F,
            .mask = &scratch.masks[(message + frame) % 3U],
            .color = {.red = 30, .green = 31, .blue = 38, .alpha = 255},
        };

        ++index;

        out[index] = DrawMask{
            .x = 220.0F,
            .y = y + 18.0F,
            .mask = &scratch.masks[(message + frame + 1U) % 3U],
            .color = {.red = 30, .green = 31, .blue = 38, .alpha = 255},
        };

        ++index;

        if (message % 2U == 0U) {
            out[index] = FillRoundedRect{
                .bounds = {.x = 420.0F, .y = y, .width = 40.0F, .height = 14.0F},
                .radius = 7.0F,
                .color = {.red = 230, .green = 236, .blue = 250, .alpha = 255},
            };
        } else {
            out[index] = FillRoundedRect{
                .bounds = {.x = 420.0F, .y = y + 36.0F, .width = 40.0F, .height = 2.0F},
                .radius = 1.0F,
                .color = {.red = 246, .green = 247, .blue = 250, .alpha = 255},
            };
        }

        ++index;
    }
}

void build_translucent_overlays(
    std::uint64_t seed,
    std::size_t frame,
    const E3Scratch& scratch,
    DisplayCommand* out
) noexcept {
    auto index = std::size_t{};

    out[index] = FillRect{
        .bounds = {.width = static_cast<float>(kE3Width), .height = static_cast<float>(kE3Height)},
        .color = {.red = 16, .green = 18, .blue = 26, .alpha = 255},
    };

    ++index;

    for (std::size_t cell = 0; cell < 48; ++cell) {
        const auto x = static_cast<float>(cell % 8 * 155 + 10);
        const auto y = static_cast<float>(cell / 8 * 115 + 10);

        auto stream = seed ^ (static_cast<std::uint64_t>(cell) * kStreamStride);
        const auto fill = e3_color(stream);

        out[index] = FillRect{
            .bounds = {.x = x, .y = y, .width = 150.0F, .height = 110.0F},
            .color = fill,
        };

        ++index;
    }

    for (std::size_t veil = 0; veil < 24; ++veil) {
        const auto x = static_cast<float>(veil % 6 * 200 + 75);
        const auto y = static_cast<float>(veil / 6 * 160 + 55);

        auto stream = seed ^ (static_cast<std::uint64_t>(veil) * kVeilStride);
        const auto tint = e3_color(stream);

        out[index] = FillRect{
            .bounds = {.x = x, .y = y, .width = 180.0F, .height = 120.0F},
            .color = {.red = tint.red, .green = tint.green, .blue = tint.blue, .alpha = 140},
        };

        ++index;
    }

    for (std::size_t badge = 0; badge < 4; ++badge) {
        const auto x = static_cast<float>(badge * 300 + 40);
        const auto y = static_cast<float>(badge * 120 + 60);

        out[index] = DrawImage{
            .source = {.width = 16.0F, .height = 16.0F},
            .destination = {.x = x, .y = y, .width = 64.0F, .height = 64.0F},
            .image = &scratch.images[1],
            .sampling = SoftwareImageSampling::linear,
            .opacity = 128,
        };

        ++index;
    }

    out[index] = FillRect{
        .bounds = {.width = static_cast<float>(kE3Width), .height = static_cast<float>(kE3Height)},
        .color = {
            .red = 8,
            .green = 10,
            .blue = 18,
            .alpha = static_cast<std::uint8_t>(96 + frame % 64U)
        },
    };

    ++index;
}

void build_explorer(
    std::uint64_t seed,
    std::size_t frame,
    const E3Scratch& scratch,
    DisplayCommand* out
) noexcept {
    auto index = std::size_t{};

    out[index] = FillRect{
        .bounds = {.width = static_cast<float>(kE3Width), .height = static_cast<float>(kE3Height)},
        .color = {.red = 255, .green = 255, .blue = 255, .alpha = 255},
    };

    ++index;

    out[index] = FillRect{
        .bounds = {.width = static_cast<float>(kE3Width), .height = 28.0F},
        .color = {.red = 236, .green = 238, .blue = 244, .alpha = 255},
    };

    ++index;

    out[index] = StrokeRect{
        .bounds = {.x = 880.0F, .y = 8.0F, .width = 12.0F, .height = 12.0F},
        .thickness = 2.0F,
        .color = {.red = 90, .green = 92, .blue = 104, .alpha = 255},
    };

    ++index;

    constexpr auto visible_rows = std::size_t{40};

    const auto base = (frame * 7) % 999960;
    const auto selected = frame % visible_rows;

    for (std::size_t slot = 0; slot < visible_rows; ++slot) {
        const auto entry = base + slot;
        const auto y = 28.0F + static_cast<float>(slot * 17);

        const auto is_selected = slot == selected;

        out[index] = FillRect{
            .bounds = {.y = y, .width = static_cast<float>(kE3Width), .height = 17.0F},
            .color = is_selected ? Srgba8{.red = 210, .green = 226, .blue = 250, .alpha = 255}
                                 : Srgba8{.red = 255, .green = 255, .blue = 255, .alpha = 255},
        };

        ++index;

        out[index] = DrawImage{
            .source = {.width = 8.0F, .height = 8.0F},
            .destination = {.x = 4.0F, .y = y + 1.0F, .width = 15.0F, .height = 15.0F},
            .image = &scratch.images[0],
            .sampling = SoftwareImageSampling::nearest,
            .opacity = 255,
        };

        ++index;

        out[index] = DrawMask{
            .x = 28.0F,
            .y = y + 3.0F,
            .mask = &scratch.masks[entry % 3U],
            .color = {.red = 30, .green = 31, .blue = 38, .alpha = 255},
        };

        ++index;

        out[index] = DrawMask{
            .x = 900.0F,
            .y = y + 3.0F,
            .mask = &scratch.masks[0],
            .color = {.red = 120, .green = 122, .blue = 132, .alpha = 255},
        };

        ++index;
    }
}

}

std::string_view e3_scene_name(E3Scene scene) noexcept {
    switch (scene) {
    case E3Scene::shadowed_boxes_600:
        return "shadowed-boxes-600";
    case E3Scene::row_scroll_1000:
        return "row-scroll-1000";
    case E3Scene::virtual_rows_100k:
        return "virtual-rows-100k";
    case E3Scene::dashboard_12:
        return "dashboard-12";
    case E3Scene::changing_text:
        return "changing-text";
    case E3Scene::chat_cjk_arabic:
        return "chat-cjk-arabic";
    case E3Scene::translucent_overlays:
        return "translucent-overlays";
    case E3Scene::explorer_1m:
        return "explorer-1m";
    }

    return "invalid-scene";
}

bool e3_parse_scene(std::string_view name, E3Scene& scene) noexcept {
    if (name == "shadowed-boxes-600") {
        scene = E3Scene::shadowed_boxes_600;

        return true;
    }

    if (name == "row-scroll-1000") {
        scene = E3Scene::row_scroll_1000;

        return true;
    }

    if (name == "virtual-rows-100k") {
        scene = E3Scene::virtual_rows_100k;

        return true;
    }

    if (name == "dashboard-12") {
        scene = E3Scene::dashboard_12;

        return true;
    }

    if (name == "changing-text") {
        scene = E3Scene::changing_text;

        return true;
    }

    if (name == "chat-cjk-arabic") {
        scene = E3Scene::chat_cjk_arabic;

        return true;
    }

    if (name == "translucent-overlays") {
        scene = E3Scene::translucent_overlays;

        return true;
    }

    if (name == "explorer-1m") {
        scene = E3Scene::explorer_1m;

        return true;
    }

    return false;
}

E3BuildResult e3_build_scene(
    E3Scene scene,
    std::uint64_t seed,
    std::size_t frame,
    std::span<DisplayCommand> out,
    E3Scratch& scratch
) noexcept {
    const auto needed = e3_max_commands(scene);

    if (needed == 0) {
        return E3BuildResult{.status = E3BuildStatus::invalid_scene};
    }

    if (out.size() < needed) {
        return E3BuildResult{.status = E3BuildStatus::truncated};
    }

    setup_tiles(seed, scratch);

    switch (scene) {
    case E3Scene::shadowed_boxes_600:
        build_shadowed_boxes(seed, out.data());

        break;
    case E3Scene::row_scroll_1000:
        build_row_scroll(seed, frame, scratch, out.data());

        break;
    case E3Scene::virtual_rows_100k:
        build_virtual_rows(seed, frame, scratch, out.data());

        break;
    case E3Scene::dashboard_12:
        build_dashboard(seed, frame, scratch, out.data());

        break;
    case E3Scene::changing_text:
        build_changing_text(seed, frame, out.data());

        break;
    case E3Scene::chat_cjk_arabic:
        build_chat_cjk_arabic(seed, frame, scratch, out.data());

        break;
    case E3Scene::translucent_overlays:
        build_translucent_overlays(seed, frame, scratch, out.data());

        break;
    case E3Scene::explorer_1m:
        build_explorer(seed, frame, scratch, out.data());

        break;
    }

    const auto built = std::span<DisplayCommand>(out.data(), needed);

    return E3BuildResult{
        .status = E3BuildStatus::complete,
        .produced = needed,
        .content_hash = hash_display_commands(built),
    };
}

}
