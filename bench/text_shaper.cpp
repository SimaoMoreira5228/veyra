#include "veyra/text/shaper.hpp"

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
