#include "veyra/text/shaped_run_cache.hpp"

#include <array>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

void require(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

void check_lookup_and_invalidation() {
    std::array<veyra::ShapedRunCacheSlot, 2> slots{};
    std::array<char, 64> text_storage{};
    std::array<veyra::ShapedGlyph, 16> glyph_storage{};
    veyra::ShapedRunCache cache(slots, text_storage, glyph_storage, 32, 8);
    const veyra::ShapedRunCacheKey key{.font_id = 4, .style_id = 9};
    const std::array<veyra::ShapedGlyph, 2> glyphs{
        veyra::ShapedGlyph{.id = 21, .cluster = 0, .x_advance = 500},
        veyra::ShapedGlyph{.id = 22, .cluster = 1, .x_advance = 300},
    };
    std::array<veyra::ShapedGlyph, 2> output{
        veyra::ShapedGlyph{.id = 99},
        veyra::ShapedGlyph{.id = 99},
    };
    std::size_t glyph_count = 7;

    require(cache.valid(), "provided workspace is valid");
    require(
        cache.find(key, "ab", output, glyph_count) == veyra::ShapedRunCacheResult::miss,
        "an empty cache misses"
    );
    require(glyph_count == 0, "a miss resets the output count");
    require(
        cache.insert(key, "ab", glyphs) == veyra::ShapedRunCacheResult::stored,
        "a shaped run enters the cache"
    );
    require(
        cache.find(key, "ab", output, glyph_count) == veyra::ShapedRunCacheResult::hit,
        "matching font, style, and text hit"
    );
    require(glyph_count == 2, "a hit reports the glyph count");
    require(
        output[0].id == 21 && output[1].id == 22 && output[1].x_advance == 300,
        "a hit copies the complete shaped run"
    );

    const auto unchanged = output;
    std::array<veyra::ShapedGlyph, 1> short_output{output[0]};
    require(
        cache.find(key, "ab", short_output, glyph_count) ==
            veyra::ShapedRunCacheResult::insufficient_output,
        "short output is reported"
    );
    require(glyph_count == 2, "short output reports required capacity");
    require(short_output[0].id == unchanged[0].id, "short output stays unchanged");

    require(
        cache.find({.font_id = 5, .style_id = 9}, "ab", output, glyph_count) ==
            veyra::ShapedRunCacheResult::miss,
        "font identity invalidates a hit"
    );
    require(
        cache.find({.font_id = 4, .style_id = 10}, "ab", output, glyph_count) ==
            veyra::ShapedRunCacheResult::miss,
        "style identity invalidates a hit"
    );
    require(
        cache.find(key, "ac", output, glyph_count) == veyra::ShapedRunCacheResult::miss,
        "text changes invalidate a hit"
    );

    require(
        cache.insert(key, "this string definitely exceeds one entry", glyphs) ==
            veyra::ShapedRunCacheResult::entry_too_large,
        "oversized text is not cached"
    );
    require(
        cache.find(key, "ab", output, glyph_count) == veyra::ShapedRunCacheResult::hit,
        "an oversized insert leaves existing entries intact"
    );

    cache.clear();
    require(
        cache.find(key, "ab", output, glyph_count) == veyra::ShapedRunCacheResult::miss,
        "clear invalidates every entry"
    );

    std::array<veyra::ShapedRunCacheSlot, 1> direct_slots{};
    std::array<char, 32> direct_text_storage{};
    std::array<veyra::ShapedGlyph, 8> direct_glyph_storage{};
    veyra::ShapedRunCache
        direct_cache(direct_slots, direct_text_storage, direct_glyph_storage, 32, 8);
    const std::array<veyra::ShapedGlyph, 1> replacement_glyph{
        veyra::ShapedGlyph{.id = 77},
    };
    require(
        direct_cache.insert(key, "ab", glyphs) == veyra::ShapedRunCacheResult::stored,
        "first direct-mapped entry is stored"
    );
    require(
        direct_cache.insert(key, "ac", replacement_glyph) == veyra::ShapedRunCacheResult::stored,
        "a colliding entry replaces its slot"
    );
    require(
        direct_cache.find(key, "ab", output, glyph_count) == veyra::ShapedRunCacheResult::miss,
        "a collision cannot return a different text's glyphs"
    );
    require(
        direct_cache.find(key, "ac", output, glyph_count) == veyra::ShapedRunCacheResult::hit &&
            glyph_count == 1 && output[0].id == 77,
        "the replacement entry remains readable"
    );
}

void check_font_scale_invalidates() {
    std::array<veyra::ShapedRunCacheSlot, 4> slots{};
    std::array<char, 128> text_storage{};
    std::array<veyra::ShapedGlyph, 32> glyph_storage{};
    veyra::ShapedRunCache cache(slots, text_storage, glyph_storage, 32, 8);
    const veyra::ShapedRunCacheKey unscaled{.font_id = 4, .style_id = 9};
    const veyra::ShapedRunCacheKey scaled{
        .font_id = 4,
        .style_id = 9,
        .font_scale_x = 2048,
        .font_scale_y = 2048,
    };
    const std::array<veyra::ShapedGlyph, 2> glyphs{
        veyra::ShapedGlyph{.id = 21, .cluster = 0, .x_advance = 500},
        veyra::ShapedGlyph{.id = 22, .cluster = 1, .x_advance = 300},
    };
    const std::array<veyra::ShapedGlyph, 2> rescaled_glyphs{
        veyra::ShapedGlyph{.id = 21, .cluster = 0, .x_advance = 1000},
        veyra::ShapedGlyph{.id = 22, .cluster = 1, .x_advance = 600},
    };
    std::array<veyra::ShapedGlyph, 2> output{};
    std::size_t glyph_count = 0;

    require(
        cache.insert(unscaled, "ab", glyphs) == veyra::ShapedRunCacheResult::stored,
        "unscaled run enters the cache"
    );
    require(
        cache.find(scaled, "ab", output, glyph_count) == veyra::ShapedRunCacheResult::miss,
        "a font scale change misses instead of serving stale advances"
    );
    require(glyph_count == 0, "a scale miss resets the output count");

    const veyra::ShapedRunCacheKey wide{
        .font_id = 4,
        .style_id = 9,
        .font_scale_x = 2048,
        .font_scale_y = 1024,
    };
    require(
        cache.find(wide, "ab", output, glyph_count) == veyra::ShapedRunCacheResult::miss,
        "a single-axis scale change misses as well"
    );

    require(
        cache.find(unscaled, "ab", output, glyph_count) == veyra::ShapedRunCacheResult::hit,
        "original run still hits under its own scale"
    );
    require(
        output[0].x_advance == 500 && output[1].x_advance == 300,
        "original hit keeps its own advances"
    );

    require(
        cache.insert(scaled, "ab", rescaled_glyphs) == veyra::ShapedRunCacheResult::stored,
        "rescaled run enters the cache"
    );
    require(
        cache.find(scaled, "ab", output, glyph_count) == veyra::ShapedRunCacheResult::hit,
        "rescaled run hits under its own scale"
    );
    require(
        output[0].x_advance == 1000 && output[1].x_advance == 600,
        "rescaled hit returns rescaled advances"
    );
}

void check_workspace_limits_and_empty_runs() {
    std::array<veyra::ShapedRunCacheSlot, 1> slots{};
    std::array<char, 2> short_text_storage{};
    std::array<veyra::ShapedGlyph, 1> glyph_storage{};
    veyra::ShapedRunCache invalid_cache(slots, short_text_storage, glyph_storage, 4, 1);
    require(!invalid_cache.valid(), "undersized workspace is rejected");
    std::array<veyra::ShapedGlyph, 1> output{};
    std::size_t glyph_count = 0;
    require(
        invalid_cache.find({}, "", output, glyph_count) ==
            veyra::ShapedRunCacheResult::invalid_workspace,
        "an invalid cache reports its state"
    );

    std::array<char, 4> text_storage{};
    veyra::ShapedRunCache cache(slots, text_storage, glyph_storage, 4, 1);
    require(
        cache.insert({}, "", {}) == veyra::ShapedRunCacheResult::stored,
        "empty runs can be cached"
    );
    require(
        cache.find({}, "", output, glyph_count) == veyra::ShapedRunCacheResult::hit,
        "empty text finds its cached run"
    );
    require(glyph_count == 0, "empty cached runs contain no glyphs");
}

}

int main() {
    try {
        check_lookup_and_invalidation();
        check_font_scale_invalidates();
        check_workspace_limits_and_empty_runs();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
