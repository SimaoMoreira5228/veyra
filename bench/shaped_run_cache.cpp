#include "veyra/text/shaping/shaped_run_cache.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;

constexpr std::size_t warmup = 100;
constexpr std::size_t queries = 1'000;

hb_bool_t
map_code_point(hb_font_t*, void*, hb_codepoint_t code_point, hb_codepoint_t* glyph, void*) {
    *glyph = code_point;
    return true;
}

class SyntheticFont {
  public:
    SyntheticFont() :
        font_(hb_font_create(hb_face_get_empty()), hb_font_destroy),
        functions_(hb_font_funcs_create(), hb_font_funcs_destroy) {
        hb_font_funcs_set_nominal_glyph_func(functions_.get(), map_code_point, nullptr, nullptr);
        hb_font_funcs_make_immutable(functions_.get());
        hb_font_set_funcs(font_.get(), functions_.get(), nullptr, nullptr);
    }

    hb_font_t* get() const noexcept {
        return font_.get();
    }

  private:
    std::unique_ptr<hb_font_t, decltype(&hb_font_destroy)> font_;
    std::unique_ptr<hb_font_funcs_t, decltype(&hb_font_funcs_destroy)> functions_;
};

template <typename Measure> std::vector<double> measure(Measure&& operation) {
    std::vector<double> timings;
    timings.reserve(queries);
    for (std::size_t query = 0; query < warmup; ++query) {
        operation();
    }
    for (std::size_t query = 0; query < queries; ++query) {
        const auto begin = Clock::now();
        operation();
        const auto end = Clock::now();
        timings.push_back(std::chrono::duration<double, std::micro>(end - begin).count());
    }
    std::sort(timings.begin(), timings.end());
    return timings;
}

double percentile(const std::vector<double>& timings, std::size_t percent) {
    const auto last = timings.size() - 1;
    const auto index = last / 100 * percent + last % 100 * percent / 100;
    return timings[index];
}

int run() {
    constexpr std::string_view text =
        "Search channels, recent files, and settings without reshaping every frame.";
    const veyra::ShapedRunCacheKey key{
        .font_id = 1,
        .style_id = 2,
        .direction = veyra::TextDirection::left_to_right,
        .script_tag = veyra::shaped_run_script_tag("Latn"),
        .language_id = veyra::shaped_run_language_id("en"),
    };
    SyntheticFont font;
    veyra::TextShaper shaper(text.size());
    std::array<veyra::ShapedGlyph, 128> glyphs{};
    std::array<veyra::ShapedRunCacheSlot, 1> slots{};
    std::array<char, 128> text_storage{};
    std::array<veyra::ShapedGlyph, 128> glyph_storage{};
    veyra::ShapedRunCache cache(slots, text_storage, glyph_storage, text_storage.size(), 128);
    std::size_t glyph_count = 0;
    const auto shape = [&] {
        const auto result = shaper.shape_utf8(
            font.get(),
            text,
            {.direction = veyra::TextDirection::left_to_right, .script = "Latn", .language = "en"},
            glyphs,
            glyph_count
        );
        if (result != veyra::ShapeResult::complete) {
            throw std::runtime_error("text shaping failed");
        }
    };
    shape();
    if (cache.insert(key, text, std::span<const veyra::ShapedGlyph>(glyphs).first(glyph_count)) !=
        veyra::ShapedRunCacheResult::stored) {
        throw std::runtime_error("shaped run could not be cached");
    }
    const auto shaped_count = glyph_count;
    const auto shaped_glyphs = glyphs;
    if (cache.find(key, text, glyphs, glyph_count) != veyra::ShapedRunCacheResult::hit ||
        glyph_count != shaped_count ||
        !std::equal(
            shaped_glyphs.begin(),
            shaped_glyphs.begin() + static_cast<std::ptrdiff_t>(shaped_count),
            glyphs.begin(),
            [](const veyra::ShapedGlyph& left, const veyra::ShapedGlyph& right) {
                return left.id == right.id && left.cluster == right.cluster &&
                    left.x_advance == right.x_advance && left.y_advance == right.y_advance &&
                    left.x_offset == right.x_offset && left.y_offset == right.y_offset;
            }
        )) {
        throw std::runtime_error("cached glyphs differ from the shaped run");
    }

    auto shape_timings = measure(shape);
    auto cache_timings = measure([&] {
        if (cache.find(key, text, glyphs, glyph_count) != veyra::ShapedRunCacheResult::hit) {
            throw std::runtime_error("shaped run cache missed during measurement");
        }
    });

    std::cout << "scenario,platform,compiler,build_config,text_bytes,warmup_queries,"
                 "measured_queries,shape_p50_us,shape_p95_us,cache_p50_us,cache_p95_us,"
                 "glyph_count\n"
              << "shaped_run_cache," << VEYRA_BUILD_PLATFORM << ',' << VEYRA_BUILD_COMPILER << ','
              << VEYRA_BUILD_CONFIG << ',' << text.size() << ',' << warmup << ',' << queries << ','
              << percentile(shape_timings, 50) << ',' << percentile(shape_timings, 95) << ','
              << percentile(cache_timings, 50) << ',' << percentile(cache_timings, 95) << ','
              << glyph_count << '\n';
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
