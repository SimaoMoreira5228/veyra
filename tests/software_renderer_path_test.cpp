#include "veyra/render/software_renderer.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <iostream>
#include <limits>
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

template <std::size_t N> auto line_segments(const std::array<veyra::LayoutPoint, N>& points) {
    std::array<veyra::SoftwarePathSegment, N - 1> segments{};
    for (std::size_t index = 1; index < N; ++index) {
        segments[index - 1] = veyra::SoftwareLineTo{
            .end = points[index],
        };
    }

    return segments;
}

void check_concave_path_and_clipping() {
    constexpr auto width = std::size_t{6};
    constexpr auto height = std::size_t{6};
    constexpr auto color = veyra::DisplayColor{
        .red = 30,
        .green = 60,
        .blue = 90,
    };
    const std::array points{
        veyra::LayoutPoint{.x = 0.0F, .y = 0.0F},
        veyra::LayoutPoint{.x = 4.0F, .y = 0.0F},
        veyra::LayoutPoint{.x = 4.0F, .y = 1.0F},
        veyra::LayoutPoint{.x = 1.0F, .y = 1.0F},
        veyra::LayoutPoint{.x = 1.0F, .y = 4.0F},
        veyra::LayoutPoint{.x = 0.0F, .y = 4.0F},
    };
    const auto segments = line_segments(points);
    const std::array contours{
        veyra::SoftwarePathContour{
            .start = points.front(),
            .segments = segments,
        },
    };
    const std::array<veyra::DisplayCommand, 5> commands{
        veyra::PushTranslation{
            .offset = {.x = 1.0F, .y = 1.0F},
        },
        veyra::PushClip{
            .bounds = {.width = 3.0F, .height = 3.0F},
            .clip_id = 0,
        },
        veyra::FillPath{
            .contours = contours,
            .color = color,
        },
        veyra::PopClip{},
        veyra::PopTranslation{},
    };
    std::array<veyra::SoftwarePixel, width * height> pixels{};
    std::array<veyra::SoftwareClipRect, 1> clips{};
    std::array<veyra::LayoutPoint, 1> translations{};
    std::array<double, 6> intersections{};
    const auto surface = veyra::SoftwareSurface{
        .width = width,
        .height = height,
        .stride = width,
        .pixels = pixels,
    };
    const auto workspace = veyra::SoftwareRenderWorkspace{
        .clips = clips,
        .translations = translations,
        .path_intersections = intersections,
    };

    require(
        veyra::render_software(commands, surface, workspace) ==
            veyra::SoftwareRenderResult::complete,
        "concave path renders"
    );
    require(pixel_is(pixels[1 * width + 1], 30, 60, 90), "path uses translated coordinates");
    require(pixel_is(pixels[1 * width + 3], 30, 60, 90), "path fills its upper arm");
    require(pixel_is(pixels[2 * width + 1], 30, 60, 90), "path fills its lower arm");
    require(pixel_is(pixels[2 * width + 2], 0, 0, 0), "concave cutout remains empty");
    require(pixel_is(pixels[1 * width + 4], 0, 0, 0), "clip bounds the path");

    std::array<veyra::SoftwarePixel, width * height> untouched{};
    auto insufficient_workspace = workspace;
    insufficient_workspace.path_intersections = {};
    const auto insufficient_result = veyra::render_software(
        commands,
        {
            .width = width,
            .height = height,
            .stride = width,
            .pixels = untouched,
        },
        insufficient_workspace
    );
    require(
        insufficient_result == veyra::SoftwareRenderResult::insufficient_workspace,
        "path intersection scratch is checked before drawing"
    );
    require(
        std::all_of(
            untouched.begin(),
            untouched.end(),
            [](const auto& pixel) {
                return pixel_is(pixel, 0, 0, 0);
            }
        ),
        "insufficient path scratch leaves pixels unchanged"
    );
}

void check_invalid_paths() {
    const std::array too_few_contours{
        veyra::SoftwarePathContour{
            .start = {},
            .segments = {},
        },
    };
    const std::array<veyra::DisplayCommand, 1> too_few_commands{
        veyra::FillPath{
            .contours = too_few_contours,
        },
    };
    const std::array non_finite_points{
        veyra::LayoutPoint{},
        veyra::LayoutPoint{.x = 1.0F},
        veyra::LayoutPoint{
            .y = std::numeric_limits<float>::infinity(),
        },
    };
    const auto non_finite_segments = line_segments(non_finite_points);
    const std::array non_finite_contours{
        veyra::SoftwarePathContour{
            .start = non_finite_points.front(),
            .segments = non_finite_segments,
        },
    };
    const std::array<veyra::DisplayCommand, 1> non_finite_commands{
        veyra::FillPath{
            .contours = non_finite_contours,
        },
    };
    std::array<veyra::SoftwarePixel, 1> pixels{};
    const auto surface = veyra::SoftwareSurface{
        .width = 1,
        .height = 1,
        .stride = 1,
        .pixels = pixels,
    };

    require(
        veyra::render_software(too_few_commands, surface, {}) ==
            veyra::SoftwareRenderResult::invalid_input,
        "path contours need at least one segment"
    );
    require(
        veyra::render_software(non_finite_commands, surface, {}) ==
            veyra::SoftwareRenderResult::invalid_input,
        "non-finite path points and controls are rejected"
    );

    const std::array invalid_curve_segments{
        veyra::SoftwarePathSegment{
            veyra::SoftwareQuadraticTo{
                .control = {.x = std::numeric_limits<float>::infinity()},
                .end = {.x = 1.0F, .y = 1.0F},
            },
        },
        veyra::SoftwarePathSegment{
            veyra::SoftwareLineTo{
                .end = {.y = 1.0F},
            },
        },
    };
    const std::array invalid_curve_contours{
        veyra::SoftwarePathContour{
            .start = {.y = 1.0F},
            .segments = invalid_curve_segments,
        },
    };
    const std::array<veyra::DisplayCommand, 1> invalid_curve_commands{
        veyra::FillPath{
            .contours = invalid_curve_contours,
        },
    };
    require(
        veyra::render_software(invalid_curve_commands, surface, {}) ==
            veyra::SoftwareRenderResult::invalid_input,
        "non-finite curve controls are rejected"
    );
}

void check_alpha_compositing() {
    const std::array points{
        veyra::LayoutPoint{},
        veyra::LayoutPoint{.x = 1.0F},
        veyra::LayoutPoint{.x = 1.0F, .y = 1.0F},
        veyra::LayoutPoint{.y = 1.0F},
    };
    const auto segments = line_segments(points);
    const std::array contours{
        veyra::SoftwarePathContour{
            .start = points.front(),
            .segments = segments,
        },
    };
    const std::array<veyra::DisplayCommand, 1> commands{
        veyra::FillPath{
            .contours = contours,
            .color = {.red = 100, .green = 120, .blue = 140, .alpha = 128},
        },
    };
    std::array<veyra::SoftwarePixel, 1> pixels{
        veyra::SoftwarePixel{
            .alpha = 0,
        },
    };
    const auto surface = veyra::SoftwareSurface{
        .width = 1,
        .height = 1,
        .stride = 1,
        .pixels = pixels,
    };
    std::array<double, 4> intersections{};

    require(
        veyra::render_software(
            commands,
            surface,
            {
                .path_intersections = intersections,
            }
        ) == veyra::SoftwareRenderResult::complete,
        "translucent path renders"
    );
    require(pixel_is(pixels[0], 100, 120, 140, 128), "path uses source-over alpha");
}

void check_even_odd_hole() {
    const std::array outer_points{
        veyra::LayoutPoint{},
        veyra::LayoutPoint{.x = 6.0F},
        veyra::LayoutPoint{.x = 6.0F, .y = 6.0F},
        veyra::LayoutPoint{.y = 6.0F},
    };
    const auto outer_segments = line_segments(outer_points);
    const std::array inner_points{
        veyra::LayoutPoint{.x = 2.0F, .y = 2.0F},
        veyra::LayoutPoint{.x = 4.0F, .y = 2.0F},
        veyra::LayoutPoint{.x = 4.0F, .y = 4.0F},
        veyra::LayoutPoint{.x = 2.0F, .y = 4.0F},
    };
    const auto inner_segments = line_segments(inner_points);
    const std::array contours{
        veyra::SoftwarePathContour{
            .start = outer_points.front(),
            .segments = outer_segments,
        },
        veyra::SoftwarePathContour{
            .start = inner_points.front(),
            .segments = inner_segments,
        },
    };
    const std::array<veyra::DisplayCommand, 1> commands{
        veyra::FillPath{
            .contours = contours,
            .color = {.red = 90, .green = 100, .blue = 110},
        },
    };
    std::array<veyra::SoftwarePixel, 36> pixels{};
    std::array<double, 8> intersections{};
    const auto result = veyra::render_software(
        commands,
        {
            .width = 6,
            .height = 6,
            .stride = 6,
            .pixels = pixels,
        },
        {
            .path_intersections = intersections,
        }
    );

    require(result == veyra::SoftwareRenderResult::complete, "multi-contour path renders");
    require(pixel_is(pixels[0], 90, 100, 110), "outer contour is filled");
    require(pixel_is(pixels[2 * 6 + 2], 0, 0, 0), "inner contour remains a hole");

    std::array<std::int8_t, 8> windings{};
    const std::array non_zero_commands{
        veyra::DisplayCommand{
            veyra::FillPath{
                .contours = contours,
                .color = {.red = 90, .green = 100, .blue = 110},
                .fill_rule = veyra::SoftwarePathFillRule::non_zero,
            },
        },
    };
    std::array<veyra::SoftwarePixel, 36> same_direction_pixels{};
    const auto same_direction_result = veyra::render_software(
        non_zero_commands,
        {
            .width = 6,
            .height = 6,
            .stride = 6,
            .pixels = same_direction_pixels,
        },
        {
            .path_intersections = intersections,
            .path_windings = windings,
        }
    );
    require(
        same_direction_result == veyra::SoftwareRenderResult::complete,
        "non-zero path with same-direction contours renders"
    );
    require(
        pixel_is(same_direction_pixels[2 * 6 + 2], 90, 100, 110),
        "same-direction contours keep their shared interior filled"
    );

    std::array<veyra::SoftwarePixel, 36> insufficient_winding_pixels{};
    const auto insufficient_winding_result = veyra::render_software(
        non_zero_commands,
        {
            .width = 6,
            .height = 6,
            .stride = 6,
            .pixels = insufficient_winding_pixels,
        },
        {
            .path_intersections = intersections,
            .path_windings = std::span<std::int8_t>(windings).first(7),
        }
    );
    require(
        insufficient_winding_result == veyra::SoftwareRenderResult::insufficient_workspace,
        "non-zero paths require one winding slot per possible intersection"
    );
    require(
        std::all_of(
            insufficient_winding_pixels.begin(),
            insufficient_winding_pixels.end(),
            [](const auto& pixel) {
                return pixel_is(pixel, 0, 0, 0);
            }
        ),
        "insufficient winding scratch leaves pixels unchanged"
    );

    const std::array reversed_inner_points{
        veyra::LayoutPoint{.x = 2.0F, .y = 2.0F},
        veyra::LayoutPoint{.x = 2.0F, .y = 4.0F},
        veyra::LayoutPoint{.x = 4.0F, .y = 4.0F},
        veyra::LayoutPoint{.x = 4.0F, .y = 2.0F},
    };
    const auto reversed_inner_segments = line_segments(reversed_inner_points);
    const std::array opposite_direction_contours{
        contours[0],
        veyra::SoftwarePathContour{
            .start = reversed_inner_points.front(),
            .segments = reversed_inner_segments,
        },
    };
    const std::array opposite_direction_commands{
        veyra::DisplayCommand{
            veyra::FillPath{
                .contours = opposite_direction_contours,
                .color = {.red = 90, .green = 100, .blue = 110},
                .fill_rule = veyra::SoftwarePathFillRule::non_zero,
            },
        },
    };
    std::array<veyra::SoftwarePixel, 36> opposite_direction_pixels{};
    const auto opposite_direction_result = veyra::render_software(
        opposite_direction_commands,
        {
            .width = 6,
            .height = 6,
            .stride = 6,
            .pixels = opposite_direction_pixels,
        },
        {
            .path_intersections = intersections,
            .path_windings = windings,
        }
    );
    require(
        opposite_direction_result == veyra::SoftwareRenderResult::complete,
        "non-zero path with opposite-direction contours renders"
    );
    require(
        pixel_is(opposite_direction_pixels[2 * 6 + 2], 0, 0, 0),
        "opposite-direction contours create a non-zero winding hole"
    );

    const std::array many_even_odd_contours{contours[0], contours[1], contours[1]};
    const std::array mixed_fill_commands{
        veyra::DisplayCommand{
            veyra::FillPath{
                .contours = many_even_odd_contours,
                .color = {.red = 90, .green = 100, .blue = 110},
            },
        },
        veyra::DisplayCommand{
            veyra::FillPath{
                .contours = std::span<const veyra::SoftwarePathContour>(opposite_direction_contours)
                                .last(1),
                .color = {.red = 90, .green = 100, .blue = 110},
                .fill_rule = veyra::SoftwarePathFillRule::non_zero,
            },
        },
    };
    std::array<veyra::SoftwarePixel, 36> mixed_fill_pixels{};
    std::array<double, 12> mixed_fill_intersections{};
    std::array<std::int8_t, 4> mixed_fill_windings{};
    const auto mixed_fill_result = veyra::render_software(
        mixed_fill_commands,
        {
            .width = 6,
            .height = 6,
            .stride = 6,
            .pixels = mixed_fill_pixels,
        },
        {
            .path_intersections = mixed_fill_intersections,
            .path_windings = mixed_fill_windings,
        }
    );
    require(
        mixed_fill_result == veyra::SoftwareRenderResult::complete,
        "mixed path fill rules use their own scratch requirements"
    );

    std::array<veyra::SoftwarePixel, 36> untouched{};
    const auto insufficient_result = veyra::render_software(
        commands,
        {
            .width = 6,
            .height = 6,
            .stride = 6,
            .pixels = untouched,
        },
        {
            .path_intersections = std::span<double>(intersections).first(7),
        }
    );
    require(
        insufficient_result == veyra::SoftwareRenderResult::insufficient_workspace,
        "path scratch covers every contour"
    );
    require(
        std::all_of(
            untouched.begin(),
            untouched.end(),
            [](const auto& pixel) {
                return pixel_is(pixel, 0, 0, 0);
            }
        ),
        "insufficient multi-contour scratch leaves pixels unchanged"
    );
}

void check_quadratic_and_cubic_curves() {
    const std::array quadratic_segments{
        veyra::SoftwarePathSegment{
            veyra::SoftwareQuadraticTo{
                .control = {.x = 3.0F, .y = 0.0F},
                .end = {.x = 5.0F, .y = 4.0F},
            },
        },
        veyra::SoftwarePathSegment{
            veyra::SoftwareLineTo{
                .end = {.x = 1.0F, .y = 4.0F},
            },
        },
    };
    const std::array cubic_segments{
        veyra::SoftwarePathSegment{
            veyra::SoftwareCubicTo{
                .control_1 = {.x = 8.0F, .y = 0.0F},
                .control_2 = {.x = 12.0F, .y = 0.0F},
                .end = {.x = 12.0F, .y = 4.0F},
            },
        },
        veyra::SoftwarePathSegment{
            veyra::SoftwareLineTo{
                .end = {.x = 8.0F, .y = 4.0F},
            },
        },
    };
    const std::array contours{
        veyra::SoftwarePathContour{
            .start = {.x = 1.0F, .y = 4.0F},
            .segments = quadratic_segments,
        },
        veyra::SoftwarePathContour{
            .start = {.x = 8.0F, .y = 4.0F},
            .segments = cubic_segments,
        },
    };
    const std::array<veyra::DisplayCommand, 1> commands{
        veyra::FillPath{
            .contours = contours,
            .color = {.red = 40, .green = 80, .blue = 120},
        },
    };
    std::array<veyra::SoftwarePixel, 84> pixels{};
    std::array<double, 9> intersections{};
    const auto result = veyra::render_software(
        commands,
        {
            .width = 14,
            .height = 6,
            .stride = 14,
            .pixels = pixels,
        },
        {
            .path_intersections = intersections,
        }
    );

    require(result == veyra::SoftwareRenderResult::complete, "quadratic and cubic paths render");
    require(pixel_is(pixels[2 * 14 + 3], 40, 80, 120), "quadratic curve fills its interior");
    require(pixel_is(pixels[1 * 14 + 10], 40, 80, 120), "cubic curve fills its interior");
    require(pixel_is(pixels[0 * 14 + 10], 0, 0, 0), "cubic curve excludes area above its apex");

    std::array<veyra::SoftwarePixel, 84> untouched{};
    std::array<double, 8> insufficient_intersections{};
    const auto insufficient_result = veyra::render_software(
        commands,
        {
            .width = 14,
            .height = 6,
            .stride = 14,
            .pixels = untouched,
        },
        {
            .path_intersections = insufficient_intersections,
        }
    );
    require(
        insufficient_result == veyra::SoftwareRenderResult::insufficient_workspace,
        "curve workspace covers all possible intersections"
    );
    require(
        std::all_of(
            untouched.begin(),
            untouched.end(),
            [](const auto& pixel) {
                return pixel_is(pixel, 0, 0, 0);
            }
        ),
        "undersized curve workspace leaves pixels unchanged"
    );

    const std::array wave_segments{
        veyra::SoftwarePathSegment{
            veyra::SoftwareCubicTo{
                .control_1 = {.x = 4.0F, .y = 6.0F},
                .control_2 = {.x = 8.0F, .y = -4.0F},
                .end = {.x = 12.0F, .y = 4.0F},
            },
        },
    };
    const std::array wave_contours{
        veyra::SoftwarePathContour{
            .start = {.y = 2.0F},
            .segments = wave_segments,
        },
    };
    const std::array<veyra::DisplayCommand, 1> wave_commands{
        veyra::FillPath{
            .contours = wave_contours,
            .color = {.red = 40, .green = 80, .blue = 120},
        },
    };
    std::array<veyra::SoftwarePixel, 65> wave_pixels{};
    std::array<double, 4> wave_intersections{};
    const auto wave_result = veyra::render_software(
        wave_commands,
        {
            .width = 13,
            .height = 5,
            .stride = 13,
            .pixels = wave_pixels,
        },
        {
            .path_intersections = wave_intersections,
        }
    );

    require(wave_result == veyra::SoftwareRenderResult::complete, "two-extrema cubic renders");
    require(pixel_is(wave_pixels[2 * 13 + 1], 40, 80, 120), "first cubic crossing interval fills");
    require(pixel_is(wave_pixels[2 * 13 + 3], 0, 0, 0), "cubic crossing intervals stay separate");
    require(pixel_is(wave_pixels[2 * 13 + 4], 40, 80, 120), "third cubic crossing fills");
    require(pixel_is(wave_pixels[2 * 13 + 11], 0, 0, 0), "cubic closing edge remains outside");
}

}

int main() {
    try {
        check_concave_path_and_clipping();
        check_invalid_paths();
        check_alpha_compositing();
        check_even_odd_hole();
        check_quadratic_and_cubic_curves();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
