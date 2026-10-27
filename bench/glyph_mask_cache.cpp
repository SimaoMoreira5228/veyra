#include "veyra/text/rendering/glyph_mask_cache.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;

struct Options {
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

        const auto value = parse_size(argv[++index]);
        if (argument == "--glyphs") {
            options.glyphs = value;
        } else if (argument == "--queries") {
            options.queries = value;
        } else if (argument == "--warmup") {
            options.warmup = value;
        } else {
            throw std::invalid_argument("unknown option: " + std::string(argument));
        }
    }

    if (options.glyphs == 0 || options.glyphs > 4'096 || options.queries == 0 ||
        options.queries > 1'000'000 || options.warmup > 1'000'000) {
        throw std::invalid_argument("glyph and query values must be within limits");
    }
    return options;
}

double percentile(const std::vector<double>& sorted_times, std::size_t percent) {
    const auto last = sorted_times.size() - 1;
    const auto index = last / 100 * percent + last % 100 * percent / 100;
    return sorted_times[index];
}

veyra::GlyphMaskKey glyph_key(std::size_t glyph_id) noexcept {
    return {
        .face_id = 1,
        .variation_id = 0,
        .glyph_id = static_cast<std::uint32_t>(glyph_id),
        .pixel_size = 16,
        .scale_x_16_16 = 65'536,
        .scale_y_16_16 = 65'536,
        .mode = veyra::GlyphRasterMode::grayscale,
    };
}

int run(const Options& options) {
    constexpr auto maximum_width = std::size_t{12};
    constexpr auto maximum_height = std::size_t{18};
    constexpr auto bucket_count = std::size_t{2};
    const auto cache_buckets = options.glyphs * bucket_count;
    const auto slot_count = cache_buckets * 4;
    const auto storage_size = slot_count * maximum_width * maximum_height;
    std::vector<veyra::GlyphMaskCacheSlot> slots(slot_count);
    std::vector<std::uint8_t> storage(storage_size);
    std::array<std::uint8_t, maximum_width * maximum_height> coverage{};
    std::vector<veyra::GlyphMaskKey> keys(options.glyphs);
    std::vector<double> timings;
    timings.reserve(options.queries);

    for (std::size_t index = 0; index < coverage.size(); ++index) {
        coverage[index] = static_cast<std::uint8_t>((index * 37) % 256);
    }
    const veyra::GlyphMask source_mask{
        .mask = {
            .width = maximum_width,
            .height = maximum_height,
            .stride = maximum_width,
            .coverage = coverage,
        },
    };
    veyra::GlyphMaskCache cache(slots, storage, maximum_width, maximum_height);
    if (!cache.valid()) {
        throw std::runtime_error("glyph mask cache setup failed");
    }

    for (std::size_t glyph = 0; glyph < options.glyphs; ++glyph) {
        keys[glyph] = glyph_key(glyph + 1);
        if (cache.insert(keys[glyph], source_mask) != veyra::GlyphMaskCacheResult::inserted) {
            throw std::runtime_error("glyph mask cache population failed");
        }
    }

    for (std::size_t query = 0; query < options.warmup; ++query) {
        if (!cache.find(keys[query % options.glyphs])) {
            throw std::runtime_error("glyph mask cache missed during warmup");
        }
    }

    std::size_t checksum = 0;
    for (std::size_t query = 0; query < options.queries; ++query) {
        const auto& key = keys[query % options.glyphs];
        const auto begin = Clock::now();
        const auto mask = cache.find(key);
        const auto end = Clock::now();
        if (!mask) {
            throw std::runtime_error("glyph mask cache missed during measurement");
        }

        timings.push_back(std::chrono::duration<double, std::nano>(end - begin).count());
        checksum += mask->mask.coverage.front() + mask->mask.coverage.back() + mask->mask.width +
            mask->mask.height;
    }

    std::sort(timings.begin(), timings.end());
    std::cout << "scenario,platform,compiler,build_config,glyphs,slots,warmup_queries,"
                 "measured_queries,p50_ns,p95_ns,p99_ns,max_ns,checksum\n"
              << "glyph_mask_cache_hit," << VEYRA_BUILD_PLATFORM << ',' << VEYRA_BUILD_COMPILER
              << ',' << VEYRA_BUILD_CONFIG << ',' << options.glyphs << ',' << slot_count << ','
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
