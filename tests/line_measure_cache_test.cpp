#include "veyra/text/rendering/line_measure_cache.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

constexpr float kAdvancePerByte = 8.0F;
constexpr float kLineHeight = 16.0F;
constexpr float kWrap256 = 256.0F;

void require(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

veyra::LineMeasureKey base_key() {
    return veyra::LineMeasureKey{
        .font_id = 4,
        .style_id = 9,
        .font_scale_x = 2048,
        .font_scale_y = 2048,
        .wrap_width = kWrap256,
        .style_generation = 7,
    };
}

veyra::LineMeasure fold_measure(std::string_view text, float wrap_width) noexcept {
    auto measure = veyra::LineMeasure{};
    if (text.empty()) {
        return measure;
    }
    const auto total = static_cast<float>(text.size()) * kAdvancePerByte;
    const auto bytes_per_line = static_cast<std::size_t>(wrap_width / kAdvancePerByte);
    if (bytes_per_line == 0 || total <= wrap_width) {
        measure.width = total;
        measure.height = kLineHeight;
        measure.line_count = 1;
        return measure;
    }
    measure.line_count =
        static_cast<std::uint32_t>((text.size() + bytes_per_line - 1) / bytes_per_line);
    measure.width = wrap_width;
    measure.height = static_cast<float>(measure.line_count) * kLineHeight;
    return measure;
}

veyra::LineMeasureCacheResult layout_measure(
    veyra::LineMeasureCache& cache,
    veyra::LineMeasureKey key,
    std::string_view text,
    std::size_t& reshape_work,
    veyra::LineMeasure& out
) {
    const auto cached = cache.find(key, text, out);
    if (cached == veyra::LineMeasureCacheResult::hit) {
        return cached;
    }
    require(
        cached == veyra::LineMeasureCacheResult::miss,
        "layout measure only runs on a clean miss"
    );
    ++reshape_work;
    out = fold_measure(text, key.wrap_width);
    require(
        cache.insert(key, text, out) == veyra::LineMeasureCacheResult::stored,
        "layout measure stores its result"
    );
    return cached;
}

void check_lookup_and_key_isolation() {
    std::array<veyra::LineMeasureCacheSlot, 8> slots{};
    std::array<char, 512> text_storage{};
    veyra::LineMeasureCache cache(slots, text_storage, 64);
    const auto key = base_key();
    const auto stored = veyra::LineMeasure{.width = 88.0F, .height = 16.0F, .line_count = 1};
    auto measure = veyra::LineMeasure{.width = -1.0F, .height = -1.0F, .line_count = 99};
    auto misses = std::size_t{0};

    require(cache.valid(), "provided workspace is valid");
    require(
        cache.find(key, "ab", measure) == veyra::LineMeasureCacheResult::miss,
        "an empty cache misses"
    );
    require(
        measure.width == -1.0F && measure.line_count == 99,
        "a miss leaves the output untouched"
    );
    require(
        cache.insert(key, "ab", stored) == veyra::LineMeasureCacheResult::stored,
        "a measured block enters the cache"
    );
    require(
        cache.find(key, "ab", measure) == veyra::LineMeasureCacheResult::hit,
        "matching font, style, scale, width, generation, and text hit"
    );
    require(
        measure.width == 88.0F && measure.height == 16.0F && measure.line_count == 1,
        "a hit replays the stored extents"
    );

    auto probe = key;
    probe.font_id = 5;
    require(
        cache.find(probe, "ab", measure) == veyra::LineMeasureCacheResult::miss,
        "font identity invalidates a hit"
    );
    ++misses;
    probe = key;
    probe.style_id = 10;
    require(
        cache.find(probe, "ab", measure) == veyra::LineMeasureCacheResult::miss,
        "style identity invalidates a hit"
    );
    ++misses;
    probe = key;
    probe.font_scale_x = 1024;
    require(
        cache.find(probe, "ab", measure) == veyra::LineMeasureCacheResult::miss,
        "horizontal scale invalidates a hit"
    );
    ++misses;
    probe = key;
    probe.font_scale_y = 1024;
    require(
        cache.find(probe, "ab", measure) == veyra::LineMeasureCacheResult::miss,
        "vertical scale invalidates a hit"
    );
    ++misses;
    probe = key;
    probe.wrap_width = 128.0F;
    require(
        cache.find(probe, "ab", measure) == veyra::LineMeasureCacheResult::miss,
        "wrap width invalidates a hit"
    );
    ++misses;
    probe = key;
    probe.style_generation = 8;
    require(
        cache.find(probe, "ab", measure) == veyra::LineMeasureCacheResult::miss,
        "style generation invalidates a hit"
    );
    ++misses;
    require(
        cache.find(key, "ac", measure) == veyra::LineMeasureCacheResult::miss,
        "text changes invalidate a hit"
    );
    ++misses;
    require(misses == 7, "every key component flips hit to miss on its own");
    require(measure.width == 88.0F && measure.line_count == 1, "misses never serve a stale replay");
    require(
        cache.find(key, "ab", measure) == veyra::LineMeasureCacheResult::hit,
        "the original entry survives its neighbors' misses"
    );

    require(
        cache.insert(key, "ab", stored) == veyra::LineMeasureCacheResult::stored,
        "an exact duplicate stores cleanly"
    );
    require(
        cache.find(key, "ab", measure) == veyra::LineMeasureCacheResult::hit &&
            measure.width == 88.0F,
        "a duplicated entry still replays one result"
    );

    cache.clear();
    require(
        cache.find(key, "ab", measure) == veyra::LineMeasureCacheResult::miss,
        "clear invalidates every entry"
    );
}

void check_exact_width_match() {
    std::array<veyra::LineMeasureCacheSlot, 4> slots{};
    std::array<char, 256> text_storage{};
    veyra::LineMeasureCache cache(slots, text_storage, 64);
    const auto key = base_key();
    const auto stored = veyra::LineMeasure{.width = 88.0F, .height = 16.0F, .line_count = 1};
    auto measure = veyra::LineMeasure{};

    require(
        cache.insert(key, "ab", stored) == veyra::LineMeasureCacheResult::stored,
        "measured block enters the cache"
    );
    require(
        cache.find(key, "ab", measure) == veyra::LineMeasureCacheResult::hit,
        "the same constraint float hits"
    );

    auto probe = key;
    probe.wrap_width = std::nextafter(kWrap256, std::numeric_limits<float>::max());
    require(
        cache.find(probe, "ab", measure) == veyra::LineMeasureCacheResult::miss,
        "a near-identical width still misses"
    );
    probe = key;
    probe.wrap_width = std::nextafter(kWrap256, 0.0F);
    require(
        cache.find(probe, "ab", measure) == veyra::LineMeasureCacheResult::miss,
        "rounding the constraint down misses as well"
    );
    require(
        cache.find(key, "ab", measure) == veyra::LineMeasureCacheResult::hit,
        "the exact constraint still hits after near misses"
    );
}

void check_overflow_and_invalid_inputs() {
    std::array<veyra::LineMeasureCacheSlot, 1> slots{};
    std::array<char, 2> short_text_storage{};
    veyra::LineMeasureCache invalid_cache(slots, short_text_storage, 4);
    require(!invalid_cache.valid(), "undersized workspace is rejected");
    auto measure = veyra::LineMeasure{};
    require(
        invalid_cache.find(base_key(), "ab", measure) ==
            veyra::LineMeasureCacheResult::invalid_workspace,
        "an invalid cache reports its state on find"
    );
    require(
        invalid_cache.insert(base_key(), "ab", measure) ==
            veyra::LineMeasureCacheResult::invalid_workspace,
        "an invalid cache reports its state on insert"
    );

    std::array<char, 4> text_storage{};
    veyra::LineMeasureCache cache(slots, text_storage, 4);
    const auto key = base_key();
    const auto stored = veyra::LineMeasure{.width = 16.0F, .height = 16.0F, .line_count = 1};
    require(
        cache.insert(key, "ab", stored) == veyra::LineMeasureCacheResult::stored,
        "a small entry fits its slot"
    );

    require(
        cache.insert(key, "toolong", stored) == veyra::LineMeasureCacheResult::entry_too_large,
        "oversized text is not cached"
    );
    require(
        cache.find(key, "toolong", measure) == veyra::LineMeasureCacheResult::miss,
        "oversized text never hits"
    );
    require(
        cache.find(key, "ab", measure) == veyra::LineMeasureCacheResult::hit &&
            measure.width == 16.0F,
        "an oversized insert leaves existing entries intact"
    );

    const auto quiet_nan = std::numeric_limits<float>::quiet_NaN();
    const auto infinity = std::numeric_limits<float>::infinity();
    auto bad_key = key;
    bad_key.wrap_width = 0.0F;
    require(
        cache.find(bad_key, "ab", measure) == veyra::LineMeasureCacheResult::invalid_key,
        "a zero width is rejected on find"
    );
    require(
        cache.insert(bad_key, "ab", stored) == veyra::LineMeasureCacheResult::invalid_key,
        "a zero width is rejected on insert"
    );
    bad_key = key;
    bad_key.wrap_width = -64.0F;
    require(
        cache.insert(bad_key, "ab", stored) == veyra::LineMeasureCacheResult::invalid_key,
        "a negative width is rejected on insert"
    );
    bad_key = key;
    bad_key.wrap_width = quiet_nan;
    require(
        cache.insert(bad_key, "ab", stored) == veyra::LineMeasureCacheResult::invalid_key,
        "a NaN width is rejected on insert"
    );
    bad_key = key;
    bad_key.wrap_width = infinity;
    require(
        cache.insert(bad_key, "ab", stored) == veyra::LineMeasureCacheResult::invalid_key,
        "an infinite width is rejected on insert"
    );
    require(
        cache.find(key, "ab", measure) == veyra::LineMeasureCacheResult::hit,
        "rejected keys leave existing entries intact"
    );

    auto bad_measure = stored;
    bad_measure.height = -1.0F;
    require(
        cache.insert(key, "cd", bad_measure) == veyra::LineMeasureCacheResult::invalid_measure,
        "a negative height is rejected"
    );
    bad_measure = stored;
    bad_measure.width = quiet_nan;
    require(
        cache.insert(key, "cd", bad_measure) == veyra::LineMeasureCacheResult::invalid_measure,
        "a NaN extent is rejected"
    );
    require(
        cache.find(key, "cd", measure) == veyra::LineMeasureCacheResult::miss,
        "a rejected measure never hits"
    );

    require(
        cache.insert(key, "", {}) == veyra::LineMeasureCacheResult::stored,
        "empty runs can be cached"
    );
    require(
        cache.find(key, "", measure) == veyra::LineMeasureCacheResult::hit,
        "empty text finds its cached measure"
    );
    require(
        measure.width == 0.0F && measure.height == 0.0F && measure.line_count == 0,
        "empty runs replay zero extents"
    );
}

void check_generation_invalidation() {
    std::array<veyra::LineMeasureCacheSlot, 4> slots{};
    std::array<char, 256> text_storage{};
    veyra::LineMeasureCache cache(slots, text_storage, 64);
    const auto key = base_key();
    const auto stored = veyra::LineMeasure{.width = 88.0F, .height = 16.0F, .line_count = 1};
    auto measure = veyra::LineMeasure{};

    require(
        cache.insert(key, "ab", stored) == veyra::LineMeasureCacheResult::stored,
        "measured block enters under its generation"
    );
    auto next_key = key;
    next_key.style_generation = 8;
    require(
        cache.find(next_key, "ab", measure) == veyra::LineMeasureCacheResult::miss,
        "a bumped generation misses instead of replaying stale extents"
    );
    const auto remeasured = veyra::LineMeasure{.width = 96.0F, .height = 16.0F, .line_count = 1};
    require(
        cache.insert(next_key, "ab", remeasured) == veyra::LineMeasureCacheResult::stored,
        "the new generation stores its own measure"
    );
    require(
        cache.find(next_key, "ab", measure) == veyra::LineMeasureCacheResult::hit &&
            measure.width == 96.0F,
        "the new generation replays its own extents"
    );

    cache.clear();
    require(
        cache.find(key, "ab", measure) == veyra::LineMeasureCacheResult::miss,
        "clear drops the old generation"
    );
    require(
        cache.find(next_key, "ab", measure) == veyra::LineMeasureCacheResult::miss,
        "clear drops the new generation too"
    );
}

void check_layout_measure_carry() {
    std::array<veyra::LineMeasureCacheSlot, 8> slots{};
    std::array<char, 512> text_storage{};
    veyra::LineMeasureCache cache(slots, text_storage, 64);
    const auto key = base_key();
    auto reshape_work = std::size_t{0};
    auto measure = veyra::LineMeasure{};

    require(
        layout_measure(cache, key, "hello world", reshape_work, measure) ==
            veyra::LineMeasureCacheResult::miss,
        "first measure of a frame misses"
    );
    require(reshape_work == 1, "first measure does the shaping work");
    require(
        measure.width == 88.0F && measure.height == 16.0F && measure.line_count == 1,
        "one short line measures its advances"
    );

    for (auto frame = 0; frame < 3; ++frame) {
        require(
            layout_measure(cache, key, "hello world", reshape_work, measure) ==
                veyra::LineMeasureCacheResult::hit,
            "repeat frames replay the carried measure"
        );
    }
    require(reshape_work == 1, "repeat frames skip reshaping entirely");
    require(
        measure.width == 88.0F && measure.height == 16.0F && measure.line_count == 1,
        "carried frames replay identical extents"
    );

    auto narrow_key = key;
    narrow_key.wrap_width = 64.0F;
    require(
        layout_measure(cache, narrow_key, "hello world", reshape_work, measure) ==
            veyra::LineMeasureCacheResult::miss,
        "a changed width misses"
    );
    require(reshape_work == 2, "a changed width remeasures");
    require(
        measure.width == 64.0F && measure.height == 32.0F && measure.line_count == 2,
        "the narrow width wraps to two lines"
    );
    require(
        layout_measure(cache, narrow_key, "hello world", reshape_work, measure) ==
            veyra::LineMeasureCacheResult::hit,
        "the narrow width carries on its next frame"
    );
    require(reshape_work == 2, "the narrow repeat skips reshaping");

    require(
        layout_measure(cache, key, "hello world", reshape_work, measure) ==
            veyra::LineMeasureCacheResult::hit,
        "the wide entry survives beside the narrow one"
    );
    require(reshape_work == 2, "switching widths back skips reshaping");

    require(
        layout_measure(cache, key, "hello world!", reshape_work, measure) ==
            veyra::LineMeasureCacheResult::miss,
        "changed text at the same width misses"
    );
    require(reshape_work == 3, "changed text remeasures");
    require(
        measure.width == 96.0F && measure.height == 16.0F && measure.line_count == 1,
        "the edited text measures its own extents"
    );
}

void check_session_adopts_when_neither_text_nor_constraints_changed() {
    auto session = veyra::LineMeasureSession{};
    auto frame = veyra::LineMeasureFrame{};

    require(!session.has_frame(), "a fresh session holds no committed frame");
    require(!session.last_frame(frame), "a fresh session replays nothing");
    require(session.adopted_frames() == 0, "a fresh session adopted nothing");
    require(session.remeasured_frames() == 0, "a fresh session remeasured nothing");

    require(!session.begin_frame(3, 11), "the first frame has nothing to adopt");
    require(session.remeasured_frames() == 1, "the first frame counts as remeasured");
    require(!session.has_frame(), "an uncommitted frame stays unobservable");

    session.end_frame(4, 64.0F);

    require(session.has_frame(), "end_frame commits the measured frame");
    require(session.last_frame(frame), "the committed frame replays its logged extents");
    require(
        frame.text_generation == 3 && frame.constraint_generation == 11 && frame.line_count == 4 &&
            frame.total_extent == 64.0F,
        "the committed frame logs both generations with its extents"
    );

    require(session.begin_frame(3, 11), "equal text and constraint generations adopt");
    require(session.adopted_frames() == 1, "the adopted frame is counted");
    require(session.remeasured_frames() == 1, "adopting skips the measure pass");
    require(
        session.last_frame(frame) && frame.line_count == 4 && frame.total_extent == 64.0F,
        "adopt replays the logged extents instead of remeasuring"
    );
    require(session.begin_frame(3, 11), "an adopt needs no end_frame before the next adopt");
    require(session.adopted_frames() == 2, "back-to-back adopts each count");
}

void check_session_any_text_change_bumps_text_generation_and_remeasures() {
    auto session = veyra::LineMeasureSession{};

    require(!session.begin_frame(1, 1), "a cold session remeasures");
    session.end_frame(2, 32.0F);
    require(session.begin_frame(1, 1), "unchanged generations adopt");

    require(!session.begin_frame(2, 1), "any text change bumps text_generation and remeasures");
    require(session.adopted_frames() == 1, "the text edit ends the adoption streak");
    require(session.remeasured_frames() == 2, "the text edit counts as remeasured");

    session.end_frame(3, 48.0F);

    auto frame = veyra::LineMeasureFrame{};
    require(
        session.last_frame(frame) && frame.text_generation == 2 && frame.line_count == 3,
        "end_frame overwrites the committed frame with the new generation"
    );
    require(session.begin_frame(2, 1), "the recommitted text generation adopts again");
}

void check_session_any_constraint_change_bumps_constraint_generation_and_remeasures() {
    auto session = veyra::LineMeasureSession{};

    require(!session.begin_frame(4, 7), "a cold session remeasures");
    session.end_frame(2, 32.0F);
    require(session.begin_frame(4, 7), "unchanged generations adopt");

    require(
        !session.begin_frame(4, 8),
        "any font, style, wrap, or direction change bumps constraint_generation and remeasures"
    );
    require(session.remeasured_frames() == 2, "the constraint edit counts as remeasured");

    session.end_frame(3, 48.0F);

    auto frame = veyra::LineMeasureFrame{};
    require(
        session.last_frame(frame) && frame.constraint_generation == 8,
        "end_frame logs the new constraint generation"
    );
    require(session.begin_frame(4, 8), "the recommitted constraint generation adopts again");
}

void check_session_aborted_frame_never_adopts() {
    auto session = veyra::LineMeasureSession{};

    require(!session.begin_frame(5, 9), "a cold session remeasures");
    session.end_frame(2, 32.0F);
    require(session.begin_frame(5, 9), "steady state adopts");

    require(!session.begin_frame(6, 9), "a moved text generation opens a remeasure");
    require(
        !session.begin_frame(6, 9),
        "an uncommitted frame never adopts even when its generations match"
    );
    require(session.adopted_frames() == 1, "the aborted frame is never counted as adopted");
    require(session.remeasured_frames() == 3, "every uncommitted begin counts as remeasured");

    session.end_frame(3, 48.0F);
    require(session.begin_frame(6, 9), "committing the frame restores adoption");
}

void check_session_generations_compare_by_equality_across_u64_wraparound() {
    auto session = veyra::LineMeasureSession{};
    constexpr auto max = std::numeric_limits<std::uint64_t>::max();

    require(!session.begin_frame(max, max), "the max generation remeasures cold");
    session.end_frame(1, 16.0F);
    require(session.begin_frame(max, max), "the max generation adopts by equality");

    require(
        !session.begin_frame(0, max),
        "text_generation wrapping max to zero remeasures instead of adopting"
    );
    session.end_frame(1, 16.0F);
    require(session.begin_frame(0, max), "the wrapped text_generation adopts once committed");

    require(
        !session.begin_frame(0, 0),
        "constraint_generation wrapping max to zero remeasures instead of adopting"
    );
    session.end_frame(1, 16.0F);
    require(session.begin_frame(0, 0), "the wrapped constraint_generation adopts once committed");
    require(session.adopted_frames() == 3, "each exact generation match adopts once");
    require(session.remeasured_frames() == 3, "each wrap transition remeasures once");
}

void check_session_invalidate_drops_the_logged_frame() {
    auto session = veyra::LineMeasureSession{};
    auto frame = veyra::LineMeasureFrame{};

    require(!session.begin_frame(1, 1), "a cold session remeasures");
    session.end_frame(2, 32.0F);
    require(session.begin_frame(1, 1), "steady state adopts");

    session.invalidate();

    require(!session.has_frame(), "invalidate drops the recorded frame");
    require(!session.last_frame(frame), "invalidate drops the replayable extents");
    require(!session.begin_frame(1, 1), "the next begin remeasures after invalidate");
    require(session.adopted_frames() == 1, "invalidate keeps the adoption count");
    require(session.remeasured_frames() == 2, "the post-invalidate begin counts as remeasured");
}

void check_session_stray_end_frame_commits_nothing() {
    auto session = veyra::LineMeasureSession{};
    auto frame = veyra::LineMeasureFrame{};

    session.end_frame(7, 112.0F);

    require(!session.has_frame(), "end_frame without begin_frame commits nothing");
    require(!session.begin_frame(1, 1), "a stray end leaves the session cold");

    session.end_frame(2, 32.0F);
    require(session.begin_frame(1, 1), "an adopt opens nothing to close");
    session.end_frame(9, 144.0F);

    require(
        session.last_frame(frame) && frame.line_count == 2 && frame.total_extent == 32.0F,
        "a stray end_frame leaves the committed frame intact"
    );
}

}

int main() {
    try {
        check_lookup_and_key_isolation();
        check_exact_width_match();
        check_overflow_and_invalid_inputs();
        check_generation_invalidation();
        check_layout_measure_carry();
        check_session_adopts_when_neither_text_nor_constraints_changed();
        check_session_any_text_change_bumps_text_generation_and_remeasures();
        check_session_any_constraint_change_bumps_constraint_generation_and_remeasures();
        check_session_aborted_frame_never_adopts();
        check_session_generations_compare_by_equality_across_u64_wraparound();
        check_session_invalidate_drops_the_logged_frame();
        check_session_stray_end_frame_commits_nothing();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
