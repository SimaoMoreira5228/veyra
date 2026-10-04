#include "veyra/text/freetype_rasterizer.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;

constexpr std::size_t width = 64;
constexpr std::size_t height = 64;
constexpr std::size_t warmup = 100;
constexpr std::size_t queries = 1'000;

double percentile(const std::vector<double>& timings, std::size_t percent) {
    const auto last = timings.size() - 1;
    const auto index = last / 100 * percent + last % 100 * percent / 100;
    return timings[index];
}

int run() {
    std::array<FT_Byte, width * height * 4> source{};
    for (std::size_t pixel_index = 0; pixel_index < width * height; ++pixel_index) {
        const auto alpha = static_cast<std::uint8_t>((pixel_index * 37U) & 0xffU);
        const auto offset = pixel_index * 4;
        source[offset] = static_cast<FT_Byte>(alpha * 3U / 4U);
        source[offset + 1] = static_cast<FT_Byte>(alpha / 2U);
        source[offset + 2] = static_cast<FT_Byte>(alpha / 4U);
        source[offset + 3] = alpha;
    }

    const FT_Bitmap bitmap{
        .rows = static_cast<unsigned int>(height),
        .width = static_cast<unsigned int>(width),
        .pitch = static_cast<int>(width * 4),
        .buffer = source.data(),
        .pixel_mode = FT_PIXEL_MODE_BGRA,
    };
    std::array<veyra::SoftwarePixel, width * height> output{};
    std::vector<double> timings;
    timings.reserve(queries);
    veyra::GlyphColorImage glyph;

    const auto copy = [&] {
        return veyra::copy_freetype_color_bitmap(bitmap, 0, 0, output, width, glyph);
    };
    for (std::size_t query = 0; query < warmup; ++query) {
        if (copy() != veyra::GlyphRasterizeResult::complete) {
            throw std::runtime_error("color bitmap copy failed during warmup");
        }
    }

    std::uint64_t checksum = 0;
    for (std::size_t query = 0; query < queries; ++query) {
        const auto begin = Clock::now();
        const auto result = copy();
        const auto end = Clock::now();
        if (result != veyra::GlyphRasterizeResult::complete) {
            throw std::runtime_error("color bitmap copy failed during measurement");
        }
        timings.push_back(std::chrono::duration<double, std::micro>(end - begin).count());
        checksum += output[query % output.size()].red + output[query % output.size()].alpha;
    }

    std::sort(timings.begin(), timings.end());
    std::cout << "scenario,platform,compiler,build_config,width,height,warmup_queries,"
                 "measured_queries,p50_us,p95_us,p99_us,max_us,checksum\n"
              << "freetype_color_bitmap_copy," << VEYRA_BUILD_PLATFORM << ','
              << VEYRA_BUILD_COMPILER << ',' << VEYRA_BUILD_CONFIG << ',' << width << ',' << height
              << ',' << warmup << ',' << queries << ',' << percentile(timings, 50) << ','
              << percentile(timings, 95) << ',' << percentile(timings, 99) << ',' << timings.back()
              << ',' << checksum << '\n';
    return 0;
}

}

int main() {
    try {
        return run();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 2;
    }
}
