#include "veyra/render/software_renderer.hpp"
#include "veyra/render/retained_scroll.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;

struct Options {
    std::size_t width{1280};
    std::size_t height{720};
    std::size_t commands{64};
    std::size_t frames{5'000};
    std::size_t warmup{500};
    std::size_t scroll_delta{};
    bool scroll_full{};
    bool scroll_verify{};
    bool damage{};
    bool translucent{};
    bool mask{};
    bool path{};
    bool curve{};
    bool winding{};
    bool image{};
    bool scaled_image{};
    bool linear_image{};
    bool faded_image{};
    bool border{};
    bool rounded{};
    bool rounded_border{};
    bool line{};
    bool polyline{};
    bool translated{};
};

std::size_t parse_size(const char* value) {
    const std::string_view text(value);
    std::size_t parsed = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), parsed);
    if (error != std::errc{} || end != text.data() + text.size()) {
        throw std::invalid_argument("expected a non-negative integer: " + std::string(text));
    }
    return parsed;
}

Options parse_options(int argc, char** argv) {
    Options options;
    for (int index = 1; index < argc; ++index) {
        const std::string_view argument(argv[index]);
        if (argument == "--damage") {
            options.damage = true;
            continue;
        }
        if (argument == "--translucent") {
            options.translucent = true;
            continue;
        }
        if (argument == "--mask") {
            options.mask = true;
            continue;
        }
        if (argument == "--path") {
            options.path = true;
            continue;
        }
        if (argument == "--curve") {
            options.curve = true;
            continue;
        }
        if (argument == "--winding") {
            options.winding = true;
            continue;
        }
        if (argument == "--image") {
            options.image = true;
            continue;
        }
        if (argument == "--scaled-image") {
            options.scaled_image = true;
            continue;
        }
        if (argument == "--linear-image") {
            options.linear_image = true;
            continue;
        }
        if (argument == "--faded-image") {
            options.faded_image = true;
            continue;
        }
        if (argument == "--border") {
            options.border = true;
            continue;
        }
        if (argument == "--rounded") {
            options.rounded = true;
            continue;
        }
        if (argument == "--rounded-border") {
            options.rounded_border = true;
            continue;
        }
        if (argument == "--line") {
            options.line = true;
            continue;
        }
        if (argument == "--polyline") {
            options.polyline = true;
            continue;
        }
        if (argument == "--translated") {
            options.translated = true;
            continue;
        }
        if (argument == "--scroll-full") {
            options.scroll_full = true;
            continue;
        }
        if (argument == "--scroll-verify") {
            options.scroll_verify = true;
            continue;
        }
        if (index + 1 >= argc) {
            throw std::invalid_argument("missing value for " + std::string(argument));
        }

        const auto value = parse_size(argv[++index]);
        if (argument == "--width") {
            options.width = value;
        } else if (argument == "--height") {
            options.height = value;
        } else if (argument == "--commands") {
            options.commands = value;
        } else if (argument == "--frames") {
            options.frames = value;
        } else if (argument == "--warmup") {
            options.warmup = value;
        } else if (argument == "--scroll-delta") {
            options.scroll_delta = value;
        } else {
            throw std::invalid_argument("unknown option: " + std::string(argument));
        }
    }

    const auto special_modes = static_cast<unsigned int>(options.mask) +
        static_cast<unsigned int>(options.path) + static_cast<unsigned int>(options.curve) +
        static_cast<unsigned int>(options.winding) + static_cast<unsigned int>(options.image) +
        static_cast<unsigned int>(options.scaled_image) +
        static_cast<unsigned int>(options.linear_image) +
        static_cast<unsigned int>(options.faded_image) + static_cast<unsigned int>(options.border) +
        static_cast<unsigned int>(options.rounded) +
        static_cast<unsigned int>(options.rounded_border) +
        static_cast<unsigned int>(options.line) + static_cast<unsigned int>(options.polyline);
    if (options.scroll_full && options.scroll_delta == 0) {
        throw std::invalid_argument("--scroll-full needs --scroll-delta");
    }
    if (options.scroll_verify && options.scroll_delta == 0) {
        throw std::invalid_argument("--scroll-verify needs --scroll-delta");
    }
    if (options.scroll_verify && options.scroll_full) {
        throw std::invalid_argument("choose one of --scroll-verify or --scroll-full");
    }
    if (special_modes > 1) {
        throw std::invalid_argument(
            "choose one of --mask, --path, --curve, --winding, --image, --scaled-image, "
            "--linear-image, --faded-image, "
            "--border, --rounded, --rounded-border, --line, or --polyline"
        );
    }
    if (options.width == 0 || options.width > 4'096 || options.height == 0 ||
        options.height > 4'096 || options.commands == 0 || options.commands > 100'000 ||
        options.frames == 0 || options.frames > 10'000'000 || options.warmup > 10'000'000) {
        throw std::invalid_argument("renderer values must be positive and within limits");
    }
    return options;
}

double percentile(const std::vector<double>& sorted_times, std::size_t percent) {
    const auto last = sorted_times.size() - 1;
    const auto index = last / 100 * percent + last % 100 * percent / 100;
    return sorted_times[index];
}

std::string_view scenario_name(const Options& options) noexcept {
    if (options.scroll_verify) {
        return "software_scroll_verify";
    }
    if (options.polyline && options.translated) {
        return options.damage ? "software_polyline_translated_damage"
                              : "software_polyline_translated_full";
    }
    if ((options.path || options.curve || options.winding) && options.translated) {
        if (options.curve) {
            return options.damage ? "software_curve_translated_damage"
                                  : "software_curve_translated_full";
        }
        if (options.winding) {
            return options.damage ? "software_winding_translated_damage"
                                  : "software_winding_translated_full";
        }
        return options.damage ? "software_path_translated_damage" : "software_path_translated_full";
    }
    if (options.translated) {
        return options.damage ? "software_translated_damage" : "software_translated_full";
    }
    if (options.rounded) {
        return options.damage ? "software_rounded_rect_damage" : "software_rounded_rect_full";
    }
    if (options.rounded_border) {
        return options.damage ? "software_rounded_border_damage" : "software_rounded_border_full";
    }
    if (options.path) {
        return options.damage ? "software_path_damage" : "software_path_full";
    }
    if (options.curve) {
        return options.damage ? "software_curve_damage" : "software_curve_full";
    }
    if (options.winding) {
        return options.damage ? "software_winding_damage" : "software_winding_full";
    }
    if (options.mask) {
        if (options.translucent) {
            return options.damage ? "software_mask_alpha_damage" : "software_mask_alpha_full";
        }
        return options.damage ? "software_mask_damage" : "software_mask_full";
    }
    if (options.linear_image) {
        if (options.translucent) {
            return options.damage ? "software_image_scaled_linear_alpha_damage"
                                  : "software_image_scaled_linear_alpha_full";
        }
        return options.damage ? "software_image_scaled_linear_damage"
                              : "software_image_scaled_linear_full";
    }
    if (options.faded_image) {
        return options.damage ? "software_image_opacity_damage" : "software_image_opacity_full";
    }
    if (options.scaled_image) {
        if (options.translucent) {
            return options.damage ? "software_image_scaled_alpha_damage"
                                  : "software_image_scaled_alpha_full";
        }
        return options.damage ? "software_image_scaled_nearest_damage"
                              : "software_image_scaled_nearest_full";
    }
    if (options.image) {
        if (options.translucent) {
            return options.damage ? "software_image_opaque_target_damage"
                                  : "software_image_opaque_target_full";
        }
        return options.damage ? "software_image_transparent_target_damage"
                              : "software_image_transparent_target_full";
    }
    if (options.border) {
        if (options.translucent) {
            return options.damage ? "software_border_alpha_damage" : "software_border_alpha_full";
        }
        return options.damage ? "software_border_damage" : "software_border_full";
    }
    if (options.line) {
        if (options.translucent) {
            return options.damage ? "software_line_alpha_damage" : "software_line_alpha_full";
        }
        return options.damage ? "software_line_damage" : "software_line_full";
    }
    if (options.polyline) {
        return options.damage ? "software_polyline_damage" : "software_polyline_full";
    }
    if (options.translucent) {
        return options.damage ? "software_rgba_damage" : "software_rgba_full";
    }
    return options.damage ? "software_rect_damage" : "software_rect_full";
}

int run_scroll(const Options& options) {
    constexpr auto band_height = std::size_t{8};

    if (options.height % band_height != 0) {
        throw std::invalid_argument("scroll mode needs a height divisible by 8");
    }
    if (options.scroll_delta % band_height != 0) {
        throw std::invalid_argument("scroll mode needs a delta divisible by 8");
    }

    const auto bands = options.height / band_height;
    const auto shift = options.scroll_delta / band_height;
    std::vector<veyra::DisplayCommand> commands(bands);
    std::vector<veyra::SoftwarePixel> pixels(options.width * options.height);
    std::vector<veyra::SoftwarePixel> scratch_pixels(options.width * options.height);
    const auto surface = veyra::SoftwareSurface{
        .width = options.width,
        .height = options.height,
        .stride = options.width,
        .pixels = pixels,
    };
    const auto scratch = veyra::SoftwareSurface{
        .width = options.width,
        .height = options.height,
        .stride = options.width,
        .pixels = scratch_pixels,
    };
    std::array<veyra::SoftwareClipRect, 1> clips{};
    const auto workspace = veyra::SoftwareRenderWorkspace{.clips = clips};
    auto retained = veyra::RetainedScrollSurface{};
    auto stats = veyra::RetainedScrollStats{};
    auto verify_stats = veyra::RetainedScrollVerifyStats{};
    auto last_verify = veyra::RetainedVerifyResult::complete;

    const auto paint = [&](std::size_t frame) {
        for (auto band = std::size_t{}; band < bands; ++band) {
            const auto source = (band + bands - (frame * shift) % bands) % bands;
            commands[band] = veyra::FillRect{
                .bounds =
                    {
                        .y = static_cast<float>(band * band_height),
                        .width = static_cast<float>(options.width),
                        .height = static_cast<float>(band_height),
                    },
                .color = {
                    .red = static_cast<std::uint8_t>((source * 31) % 256),
                    .green = static_cast<std::uint8_t>((source * 67) % 256),
                    .blue = static_cast<std::uint8_t>((source * 97) % 256),
                    .alpha = 255,
                },
            };
        }

        if (options.scroll_full) {
            return veyra::render_software(commands, surface, workspace) ==
                veyra::SoftwareRenderResult::complete;
        }

        if (options.scroll_verify) {
            last_verify = veyra::render_retained_scrolled_verified(
                commands,
                surface,
                workspace,
                {
                    .delta_y = static_cast<int>(options.scroll_delta),
                    .content_generation = 1,
                },
                retained,
                scratch,
                stats,
                verify_stats
            );

            return last_verify == veyra::RetainedVerifyResult::complete ||
                last_verify == veyra::RetainedVerifyResult::mismatch;
        }

        return veyra::render_retained_scrolled(
                   commands,
                   surface,
                   workspace,
                   {
                       .delta_y = static_cast<int>(options.scroll_delta),
                       .content_generation = 1,
                   },
                   retained,
                   stats
               ) == veyra::RetainedScrollResult::complete;
    };

    for (auto frame = std::size_t{}; frame < options.warmup; ++frame) {
        if (options.scroll_full) {
            std::fill(pixels.begin(), pixels.end(), veyra::SoftwarePixel{});
        }
        if (!paint(frame)) {
            throw std::runtime_error("scroll rendering failed during warmup");
        }
    }

    std::vector<double> timings;
    timings.reserve(options.frames);
    auto mismatches = std::size_t{};

    for (auto frame = std::size_t{}; frame < options.frames; ++frame) {
        if (options.scroll_full) {
            std::fill(pixels.begin(), pixels.end(), veyra::SoftwarePixel{});
        }
        const auto begin = Clock::now();
        const auto painted = paint(options.warmup + frame);
        const auto end = Clock::now();
        if (!painted) {
            throw std::runtime_error("scroll rendering failed during measurement");
        }
        if (options.scroll_verify && last_verify == veyra::RetainedVerifyResult::mismatch) {
            ++mismatches;
        }
        timings.push_back(std::chrono::duration<double, std::nano>(end - begin).count());
    }

    std::sort(timings.begin(), timings.end());
    std::uint64_t checksum = 0;
    for (const auto& pixel : pixels) {
        checksum += pixel.red + pixel.green + pixel.blue + pixel.alpha;
    }

    const auto scenario = options.scroll_verify ? "software_scroll_verify"
        : options.scroll_full                   ? "software_scroll_full"
                                                : "software_scroll_retained";

    std::cout << "scenario,platform,compiler,build_config,width,height,commands,warmup_frames,"
                 "measured_frames,p50_ns,p95_ns,p99_ns,max_ns,checksum";
    if (options.scroll_verify) {
        std::cout << ",mismatches";
    }
    std::cout << '\n'
              << scenario << ',' << VEYRA_BUILD_PLATFORM << ',' << VEYRA_BUILD_COMPILER << ','
              << VEYRA_BUILD_CONFIG << ',' << options.width << ',' << options.height << ',' << bands
              << ',' << options.warmup << ',' << options.frames << ',' << percentile(timings, 50)
              << ',' << percentile(timings, 95) << ',' << percentile(timings, 99) << ','
              << timings.back() << ',' << checksum;
    if (options.scroll_verify) {
        std::cout << ',' << mismatches;
    }
    std::cout << '\n';
    return 0;
}

int run(const Options& options) {
    if (options.scroll_delta > 0 || options.scroll_verify) {
        return run_scroll(options);
    }

    std::vector<veyra::DisplayCommand> commands(options.commands + (options.translated ? 2 : 0));
    std::vector<veyra::LayoutPoint> polyline_points(options.polyline ? options.commands * 4 : 0);
    const auto path_mode = options.path || options.curve || options.winding;
    const auto path_segments_per_command = options.curve ? std::size_t{2} : std::size_t{3};
    std::vector<veyra::SoftwarePathSegment> path_segments(
        path_mode ? options.commands * path_segments_per_command : 0
    );
    std::vector<veyra::SoftwarePathContour> path_contours(path_mode ? options.commands : 0);
    constexpr auto mask_width = std::size_t{12};
    constexpr auto mask_height = std::size_t{18};
    std::array<std::uint8_t, mask_width * mask_height> coverage{};
    for (std::size_t y = 1; y + 1 < mask_height; ++y) {
        for (std::size_t x = 1; x + 1 < mask_width; ++x) {
            coverage[y * mask_width + x] = static_cast<std::uint8_t>((x + y) % 3 == 0 ? 160 : 255);
        }
    }
    std::array<veyra::SoftwarePixel, mask_width * mask_height> image_pixels{};
    for (std::size_t index = 0; index < image_pixels.size(); ++index) {
        image_pixels[index] = {
            .red = static_cast<std::uint8_t>((index * 31) % 256),
            .green = static_cast<std::uint8_t>((index * 67) % 256),
            .blue = static_cast<std::uint8_t>((index * 97) % 256),
            .alpha = static_cast<std::uint8_t>((index % 4) * 85),
        };
    }
    const auto mask = veyra::SoftwareMask{
        .width = mask_width,
        .height = mask_height,
        .stride = mask_width,
        .coverage = coverage,
        .generation = 0,
    };
    const auto image = veyra::SoftwareImage{
        .width = mask_width,
        .height = mask_height,
        .stride = mask_width,
        .pixels = image_pixels,
        .generation = 0,
    };
    const auto color = veyra::DisplayColor{
        .red = 70,
        .green = 120,
        .blue = 210,
        .alpha = static_cast<std::uint8_t>(options.translucent ? 160 : 255),
    };
    const auto image_scale =
        options.scaled_image || options.linear_image ? std::size_t{2} : std::size_t{1};
    const auto image_width = mask_width * image_scale;
    const auto image_height = mask_height * image_scale;
    const auto columns = std::max<std::size_t>(1, options.width / (image_width + 4));
    for (std::size_t index = 0; index < options.commands; ++index) {
        if (options.polyline) {
            const auto top = index * options.height / options.commands;
            const auto bottom = (index + 1) * options.height / options.commands;
            const auto first_point = index * 4;
            polyline_points[first_point] = {.y = static_cast<float>(top)};
            polyline_points[first_point + 1] = {
                .x = static_cast<float>(options.width) / 3.0F,
                .y = static_cast<float>(bottom),
            };
            polyline_points[first_point + 2] = {
                .x = static_cast<float>(options.width) * 2.0F / 3.0F,
                .y = static_cast<float>(top),
            };
            polyline_points[first_point + 3] = {
                .x = static_cast<float>(options.width),
                .y = static_cast<float>(bottom),
            };
            commands[index + static_cast<std::size_t>(options.translated)] = veyra::StrokePolyline{
                .points =
                    std::span<const veyra::LayoutPoint>(polyline_points).subspan(first_point, 4),
                .thickness = 1.5F,
                .color = color,
            };
            continue;
        }
        if (path_mode) {
            const auto top = index * options.height / options.commands;
            const auto bottom = (index + 1) * options.height / options.commands;
            const auto first_segment = index * path_segments_per_command;
            if (options.curve) {
                path_segments[first_segment] = veyra::SoftwareCubicTo{
                    .control_1 =
                        {
                            .x = static_cast<float>(options.width) / 3.0F,
                            .y = static_cast<float>(bottom),
                        },
                    .control_2 =
                        {
                            .x = static_cast<float>(options.width) * 2.0F / 3.0F,
                            .y = static_cast<float>(bottom),
                        },
                    .end = {
                        .x = static_cast<float>(options.width),
                        .y = static_cast<float>(top),
                    },
                };
                path_segments[first_segment + 1] = veyra::SoftwareLineTo{
                    .end = {.x = 0.0F, .y = static_cast<float>(top)},
                };
            } else {
                path_segments[first_segment] = veyra::SoftwareLineTo{
                    .end = {
                        .x = static_cast<float>(options.width),
                        .y = static_cast<float>(top),
                    },
                };
                path_segments[first_segment + 1] = veyra::SoftwareLineTo{
                    .end = {
                        .x = static_cast<float>(options.width),
                        .y = static_cast<float>(bottom),
                    },
                };
                path_segments[first_segment + 2] = veyra::SoftwareLineTo{
                    .end = {.x = 0.0F, .y = static_cast<float>(bottom)},
                };
            }
            path_contours[index] = veyra::SoftwarePathContour{
                .start = {.x = 0.0F, .y = static_cast<float>(top)},
                .segments = std::span<const veyra::SoftwarePathSegment>(path_segments)
                                .subspan(first_segment, path_segments_per_command),
            };
            commands[index + static_cast<std::size_t>(options.translated)] = veyra::FillPath{
                .contours =
                    std::span<const veyra::SoftwarePathContour>(path_contours).subspan(index, 1),
                .color = color,
                .fill_rule = options.winding ? veyra::SoftwarePathFillRule::non_zero
                                             : veyra::SoftwarePathFillRule::even_odd,
            };
            continue;
        }
        if (options.mask) {
            const auto x = (index % columns) * (mask_width + 4);
            const auto y = (index / columns) * (mask_height + 4);
            commands[index + static_cast<std::size_t>(options.translated)] = veyra::DrawMask{
                .x = static_cast<float>(x),
                .y = static_cast<float>(y),
                .mask = &mask,
                .color = color,
            };
            continue;
        }
        if (options.image || options.scaled_image || options.linear_image || options.faded_image) {
            const auto x = (index % columns) * (image_width + 4);
            const auto y = (index / columns) * (image_height + 4);
            commands[index + static_cast<std::size_t>(options.translated)] = veyra::DrawImage{
                .source =
                    {
                        .width = static_cast<float>(image.width),
                        .height = static_cast<float>(image.height),
                    },
                .destination =
                    {
                        .x = static_cast<float>(x),
                        .y = static_cast<float>(y),
                        .width = static_cast<float>(image_width),
                        .height = static_cast<float>(image_height),
                    },
                .image = &image,
                .sampling = options.linear_image ? veyra::SoftwareImageSampling::linear
                                                 : veyra::SoftwareImageSampling::nearest,
                .opacity = static_cast<std::uint8_t>(options.faded_image ? 128 : 255),
            };
            continue;
        }
        if (options.border) {
            const auto top = index * options.height / options.commands;
            const auto bottom = (index + 1) * options.height / options.commands;
            commands[index + static_cast<std::size_t>(options.translated)] = veyra::StrokeRect{
                .bounds =
                    {
                        .x = 0.0F,
                        .y = static_cast<float>(top),
                        .width = static_cast<float>(options.width),
                        .height = static_cast<float>(bottom - top),
                    },
                .thickness = 1.0F,
                .color = color,
            };
            continue;
        }
        if (options.rounded) {
            const auto top = index * options.height / options.commands;
            const auto bottom = (index + 1) * options.height / options.commands;
            commands[index + static_cast<std::size_t>(options.translated)] = veyra::FillRoundedRect{
                .bounds =
                    {
                        .x = 0.0F,
                        .y = static_cast<float>(top),
                        .width = static_cast<float>(options.width),
                        .height = static_cast<float>(bottom - top),
                    },
                .radius = static_cast<float>(bottom - top) * 0.25F,
                .color = color,
            };
            continue;
        }
        if (options.rounded_border) {
            const auto top = index * options.height / options.commands;
            const auto bottom = (index + 1) * options.height / options.commands;
            commands[index + static_cast<std::size_t>(options.translated)] =
                veyra::StrokeRoundedRect{
                    .bounds =
                        {
                            .x = 0.0F,
                            .y = static_cast<float>(top),
                            .width = static_cast<float>(options.width),
                            .height = static_cast<float>(bottom - top),
                        },
                    .radius = static_cast<float>(bottom - top) * 0.25F,
                    .thickness = 1.0F,
                    .color = color,
                };
            continue;
        }
        if (options.line) {
            const auto start_x = index * 97 % options.width;
            const auto start_y = index * 53 % options.height;
            const auto end_x = std::min(options.width, start_x + std::size_t{96});
            const auto end_y = std::min(options.height, start_y + std::size_t{48});
            commands[index + static_cast<std::size_t>(options.translated)] = veyra::StrokeLine{
                .start =
                    {
                        .x = static_cast<float>(start_x),
                        .y = static_cast<float>(start_y),
                    },
                .end =
                    {
                        .x = static_cast<float>(end_x),
                        .y = static_cast<float>(end_y),
                    },
                .thickness = 1.5F,
                .color = color,
            };
            continue;
        }

        const auto top = index * options.height / options.commands;
        const auto bottom = (index + 1) * options.height / options.commands;
        commands[index + static_cast<std::size_t>(options.translated)] = veyra::FillRect{
            .bounds =
                {
                    .x = 0.0F,
                    .y = static_cast<float>(top),
                    .width = static_cast<float>(options.width),
                    .height = static_cast<float>(bottom - top),
                },
            .color = {
                .red = static_cast<std::uint8_t>((index * 31) % 256),
                .green = static_cast<std::uint8_t>((index * 67) % 256),
                .blue = static_cast<std::uint8_t>((index * 97) % 256),
                .alpha = static_cast<std::uint8_t>(options.translucent ? 160 : 255),
            },
        };
    }

    const auto initial_pixel = veyra::SoftwarePixel{
        .alpha = static_cast<std::uint8_t>(options.translucent ? 255 : 0),
    };
    std::vector<veyra::SoftwarePixel> pixels(options.width * options.height, initial_pixel);
    const auto surface = veyra::SoftwareSurface{
        .width = options.width,
        .height = options.height,
        .stride = options.width,
        .pixels = pixels,
    };
    std::array<veyra::LayoutPoint, 1> translations{};
    std::array<double, 5> path_intersections{};
    std::array<std::int8_t, 5> path_windings{};
    if (options.translated) {
        commands.front() = veyra::PushTranslation{
            .offset = {.x = 0.25F, .y = -0.25F},
        };
        commands.back() = veyra::PopTranslation{};
    }
    const auto workspace = veyra::SoftwareRenderWorkspace{
        .translations = options.translated ? std::span<veyra::LayoutPoint>(translations)
                                           : std::span<veyra::LayoutPoint>{},
        .path_intersections =
            path_mode ? std::span<double>(path_intersections) : std::span<double>{},
        .path_windings =
            options.winding ? std::span<std::int8_t>(path_windings) : std::span<std::int8_t>{},
    };
    const auto damage = veyra::LayoutRect{
        .x = static_cast<float>(options.width) / 4.0F,
        .y = static_cast<float>(options.height) / 4.0F,
        .width = static_cast<float>(std::max<std::size_t>(1, options.width / 8)),
        .height = static_cast<float>(std::max<std::size_t>(1, options.height / 8)),
    };
    const auto damage_regions = std::span<const veyra::LayoutRect>(&damage, 1);
    std::vector<double> timings;
    timings.reserve(options.frames);

    const auto render_frame = [&] {
        if (options.damage) {
            return veyra::render_software(commands, surface, workspace, damage_regions);
        }
        return veyra::render_software(commands, surface, workspace);
    };
    for (std::size_t frame = 0; frame < options.warmup; ++frame) {
        std::fill(pixels.begin(), pixels.end(), initial_pixel);
        if (render_frame() != veyra::SoftwareRenderResult::complete) {
            throw std::runtime_error("software rendering failed during warmup");
        }
    }

    for (std::size_t frame = 0; frame < options.frames; ++frame) {
        std::fill(pixels.begin(), pixels.end(), initial_pixel);
        const auto begin = Clock::now();
        const auto result = render_frame();
        const auto end = Clock::now();
        if (result != veyra::SoftwareRenderResult::complete) {
            throw std::runtime_error("software rendering failed during measurement");
        }
        timings.push_back(std::chrono::duration<double, std::nano>(end - begin).count());
    }

    std::sort(timings.begin(), timings.end());
    std::uint64_t checksum = 0;
    for (const auto& pixel : pixels) {
        checksum += pixel.red + pixel.green + pixel.blue + pixel.alpha;
    }

    std::cout << "scenario,platform,compiler,build_config,width,height,commands,warmup_frames,"
                 "measured_frames,p50_ns,p95_ns,p99_ns,max_ns,checksum\n"
              << scenario_name(options) << ',' << VEYRA_BUILD_PLATFORM << ','
              << VEYRA_BUILD_COMPILER << ',' << VEYRA_BUILD_CONFIG << ',' << options.width << ','
              << options.height << ',' << options.commands << ',' << options.warmup << ','
              << options.frames << ',' << percentile(timings, 50) << ',' << percentile(timings, 95)
              << ',' << percentile(timings, 99) << ',' << timings.back() << ',' << checksum << '\n';
    return 0;
}

}

int main(int argc, char** argv) {
    try {
        return run(parse_options(argc, argv));
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 2;
    }
}
