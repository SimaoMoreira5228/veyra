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
    bool image{};
    bool border{};
    bool line{};
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
        if (argument == "--image") {
            options.image = true;
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
        static_cast<unsigned int>(options.image) + static_cast<unsigned int>(options.border) +
        static_cast<unsigned int>(options.line);
    if (special_modes > 1) {
        throw std::invalid_argument("choose one of --mask, --image, --border, or --line");
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
    if (options.mask) {
        if (options.translucent) {
            return options.damage ? "software_mask_alpha_damage" : "software_mask_alpha_full";
        }
        return options.damage ? "software_mask_damage" : "software_mask_full";
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
    std::vector<veyra::DisplayCommand> commands(options.commands);
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
    const auto columns = std::max<std::size_t>(1, options.width / (mask_width + 4));
    for (std::size_t index = 0; index < options.commands; ++index) {
        if (options.mask) {
            const auto x = (index % columns) * (mask_width + 4);
            const auto y = (index / columns) * (mask_height + 4);
            commands[index] = veyra::DrawMask{
                .x = static_cast<std::int32_t>(x),
                .y = static_cast<std::int32_t>(y),
                .mask = &mask,
                .color = color,
            };
            continue;
        }
        if (options.image) {
            const auto x = (index % columns) * (mask_width + 4);
            const auto y = (index / columns) * (mask_height + 4);
            commands[index] = veyra::DrawImage{
                .x = static_cast<std::int32_t>(x),
                .y = static_cast<std::int32_t>(y),
                .image = &image,
            };
            continue;
        }
        if (options.border) {
            const auto top = index * options.height / options.commands;
            const auto bottom = (index + 1) * options.height / options.commands;
            commands[index] = veyra::StrokeRect{
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
            commands[index] = veyra::StrokeLine{
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
        commands[index] = veyra::FillRect{
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
    const auto workspace = veyra::SoftwareRenderWorkspace{};
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
