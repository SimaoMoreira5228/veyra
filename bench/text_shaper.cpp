#include "veyra/text/shaping/glyph_run.hpp"
#include "veyra/text/shaping/shaper.hpp"

#include <hb-ot.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;
using Blob = std::unique_ptr<hb_blob_t, decltype(&hb_blob_destroy)>;
using Face = std::unique_ptr<hb_face_t, decltype(&hb_face_destroy)>;
using Font = std::unique_ptr<hb_font_t, decltype(&hb_font_destroy)>;

struct Options {
    const char* font_path{};
    std::size_t frames{5'000};
    std::size_t warmup{500};
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
        if (index + 1 >= argc) {
            throw std::invalid_argument("missing value for " + std::string(argument));
        }
        const auto* value = argv[++index];
        if (argument == "--font") {
            options.font_path = value;
        } else if (argument == "--frames") {
            options.frames = parse_size(value);
        } else if (argument == "--warmup") {
            options.warmup = parse_size(value);
        } else {
            throw std::invalid_argument("unknown option: " + std::string(argument));
        }
    }

    if (options.font_path == nullptr || options.font_path[0] == '\0' || options.frames == 0 ||
        options.frames > 10'000'000 || options.warmup > 10'000'000) {
        throw std::invalid_argument("provide a font and keep frame counts within limits");
    }
    return options;
}

double percentile(const std::vector<double>& sorted_times, std::size_t percent) {
    const auto last = sorted_times.size() - 1;
    const auto index = last / 100 * percent + last % 100 * percent / 100;
    return sorted_times[index];
}

Font load_font(const Options& options) {
    Blob blob(hb_blob_create_from_file_or_fail(options.font_path), hb_blob_destroy);
    if (!blob) {
        throw std::runtime_error("could not load font file");
    }

    Face face(hb_face_create(blob.get(), 0), hb_face_destroy);
    const auto units_per_em = face ? hb_face_get_upem(face.get()) : 0;
    if (units_per_em == 0 ||
        units_per_em > static_cast<unsigned int>(std::numeric_limits<int>::max())) {
        throw std::runtime_error("font has no usable units-per-em value");
    }

    Font font(hb_font_create(face.get()), hb_font_destroy);
    if (!font) {
        throw std::runtime_error("could not create HarfBuzz font");
    }
    hb_ot_font_set_funcs(font.get());
    const auto scale = static_cast<int>(units_per_em);
    hb_font_set_scale(font.get(), scale, scale);
    return font;
}

int run(const Options& options) {
    auto font = load_font(options);
    constexpr std::string_view text = "office affine efficient rendering";
    veyra::TextShaper shaper(128);
    std::array<veyra::ShapedGlyph, 64> glyphs{};
    std::size_t glyph_count = 0;
    const auto shape = [&] {
        return shaper.shape_utf8(
            font.get(),
            text,
            {
                .direction = veyra::TextDirection::left_to_right,
                .script = "Latn",
                .language = "en",
            },
            glyphs,
            glyph_count
        );
    };

    for (std::size_t frame = 0; frame < options.warmup; ++frame) {
        if (shape() != veyra::ShapeResult::complete) {
            throw std::runtime_error("shaping failed during warmup");
        }
    }

    std::vector<double> timings;
    timings.reserve(options.frames);
    for (std::size_t frame = 0; frame < options.frames; ++frame) {
        const auto begin = Clock::now();
        const auto result = shape();
        const auto end = Clock::now();
        if (result != veyra::ShapeResult::complete) {
            throw std::runtime_error("shaping failed during measurement");
        }
        timings.push_back(std::chrono::duration<double, std::nano>(end - begin).count());
    }

    std::sort(timings.begin(), timings.end());
    std::uint64_t checksum = 0;
    for (std::size_t index = 0; index < glyph_count; ++index) {
        checksum += glyphs[index].id + glyphs[index].cluster + glyphs[index].x_advance;
    }

    std::cout << "scenario,platform,compiler,build_config,input_bytes,glyphs,warmup_runs,"
                 "measured_runs,p50_ns,p95_ns,p99_ns,max_ns,checksum\n"
              << "harfbuzz_shape_utf8," << VEYRA_BUILD_PLATFORM << ',' << VEYRA_BUILD_COMPILER
              << ',' << VEYRA_BUILD_CONFIG << ',' << text.size() << ',' << glyph_count << ','
              << options.warmup << ',' << options.frames << ',' << percentile(timings, 50) << ','
              << percentile(timings, 95) << ',' << percentile(timings, 99) << ',' << timings.back()
              << ',' << checksum << '\n';

    constexpr auto mask_width = std::size_t{1};
    constexpr auto mask_height = std::size_t{1};
    const auto slot_count = std::max(std::size_t{4}, glyph_count * 128);
    std::vector<veyra::GlyphMaskCacheSlot> slots(slot_count);
    std::vector<std::uint8_t> coverage_storage(slot_count * mask_width * mask_height);
    const std::array<std::uint8_t, 1> coverage{255};
    const auto mask = veyra::GlyphMask{
        .mask = {
            .width = mask_width,
            .height = mask_height,
            .stride = mask_width,
            .coverage = coverage,
            .generation = 0,
        },
    };
    veyra::GlyphMaskCache cache(slots, coverage_storage, mask_width, mask_height);
    if (!cache.valid()) {
        throw std::runtime_error("glyph draw cache setup failed");
    }

    auto key = veyra::GlyphMaskKey{
        .face_id = 1,
        .pixel_size = 16,
    };
    for (std::size_t index = 0; index < glyph_count; ++index) {
        key.glyph_id = glyphs[index].id;
        const auto insert_result = cache.insert(key, mask);
        if (insert_result != veyra::GlyphMaskCacheResult::inserted &&
            insert_result != veyra::GlyphMaskCacheResult::updated) {
            throw std::runtime_error("glyph draw cache population failed");
        }
    }

    int font_x_scale = 0;
    int font_y_scale = 0;
    hb_font_get_scale(font.get(), &font_x_scale, &font_y_scale);
    std::array<veyra::SoftwareMask, 64> mask_views{};
    std::array<veyra::DisplayCommand, 64> commands{};
    std::size_t command_count = 0;
    const auto build_draws = [&] {
        return veyra::build_glyph_draw_commands(
            std::span<const veyra::ShapedGlyph>(glyphs).first(glyph_count),
            cache,
            key,
            {.x = 0.0F, .y = 20.0F},
            static_cast<double>(font_x_scale),
            static_cast<double>(font_y_scale),
            {.red = 20, .green = 30, .blue = 40},
            mask_views,
            commands,
            command_count
        );
    };

    for (std::size_t frame = 0; frame < options.warmup; ++frame) {
        if (build_draws() != veyra::GlyphRunResult::complete) {
            throw std::runtime_error("glyph draw list failed during warmup");
        }
    }

    std::vector<double> draw_timings;
    draw_timings.reserve(options.frames);
    for (std::size_t frame = 0; frame < options.frames; ++frame) {
        const auto begin = Clock::now();
        const auto result = build_draws();
        const auto end = Clock::now();
        if (result != veyra::GlyphRunResult::complete) {
            throw std::runtime_error("glyph draw list failed during measurement");
        }
        draw_timings.push_back(std::chrono::duration<double, std::nano>(end - begin).count());
    }

    std::sort(draw_timings.begin(), draw_timings.end());
    for (std::size_t index = 0; index < command_count; ++index) {
        const auto* draw = std::get_if<veyra::DrawMask>(&commands[index]);
        if (draw == nullptr) {
            throw std::runtime_error("glyph draw list emitted an unexpected command");
        }
        checksum += static_cast<std::uint32_t>(draw->x) + static_cast<std::uint32_t>(draw->y);
    }

    std::cout << "shaped_glyph_draw_list," << VEYRA_BUILD_PLATFORM << ',' << VEYRA_BUILD_COMPILER
              << ',' << VEYRA_BUILD_CONFIG << ',' << text.size() << ',' << glyph_count << ','
              << options.warmup << ',' << options.frames << ',' << percentile(draw_timings, 50)
              << ',' << percentile(draw_timings, 95) << ',' << percentile(draw_timings, 99) << ','
              << draw_timings.back() << ',' << checksum << '\n';

    constexpr auto image_width = std::size_t{1};
    constexpr auto image_height = std::size_t{1};
    const auto image_slot_count = std::max(std::size_t{4}, glyph_count * 128);
    std::vector<veyra::GlyphImageCacheSlot> image_slots(image_slot_count);
    std::vector<veyra::SoftwarePixel> image_storage(image_slot_count * image_width * image_height);
    const std::array<veyra::SoftwarePixel, 1> pixel{
        veyra::SoftwarePixel{.red = 220, .green = 180, .blue = 40, .alpha = 255},
    };
    const auto image = veyra::GlyphImage{
        .image = {
            .width = image_width,
            .height = image_height,
            .stride = image_width,
            .pixels = pixel,
            .generation = 0,
        },
    };
    veyra::GlyphImageCache image_cache(image_slots, image_storage, image_width, image_height);
    if (!image_cache.valid()) {
        throw std::runtime_error("color glyph draw cache setup failed");
    }

    auto image_key = veyra::GlyphImageKey{
        .face_id = 1,
        .pixel_size = 16,
    };
    for (std::size_t index = 0; index < glyph_count; ++index) {
        image_key.glyph_id = glyphs[index].id;
        const auto insert_result = image_cache.insert(image_key, image);
        if (insert_result != veyra::GlyphImageCacheResult::inserted &&
            insert_result != veyra::GlyphImageCacheResult::updated) {
            throw std::runtime_error("color glyph draw cache population failed");
        }
    }

    std::array<veyra::SoftwareImage, 64> image_views{};
    const auto build_color_draws = [&] {
        return veyra::build_color_glyph_draw_commands(
            std::span<const veyra::ShapedGlyph>(glyphs).first(glyph_count),
            image_cache,
            image_key,
            {.x = 0.0F, .y = 20.0F},
            static_cast<double>(font_x_scale),
            static_cast<double>(font_y_scale),
            image_views,
            commands,
            command_count
        );
    };

    for (std::size_t frame = 0; frame < options.warmup; ++frame) {
        if (build_color_draws() != veyra::GlyphRunResult::complete) {
            throw std::runtime_error("color glyph draw list failed during warmup");
        }
    }

    std::vector<double> color_draw_timings;
    color_draw_timings.reserve(options.frames);
    for (std::size_t frame = 0; frame < options.frames; ++frame) {
        const auto begin = Clock::now();
        const auto result = build_color_draws();
        const auto end = Clock::now();
        if (result != veyra::GlyphRunResult::complete) {
            throw std::runtime_error("color glyph draw list failed during measurement");
        }
        color_draw_timings.push_back(std::chrono::duration<double, std::nano>(end - begin).count());
    }

    std::sort(color_draw_timings.begin(), color_draw_timings.end());
    for (std::size_t index = 0; index < command_count; ++index) {
        const auto* draw = std::get_if<veyra::DrawImage>(&commands[index]);
        if (draw == nullptr) {
            throw std::runtime_error("color glyph draw list emitted an unexpected command");
        }
        checksum += static_cast<std::uint32_t>(draw->destination.x) +
            static_cast<std::uint32_t>(draw->destination.y);
    }

    std::cout << "shaped_color_glyph_draw_list," << VEYRA_BUILD_PLATFORM << ','
              << VEYRA_BUILD_COMPILER << ',' << VEYRA_BUILD_CONFIG << ',' << text.size() << ','
              << glyph_count << ',' << options.warmup << ',' << options.frames << ','
              << percentile(color_draw_timings, 50) << ',' << percentile(color_draw_timings, 95)
              << ',' << percentile(color_draw_timings, 99) << ',' << color_draw_timings.back()
              << ',' << checksum << '\n';
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
