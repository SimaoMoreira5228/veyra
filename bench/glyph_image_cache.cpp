#include "veyra/text/glyph_image_cache.hpp"

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

double percentile(const std::vector<double>& sorted_times, std::size_t percent) {
    const auto last = sorted_times.size() - 1;
    const auto index = last / 100 * percent + last % 100 * percent / 100;
    return sorted_times[index];
}

int run() {
    constexpr auto glyph_count = std::size_t{128};
    constexpr auto query_count = std::size_t{100'000};
    constexpr auto warmup_count = std::size_t{1'000};
    constexpr auto maximum_width = std::size_t{16};
    constexpr auto maximum_height = std::size_t{16};
    constexpr auto slot_count = glyph_count * 8;

    std::array<veyra::GlyphImageCacheSlot, slot_count> slots{};
    std::vector<veyra::SoftwarePixel> storage(slot_count * maximum_width * maximum_height);
    std::array<veyra::SoftwarePixel, maximum_width * maximum_height> pixels{};
    std::array<veyra::GlyphImageKey, glyph_count> keys{};
    std::vector<double> timings;
    timings.reserve(query_count);

    for (std::size_t index = 0; index < pixels.size(); ++index) {
        pixels[index] = {
            .red = static_cast<std::uint8_t>(index),
            .green = static_cast<std::uint8_t>(index * 3),
            .blue = static_cast<std::uint8_t>(index * 7),
            .alpha = 255,
        };
    }

    const veyra::GlyphImage source{
        .image = {
            .width = maximum_width,
            .height = maximum_height,
            .stride = maximum_width,
            .pixels = pixels,
        },
    };
    veyra::GlyphImageCache cache(slots, storage, maximum_width, maximum_height);
    if (!cache.valid()) {
        throw std::runtime_error("glyph image cache setup failed");
    }

    for (std::size_t glyph = 0; glyph < glyph_count; ++glyph) {
        keys[glyph] = {
            .face_id = 1,
            .glyph_id = static_cast<std::uint32_t>(glyph + 1),
            .pixel_size = 16,
        };
        if (cache.insert(keys[glyph], source) != veyra::GlyphImageCacheResult::inserted) {
            throw std::runtime_error("glyph image cache population failed");
        }
    }

    for (std::size_t query = 0; query < warmup_count; ++query) {
        if (!cache.find(keys[query % glyph_count])) {
            throw std::runtime_error("glyph image cache missed during warmup");
        }
    }

    std::uint64_t checksum = 0;
    for (std::size_t query = 0; query < query_count; ++query) {
        const auto begin = Clock::now();
        const auto image = cache.find(keys[query % glyph_count]);
        const auto end = Clock::now();
        if (!image) {
            throw std::runtime_error("glyph image cache missed during measurement");
        }

        timings.push_back(std::chrono::duration<double, std::nano>(end - begin).count());
        checksum += image->image.pixels.front().red + image->image.pixels.back().blue +
            image->image.width + image->image.height;
    }

    std::sort(timings.begin(), timings.end());
    std::cout << "scenario,platform,compiler,build_config,glyphs,slots,warmup_queries,"
                 "measured_queries,p50_ns,p95_ns,p99_ns,max_ns,checksum\n"
              << "glyph_image_cache_hit," << VEYRA_BUILD_PLATFORM << ',' << VEYRA_BUILD_COMPILER
              << ',' << VEYRA_BUILD_CONFIG << ',' << glyph_count << ',' << slot_count << ','
              << warmup_count << ',' << query_count << ',' << percentile(timings, 50) << ','
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
