#include "veyra/text/freetype_rasterizer.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;
using Library = std::unique_ptr<std::remove_pointer_t<FT_Library>, decltype(&FT_Done_FreeType)>;
using Face = std::unique_ptr<std::remove_pointer_t<FT_Face>, decltype(&FT_Done_Face)>;

struct Options {
    const char* font_path{};
    std::size_t glyphs{128};
    std::size_t queries{100'000};
    std::size_t warmup{1'000};
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
        } else if (argument == "--glyphs") {
            options.glyphs = parse_size(value);
        } else if (argument == "--queries") {
            options.queries = parse_size(value);
        } else if (argument == "--warmup") {
            options.warmup = parse_size(value);
        } else {
            throw std::invalid_argument("unknown option: " + std::string(argument));
        }
    }

    if (options.font_path == nullptr || options.font_path[0] == '\0' || options.glyphs == 0 ||
        options.glyphs > 100'000 || options.queries == 0 || options.queries > 10'000'000 ||
        options.warmup > 10'000'000) {
        throw std::invalid_argument("provide a font and keep glyph and query counts within limits");
    }
    return options;
}

double percentile(const std::vector<double>& sorted_times, std::size_t percent) {
    const auto last = sorted_times.size() - 1;
    const auto index = last / 100 * percent + last % 100 * percent / 100;
    return sorted_times[index];
}

int run(const Options& options) {
    FT_Library raw_library = nullptr;
    if (FT_Init_FreeType(&raw_library) != 0) {
        throw std::runtime_error("could not initialize FreeType");
    }
    Library library(raw_library, FT_Done_FreeType);

    FT_Face raw_face = nullptr;
    if (FT_New_Face(library.get(), options.font_path, 0, &raw_face) != 0) {
        throw std::runtime_error("could not load font file");
    }
    Face face(raw_face, FT_Done_Face);
    if (FT_Set_Pixel_Sizes(face.get(), 0, 24) != 0 || face->num_glyphs <= 0) {
        throw std::runtime_error("font has no usable glyph size");
    }

    const auto glyph_count = std::min(options.glyphs, static_cast<std::size_t>(face->num_glyphs));
    std::array<std::uint8_t, std::size_t{256} * 128> coverage{};
    std::vector<double> timings;
    timings.reserve(options.queries);
    veyra::GlyphMask glyph;

    const auto rasterize = [&](std::size_t query) {
        const auto glyph_id = static_cast<std::uint32_t>(query % glyph_count);
        return veyra::rasterize_freetype_glyph(
            face.get(),
            glyph_id,
            veyra::GlyphRasterMode::grayscale,
            coverage,
            256,
            glyph
        );
    };

    for (std::size_t query = 0; query < options.warmup; ++query) {
        if (rasterize(query) != veyra::GlyphRasterizeResult::complete) {
            throw std::runtime_error("glyph rasterization failed during warmup");
        }
    }

    std::uint64_t checksum = 0;
    for (std::size_t query = 0; query < options.queries; ++query) {
        const auto begin = Clock::now();
        const auto result = rasterize(query);
        const auto end = Clock::now();
        if (result != veyra::GlyphRasterizeResult::complete) {
            throw std::runtime_error("glyph rasterization failed during measurement");
        }

        timings.push_back(std::chrono::duration<double, std::nano>(end - begin).count());
        checksum += glyph.mask.width + glyph.mask.height +
            (glyph.mask.coverage.empty() ? 0 : glyph.mask.coverage.front());
    }

    std::sort(timings.begin(), timings.end());
    std::cout << "scenario,platform,compiler,build_config,glyphs,warmup_queries,measured_queries,"
                 "p50_ns,p95_ns,p99_ns,max_ns,checksum\n"
              << "freetype_glyph_rasterization," << VEYRA_BUILD_PLATFORM << ','
              << VEYRA_BUILD_COMPILER << ',' << VEYRA_BUILD_CONFIG << ',' << glyph_count << ','
              << options.warmup << ',' << options.queries << ',' << percentile(timings, 50) << ','
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
