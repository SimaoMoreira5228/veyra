#include "veyra/render/software_renderer.hpp"

#include <array>
#include <iostream>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

void require(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

bool pixel_is(
    const veyra::SoftwarePixel& pixel,
    std::uint8_t red,
    std::uint8_t green,
    std::uint8_t blue,
    std::uint8_t alpha = 255
) {
    return pixel.red == red && pixel.green == green && pixel.blue == blue && pixel.alpha == alpha;
}

void check_overlapping_damage_is_rejected() {
    std::array<veyra::SoftwarePixel, 3> pixels{
        veyra::SoftwarePixel{.red = 1, .green = 2, .blue = 3},
        veyra::SoftwarePixel{.red = 4, .green = 5, .blue = 6},
        veyra::SoftwarePixel{.red = 7, .green = 8, .blue = 9},
    };
    const auto before = pixels;
    const std::array<veyra::DisplayCommand, 1> commands{
        veyra::FillRect{
            .bounds = {.width = 3.0F, .height = 1.0F},
            .color = {.red = 100, .green = 110, .blue = 120, .alpha = 128},
        },
    };
    const std::array overlapping_damage{
        veyra::LayoutRect{.x = 0.25F, .y = 0.25F, .width = 0.5F, .height = 0.5F},
        veyra::LayoutRect{.x = 0.75F, .y = 0.25F, .width = 0.5F, .height = 0.5F},
    };
    const auto surface = veyra::SoftwareSurface{
        .width = 3,
        .height = 1,
        .stride = 3,
        .pixels = pixels,
    };

    const auto result = veyra::render_software(commands, surface, {}, overlapping_damage);
    require(result == veyra::SoftwareRenderResult::invalid_input, "overlapping damage is rejected");
    for (std::size_t index = 0; index < pixels.size(); ++index) {
        require(
            pixel_is(
                pixels[index],
                before[index].red,
                before[index].green,
                before[index].blue,
                before[index].alpha
            ),
            "overlapping damage rejection leaves the surface unchanged"
        );
    }
}

void check_invalid_input_is_transactional() {
    constexpr std::size_t width = 3;
    constexpr std::size_t height = 2;
    const auto initial = veyra::SoftwarePixel{
        .red = 5,
        .green = 6,
        .blue = 7,
    };
    std::array<veyra::SoftwarePixel, width * height> pixels;
    pixels.fill(initial);
    const auto surface = veyra::SoftwareSurface{
        .width = width,
        .height = height,
        .stride = width,
        .pixels = pixels,
    };
    const std::array<veyra::DisplayCommand, 1> valid_fill{
        veyra::FillRect{
            .bounds = {.width = 3.0F, .height = 2.0F},
            .color = {.red = 10, .green = 20, .blue = 30},
        },
    };
    const std::array<veyra::DisplayCommand, 2> unmatched_pop{
        veyra::PopClip{},
        valid_fill[0],
    };
    const std::array<veyra::DisplayCommand, 2> unclosed_push{
        veyra::PushClip{
            .bounds = {.width = 3.0F, .height = 2.0F},
            .clip_id = 0,
        },
        valid_fill[0],
    };
    const std::array<veyra::DisplayCommand, 3> clipped_fill{
        veyra::PushClip{
            .bounds = {.width = 3.0F, .height = 2.0F},
            .clip_id = 0,
        },
        valid_fill[0],
        veyra::PopClip{},
    };
    const std::array<veyra::DisplayCommand, 1> invalid_bounds{
        veyra::FillRect{
            .bounds = {.width = -1.0F, .height = 2.0F},
            .color = {.red = 10, .green = 20, .blue = 30},
        },
    };
    const std::array<veyra::DisplayCommand, 1> invalid_stroke{
        veyra::StrokeRect{
            .bounds = {.width = 3.0F, .height = 2.0F},
            .thickness = -1.0F,
        },
    };
    const std::array<veyra::DisplayCommand, 1> invalid_mask{
        veyra::DrawMask{
            .mask = nullptr,
        },
    };
    const auto invalid_mask_storage = veyra::SoftwareMask{
        .width = 2,
        .height = 2,
        .stride = 1,
        .coverage = {},
        .generation = 0,
    };
    const std::array<veyra::DisplayCommand, 1> short_mask{
        veyra::DrawMask{
            .mask = &invalid_mask_storage,
        },
    };
    const std::array<veyra::DisplayCommand, 1> invalid_image{
        veyra::DrawImage{},
    };
    const std::array<veyra::SoftwarePixel, 1> invalid_image_pixels{};
    const auto invalid_image_storage = veyra::SoftwareImage{
        .width = 2,
        .height = 1,
        .stride = 1,
        .pixels = invalid_image_pixels,
        .generation = 0,
    };
    const std::array<veyra::DisplayCommand, 1> short_image{
        veyra::DrawImage{
            .image = &invalid_image_storage,
        },
    };
    std::array<veyra::SoftwareClipRect, 1> clips{};
    const auto workspace = veyra::SoftwareRenderWorkspace{.clips = clips};
    const auto untouched = [&pixels] {
        for (const auto& pixel : pixels) {
            if (!pixel_is(pixel, 5, 6, 7)) {
                return false;
            }
        }
        return true;
    };

    require(
        veyra::render_software(unmatched_pop, surface, workspace) ==
            veyra::SoftwareRenderResult::invalid_input,
        "unmatched clip pop is rejected"
    );
    require(untouched(), "invalid clip stack leaves the surface untouched");
    require(
        veyra::render_software(unclosed_push, surface, workspace) ==
            veyra::SoftwareRenderResult::invalid_input,
        "unclosed clip push is rejected"
    );
    require(untouched(), "unclosed clip stack leaves the surface untouched");
    require(
        veyra::render_software(invalid_bounds, surface, workspace) ==
            veyra::SoftwareRenderResult::invalid_input,
        "negative rectangle extent is rejected"
    );
    require(untouched(), "invalid geometry leaves the surface untouched");
    require(
        veyra::render_software(invalid_stroke, surface, workspace) ==
            veyra::SoftwareRenderResult::invalid_input,
        "negative border thickness is rejected"
    );
    require(untouched(), "invalid border leaves the surface untouched");
    require(
        veyra::render_software(invalid_mask, surface, workspace) ==
            veyra::SoftwareRenderResult::invalid_input,
        "null mask storage is rejected"
    );
    require(untouched(), "null mask leaves the surface untouched");
    require(
        veyra::render_software(short_mask, surface, workspace) ==
            veyra::SoftwareRenderResult::invalid_input,
        "short mask storage is rejected"
    );
    require(untouched(), "invalid mask storage leaves the surface untouched");
    require(
        veyra::render_software(invalid_image, surface, workspace) ==
            veyra::SoftwareRenderResult::invalid_input,
        "null image storage is rejected"
    );
    require(untouched(), "null image leaves the surface untouched");
    require(
        veyra::render_software(short_image, surface, workspace) ==
            veyra::SoftwareRenderResult::invalid_input,
        "short image storage is rejected"
    );
    require(untouched(), "invalid image storage leaves the surface untouched");
    require(
        veyra::render_software(clipped_fill, surface, {}) ==
            veyra::SoftwareRenderResult::insufficient_workspace,
        "short clip scratch is reported before rendering"
    );
    require(untouched(), "short clip scratch leaves the surface untouched");

    const std::array invalid_damage{
        veyra::LayoutRect{.x = 0.0F, .y = 0.0F, .width = 3.0F, .height = -1.0F},
    };
    require(
        veyra::render_software(valid_fill, surface, workspace, invalid_damage) ==
            veyra::SoftwareRenderResult::invalid_input,
        "negative damage extent is rejected"
    );
    require(untouched(), "invalid damage leaves the surface untouched");

    const auto short_surface = veyra::SoftwareSurface{
        .width = width,
        .height = height,
        .stride = width,
        .pixels = std::span<veyra::SoftwarePixel>(pixels).first(pixels.size() - 1),
    };
    require(
        veyra::render_software(valid_fill, short_surface, workspace) ==
            veyra::SoftwareRenderResult::invalid_surface,
        "short pixel storage is rejected"
    );
    require(untouched(), "invalid surface storage leaves the available pixels untouched");
}

void check_per_command_geometry_rejections_are_transactional() {
    constexpr std::size_t width = 3;
    constexpr std::size_t height = 2;
    const auto initial = veyra::SoftwarePixel{
        .red = 5,
        .green = 6,
        .blue = 7,
    };
    std::array<veyra::SoftwarePixel, width * height> pixels;
    pixels.fill(initial);
    const auto surface = veyra::SoftwareSurface{
        .width = width,
        .height = height,
        .stride = width,
        .pixels = pixels,
    };
    const auto untouched = [&pixels] {
        for (const auto& pixel : pixels) {
            if (!pixel_is(pixel, 5, 6, 7)) {
                return false;
            }
        }
        return true;
    };
    std::array<veyra::LayoutPoint, 2> translations{};
    const auto rejects = [&](std::span<const veyra::DisplayCommand> commands,
                             veyra::SoftwareRenderWorkspace workspace,
                             std::string_view rejection,
                             std::string_view preservation) {
        const auto result = veyra::render_software(commands, surface, workspace);
        require(result == veyra::SoftwareRenderResult::invalid_input, rejection);
        require(untouched(), preservation);
    };
    const auto plain_workspace = veyra::SoftwareRenderWorkspace{.translations = translations};
    constexpr auto huge = std::numeric_limits<float>::max();
    constexpr auto infinite = std::numeric_limits<float>::infinity();
    constexpr auto not_a_number = std::numeric_limits<float>::quiet_NaN();

    const std::array non_finite_points{
        veyra::LayoutPoint{},
        veyra::LayoutPoint{.x = infinite, .y = 1.0F},
    };
    const std::array<veyra::DisplayCommand, 1> non_finite_polyline{
        veyra::StrokePolyline{
            .points = non_finite_points,
            .thickness = 1.0F,
        },
    };
    rejects(
        non_finite_polyline,
        {},
        "polyline with a non-finite point is rejected",
        "rejected polyline points leave the surface untouched"
    );

    const std::array<veyra::DisplayCommand, 1> non_finite_clip{
        veyra::PushClip{
            .bounds = {.x = not_a_number, .width = 1.0F, .height = 1.0F},
            .clip_id = 0,
        },
    };
    rejects(
        non_finite_clip,
        {},
        "clip with non-finite bounds is rejected",
        "rejected clip bounds leave the surface untouched"
    );

    const std::array<veyra::DisplayCommand, 1> non_finite_translation{
        veyra::PushTranslation{
            .offset = {.x = 1.0F, .y = infinite},
        },
    };
    rejects(
        non_finite_translation,
        {},
        "translation with a non-finite offset is rejected",
        "rejected translation offsets leave the surface untouched"
    );

    const std::array<std::uint8_t, 4> coverage{255, 255, 255, 255};
    const auto mask = veyra::SoftwareMask{
        .width = 2,
        .height = 2,
        .stride = 2,
        .coverage = coverage,
        .generation = 0,
    };
    const std::array<veyra::DisplayCommand, 1> nan_mask{
        veyra::DrawMask{
            .x = not_a_number,
            .mask = &mask,
        },
    };
    rejects(
        nan_mask,
        {},
        "mask with a non-numeric position is rejected",
        "rejected mask positions leave the surface untouched"
    );
    const std::array<veyra::DisplayCommand, 1> infinite_mask{
        veyra::DrawMask{
            .y = infinite,
            .mask = &mask,
        },
    };
    rejects(
        infinite_mask,
        {},
        "mask with an infinite position is rejected",
        "rejected infinite mask positions leave the surface untouched"
    );

    const std::array<veyra::DisplayCommand, 4> overflowing_pushes{
        veyra::PushTranslation{
            .offset = {.x = huge},
        },
        veyra::PushTranslation{
            .offset = {.x = huge},
        },
        veyra::PopTranslation{},
        veyra::PopTranslation{},
    };
    rejects(
        overflowing_pushes,
        plain_workspace,
        "translations that overflow to infinity are rejected",
        "overflowing translations leave the surface untouched"
    );

    const std::array<veyra::DisplayCommand, 3> translated_fill{
        veyra::PushTranslation{
            .offset = {.x = huge},
        },
        veyra::FillRect{
            .bounds = {.x = huge, .width = 1.0F, .height = 1.0F},
        },
        veyra::PopTranslation{},
    };
    rejects(
        translated_fill,
        plain_workspace,
        "translated rectangles that overflow to infinity are rejected",
        "overflowing translated rectangles leave the surface untouched"
    );

    const std::array<veyra::DisplayCommand, 3> translated_rounded_fill{
        veyra::PushTranslation{
            .offset = {.x = huge},
        },
        veyra::FillRoundedRect{
            .bounds = {.x = huge, .width = 1.0F, .height = 1.0F},
        },
        veyra::PopTranslation{},
    };
    rejects(
        translated_rounded_fill,
        plain_workspace,
        "translated rounded rectangles that overflow are rejected",
        "overflowing rounded rectangles leave the surface untouched"
    );

    const std::array<veyra::DisplayCommand, 3> translated_stroke{
        veyra::PushTranslation{
            .offset = {.y = huge},
        },
        veyra::StrokeRect{
            .bounds = {.y = huge, .width = 1.0F, .height = 1.0F},
            .thickness = 1.0F,
        },
        veyra::PopTranslation{},
    };
    rejects(
        translated_stroke,
        plain_workspace,
        "translated borders that overflow to infinity are rejected",
        "overflowing translated borders leave the surface untouched"
    );

    const std::array<veyra::DisplayCommand, 3> translated_rounded_stroke{
        veyra::PushTranslation{
            .offset = {.x = huge},
        },
        veyra::StrokeRoundedRect{
            .bounds = {.x = huge, .width = 1.0F, .height = 1.0F},
            .thickness = 1.0F,
        },
        veyra::PopTranslation{},
    };
    rejects(
        translated_rounded_stroke,
        plain_workspace,
        "translated rounded borders that overflow are rejected",
        "overflowing rounded borders leave the surface untouched"
    );

    const std::array<veyra::DisplayCommand, 3> translated_line{
        veyra::PushTranslation{
            .offset = {.x = huge},
        },
        veyra::StrokeLine{
            .start = {.x = huge},
            .end = {.x = huge, .y = 1.0F},
            .thickness = 1.0F,
        },
        veyra::PopTranslation{},
    };
    rejects(
        translated_line,
        plain_workspace,
        "translated lines that overflow to infinity are rejected",
        "overflowing translated lines leave the surface untouched"
    );

    const std::array translated_polyline_points{
        veyra::LayoutPoint{},
        veyra::LayoutPoint{.x = huge, .y = 1.0F},
    };
    const std::array<veyra::DisplayCommand, 3> translated_polyline{
        veyra::PushTranslation{
            .offset = {.x = huge},
        },
        veyra::StrokePolyline{
            .points = translated_polyline_points,
            .thickness = 1.0F,
        },
        veyra::PopTranslation{},
    };
    rejects(
        translated_polyline,
        plain_workspace,
        "translated polylines that overflow to infinity are rejected",
        "overflowing translated polylines leave the surface untouched"
    );

    const std::array<veyra::DisplayCommand, 3> translated_mask{
        veyra::PushTranslation{
            .offset = {.x = huge},
        },
        veyra::DrawMask{
            .x = huge,
            .mask = &mask,
        },
        veyra::PopTranslation{},
    };
    rejects(
        translated_mask,
        plain_workspace,
        "translated masks that overflow to infinity are rejected",
        "overflowing translated masks leave the surface untouched"
    );

    const std::array<veyra::SoftwarePathSegment, 1> translated_path_segments{
        veyra::SoftwareLineTo{
            .end = {.x = 1.0F, .y = 1.0F},
        },
    };
    const std::array<veyra::SoftwarePathContour, 1> translated_path_contours{
        veyra::SoftwarePathContour{
            .start = {.x = huge},
            .segments = translated_path_segments,
        },
    };
    const std::array<veyra::DisplayCommand, 3> translated_path{
        veyra::PushTranslation{
            .offset = {.x = huge},
        },
        veyra::FillPath{
            .contours = translated_path_contours,
        },
        veyra::PopTranslation{},
    };
    std::array<double, 2> path_intersections{};
    rejects(
        translated_path,
        {
            .translations = translations,
            .path_intersections = path_intersections,
        },
        "translated paths that overflow to infinity are rejected",
        "overflowing translated paths leave the surface untouched"
    );

    const std::array<veyra::SoftwarePixel, 1> image_pixels{
        veyra::SoftwarePixel{.red = 255},
    };
    const auto image = veyra::SoftwareImage{
        .width = 1,
        .height = 1,
        .stride = 1,
        .pixels = image_pixels,
        .generation = 0,
    };
    const std::array<veyra::DisplayCommand, 3> translated_image{
        veyra::PushTranslation{
            .offset = {.x = huge},
        },
        veyra::DrawImage{
            .source = {.width = 1.0F, .height = 1.0F},
            .destination = {.x = huge, .width = 1.0F, .height = 1.0F},
            .image = &image,
        },
        veyra::PopTranslation{},
    };
    rejects(
        translated_image,
        plain_workspace,
        "translated images that overflow to infinity are rejected",
        "overflowing translated images leave the surface untouched"
    );

    const std::array<veyra::DisplayCommand, 4> translated_clip{
        veyra::PushTranslation{
            .offset = {.x = huge},
        },
        veyra::PushClip{
            .bounds = {.x = huge, .width = 1.0F, .height = 1.0F},
            .clip_id = 0,
        },
        veyra::PopClip{},
        veyra::PopTranslation{},
    };
    std::array<veyra::SoftwareClipRect, 1> clips{};
    rejects(
        translated_clip,
        {.clips = clips, .translations = translations},
        "translated clips that overflow to infinity are rejected",
        "overflowing translated clips leave the surface untouched"
    );
}

}

int main() {
    try {
        check_overlapping_damage_is_rejected();
        check_invalid_input_is_transactional();
        check_per_command_geometry_rejections_are_transactional();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
