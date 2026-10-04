#include "veyra/text/font_fallback.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;

constexpr std::size_t warmup = 100;
constexpr std::size_t queries = 1'000;

hb_bool_t map_code_point(
    hb_font_t*,
    void*,
    hb_codepoint_t code_point,
    hb_codepoint_t* glyph,
    void* user_data
) {
    const auto supports_latin = *static_cast<const bool*>(user_data);
    if (supports_latin && code_point < 0x80U) {
        *glyph = code_point;
        return true;
    }
    if (!supports_latin && code_point <= 0x05ffU) {
        *glyph = code_point;
        return true;
    }
    return false;
}

class SyntheticFont {
  public:
    explicit SyntheticFont(bool supports_latin) :
        supports_latin_(supports_latin),
        font_(hb_font_create(hb_face_get_empty()), hb_font_destroy),
        functions_(hb_font_funcs_create(), hb_font_funcs_destroy) {
        hb_font_funcs_set_nominal_glyph_func(
            functions_.get(),
            map_code_point,
            &supports_latin_,
            nullptr
        );
        hb_font_funcs_make_immutable(functions_.get());
        hb_font_set_funcs(font_.get(), functions_.get(), nullptr, nullptr);
    }

    hb_font_t* get() const noexcept {
        return font_.get();
    }

  private:
    bool supports_latin_{};
    std::unique_ptr<hb_font_t, decltype(&hb_font_destroy)> font_;
    std::unique_ptr<hb_font_funcs_t, decltype(&hb_font_funcs_destroy)> functions_;
};

int run() {
    std::string text;
    text.reserve(4096);
    while (text.size() < 4096) {
        text.append("Veyra text fallback keeps אבג runs and wraps e\xcc\x81 graphemes. ");
    }

    SyntheticFont latin(true);
    SyntheticFont international(false);
    std::array<hb_font_t*, 2> fonts{latin.get(), international.get()};
    std::array<veyra::FontFallbackRun, 4096> runs{};
    std::vector<double> timings;
    timings.reserve(queries);
    veyra::FontFallbackResolver resolver(text.size());
    std::size_t run_count = 0;

    for (std::size_t query = 0; query < warmup; ++query) {
        if (resolver.resolve_utf8(text, fonts, runs, run_count) !=
            veyra::FontFallbackResult::complete) {
            throw std::runtime_error("font fallback failed during warmup");
        }
    }

    std::size_t checksum = 0;
    for (std::size_t query = 0; query < queries; ++query) {
        const auto begin = Clock::now();
        const auto result = resolver.resolve_utf8(text, fonts, runs, run_count);
        const auto end = Clock::now();
        if (result != veyra::FontFallbackResult::complete) {
            throw std::runtime_error("font fallback failed during measurement");
        }
        timings.push_back(std::chrono::duration<double, std::micro>(end - begin).count());
        checksum += runs[run_count - 1].byte_offset;
    }

    std::sort(timings.begin(), timings.end());
    const auto percentile = [&timings](std::size_t percent) {
        const auto last = timings.size() - 1;
        const auto index = last / 100 * percent + last % 100 * percent / 100;
        return timings[index];
    };
    std::cout << "scenario,platform,compiler,build_config,text_bytes,warmup_queries,"
                 "measured_queries,p50_us,p95_us,p99_us,max_us,run_count,checksum\n"
              << "font_fallback_4k," << VEYRA_BUILD_PLATFORM << ',' << VEYRA_BUILD_COMPILER << ','
              << VEYRA_BUILD_CONFIG << ',' << text.size() << ',' << warmup << ',' << queries << ','
              << percentile(50) << ',' << percentile(95) << ',' << percentile(99) << ','
              << timings.back() << ',' << run_count << ',' << checksum << '\n';
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
