#include "veyra/render/software_renderer.hpp"

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
    bool damage{};
    bool translucent{};
    bool mask{};
    bool path{};
    bool curve{};
    bool winding{};
    bool image{};
    bool scaled_image{};
    bool border{};
    bool line{};
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
        if (argument == "--border") {
            options.border = true;
            continue;
        }
        if (argument == "--line") {
            options.line = true;
            continue;
        }
        if (argument == "--translated") {
            options.translated = true;
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
        } else {
            throw std::invalid_argument("unknown option: " + std::string(argument));
        }
    }

    const auto special_modes = static_cast<unsigned int>(options.mask) +
        static_cast<unsigned int>(options.path) + static_cast<unsigned int>(options.curve) +
        static_cast<unsigned int>(options.winding) + static_cast<unsigned int>(options.image) +
        static_cast<unsigned int>(options.scaled_image) +
        static_cast<unsigned int>(options.border) + static_cast<unsigned int>(options.line);
    if (special_modes > 1) {
        throw std::invalid_argument(
            "choose one of --mask, --path, --curve, --winding, --image, --scaled-image, "
            "--border, or --line"
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
    if (options.translucent) {
        return options.damage ? "software_rgba_damage" : "software_rgba_full";
    }
    return options.damage ? "software_rect_damage" : "software_rect_full";
}

int run(const Options& options) {
    std::vector<veyra::DisplayCommand> commands(options.commands + (options.translated ? 2 : 0));
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
    };
    const auto image = veyra::SoftwareImage{
        .width = mask_width,
        .height = mask_height,
        .stride = mask_width,
        .pixels = image_pixels,
    };
    const auto color = veyra::DisplayColor{
        .red = 70,
        .green = 120,
        .blue = 210,
        .alpha = static_cast<std::uint8_t>(options.translucent ? 160 : 255),
    };
    const auto image_scale = options.scaled_image ? std::size_t{2} : std::size_t{1};
    const auto image_width = mask_width * image_scale;
    const auto image_height = mask_height * image_scale;
    const auto columns = std::max<std::size_t>(1, options.width / (image_width + 4));
    for (std::size_t index = 0; index < options.commands; ++index) {
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
        if (options.image || options.scaled_image) {
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
