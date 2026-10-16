#include "display_list_equivalence.hpp"
#include "scroll_surface_helpers.hpp"

#include "veyra/inspector/trace_recorder.hpp"
#include "veyra/render/scroll_shift.hpp"
#include "veyra/render/software_renderer.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>

namespace {

void require(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

void require_recorded(veyra::TraceRecordResult result, std::string_view message) {
    require(result == veyra::TraceRecordResult::recorded, message);
}

constexpr auto kWidth = std::size_t{10};
constexpr auto kHeight = std::size_t{8};
constexpr auto kRows = std::size_t{8};

veyra::Srgba8 row_color(int content_y) {
    const auto red = static_cast<std::uint8_t>((content_y * 31 + 11) & 0xFF);
    const auto green = static_cast<std::uint8_t>((content_y * 17 + 7) & 0xFF);
    const auto blue = static_cast<std::uint8_t>((content_y * 5 + 3) & 0xFF);

    return {.red = red, .green = green, .blue = blue, .alpha = 255};
}

std::array<veyra::DisplayCommand, kRows> build_rows(int offset_y) {
    auto commands = std::array<veyra::DisplayCommand, kRows>{};

    for (std::size_t row = 0; row < kRows; ++row) {
        commands[row] = veyra::FillRect{
            .bounds =
                {
                    .y = static_cast<float>(row),
                    .width = static_cast<float>(kWidth),
                    .height = 1.0F,
                },
            .color = row_color(static_cast<int>(row) + offset_y),
        };
    }

    return commands;
}

bool overlaps_strip(veyra::LayoutRect bounds, const veyra::ScrollShiftRect& strip) {
    const auto left = static_cast<float>(strip.left);
    const auto top = static_cast<float>(strip.top);
    const auto width = static_cast<float>(strip.width);
    const auto height = static_cast<float>(strip.height);

    return bounds.x < left + width && left < bounds.x + bounds.width && bounds.y < top + height &&
        top < bounds.y + bounds.height;
}

std::size_t collect_strip_commands(
    std::span<const veyra::DisplayCommand> full,
    const veyra::ScrollShiftPlan& plan,
    std::span<veyra::DisplayCommand> output
) {
    auto count = std::size_t{0};

    for (const auto& command : full) {
        const auto* fill = std::get_if<veyra::FillRect>(&command);

        if (fill == nullptr) {
            continue;
        }

        for (std::size_t index = 0; index < plan.strip_count; ++index) {
            if (!overlaps_strip(fill->bounds, plan.strips[index])) {
                continue;
            }

            output[count] = command;
            ++count;
            break;
        }
    }

    return count;
}

void render_full(
    std::span<const veyra::DisplayCommand> commands,
    std::span<veyra::SoftwarePixel> pixels
) {
    const auto surface = veyra::SoftwareSurface{
        .width = kWidth,
        .height = kHeight,
        .stride = kWidth,
        .pixels = pixels,
    };
    const auto result = veyra::render_software(commands, surface, {});

    require(result == veyra::SoftwareRenderResult::complete, "full scene renders");
}

void render_strips(
    std::span<const veyra::DisplayCommand> commands,
    std::span<const veyra::LayoutRect> regions,
    std::span<veyra::SoftwarePixel> pixels
) {
    const auto surface = veyra::SoftwareSurface{
        .width = kWidth,
        .height = kHeight,
        .stride = kWidth,
        .pixels = pixels,
    };
    const auto result = veyra::render_software(commands, surface, {}, regions);

    require(result == veyra::SoftwareRenderResult::complete, "strip scene renders");
}

void record_row_caches(
    veyra::TraceRecorder& recorder,
    std::uint64_t first_time,
    std::size_t retained_from
) {
    for (std::uint8_t row = 0; row < kRows; ++row) {
        const auto hit = static_cast<std::size_t>(row) >= retained_from;
        const auto result = recorder.record_cache(first_time + row, row, hit);

        require(result == veyra::TraceRecordResult::recorded, "row cache records");
    }
}

void record_full_frame(veyra::TraceRecorder& recorder) {
    require_recorded(recorder.record_frame_begin(100, 1), "full frame begins");
    require_recorded(recorder.record_layout(101, 8, 1), "full frame lays out every row");
    require_recorded(recorder.record_damage(102, 1, true), "full frame damages the surface");
    require_recorded(
        recorder.record_invalidation(103, veyra::TraceInvalidateReason::content, veyra::NodeId{}),
        "full frame invalidates on content"
    );

    record_row_caches(recorder, 104, 8);

    require_recorded(recorder.record_frame_end(112, 1), "full frame ends");
}

void record_scroll_frame(
    veyra::TraceRecorder& recorder,
    std::size_t strip_regions,
    std::size_t strip_rows
) {
    require_recorded(recorder.record_frame_begin(200, 2), "scroll frame begins");
    require_recorded(recorder.record_layout(201, 8, 2), "scroll frame lays out every row");
    require_recorded(
        recorder.record_damage(202, static_cast<std::uint32_t>(strip_regions), false),
        "scroll frame damages the strips"
    );
    require_recorded(
        recorder
            .record_invalidation(203, veyra::TraceInvalidateReason::state_change, veyra::NodeId{}),
        "scroll frame invalidates on offset"
    );

    record_row_caches(recorder, 204, strip_rows);

    require_recorded(recorder.record_frame_end(212, 2), "scroll frame ends");
}

std::size_t cache_hits(const veyra::TraceRecorder& recorder) {
    auto hits = std::size_t{0};

    for (const auto& event : recorder.events()) {
        if (event.kind == veyra::TraceEventKind::cache && event.payload.cache.hit) {
            ++hits;
        }
    }

    return hits;
}

std::size_t cache_misses(const veyra::TraceRecorder& recorder) {
    auto misses = std::size_t{0};

    for (const auto& event : recorder.events()) {
        if (event.kind == veyra::TraceEventKind::cache && !event.payload.cache.hit) {
            ++misses;
        }
    }

    return misses;
}

veyra::TraceDamagePayload damage_of(const veyra::TraceRecorder& recorder) {
    for (const auto& event : recorder.events()) {
        if (event.kind == veyra::TraceEventKind::damage) {
            return event.payload.damage;
        }
    }

    return {};
}

void check_identical_lists_match() {
    const auto full = build_rows(0);
    const auto copy = full;

    auto first = std::size_t{0};

    require(veyra::equivalent_display_lists(full, copy, first), "identical lists match");
    require(first == full.size(), "equal lists report past the end");

    const auto empty = std::array<veyra::DisplayCommand, 0>{};

    require(veyra::equivalent_display_lists(empty, empty), "empty lists match");
}

void check_prefix_reports_length() {
    const auto full = build_rows(0);
    const auto prefix = std::span<const veyra::DisplayCommand>(full.data(), 2);
    const auto empty = std::array<veyra::DisplayCommand, 0>{};

    auto first = std::size_t{0};

    require(!veyra::equivalent_display_lists(full, prefix, first), "shorter list differs");
    require(first == prefix.size(), "prefix difference reports the shorter length");
    require(!veyra::equivalent_display_lists(empty, full, first), "empty prefix differs");
    require(first == 0, "empty prefix reports index zero");
}

void check_mutation_reports_index() {
    const auto left = build_rows(0);

    auto right = left;
    std::get<veyra::FillRect>(right[5]).color = veyra::Srgba8{.red = 1, .green = 2, .blue = 3};

    auto first = std::size_t{0};

    require(!veyra::equivalent_display_lists(left, right, first), "color mutation differs");
    require(first == 5, "color mutation reports its index");
}

void check_variant_swap_reports_index() {
    const auto left = build_rows(0);

    auto right = left;
    right[3] = veyra::PushClip{.bounds = {.x = 1.0F, .width = 1.0F, .height = 1.0F}, .clip_id = 0};

    auto first = std::size_t{0};

    require(!veyra::equivalent_display_lists(left, right, first), "variant swap differs");
    require(first == 3, "variant swap reports its index");
}

void check_floats_compare_bitwise() {
    const auto positive = std::array<veyra::DisplayCommand, 1>{
        veyra::FillRect{.bounds = {.width = 2.0F, .height = 1.0F}},
    };
    const auto negative = std::array<veyra::DisplayCommand, 1>{
        veyra::FillRect{.bounds = {.x = -0.0F, .width = 2.0F, .height = 1.0F}},
    };

    auto first = std::size_t{0};

    require(
        !veyra::equivalent_display_lists(positive, negative, first),
        "negative zero differs from zero"
    );
    require(first == 0, "sign difference reports index zero");
}

void check_resources_compare_by_identity() {
    const auto coverage = std::array<std::uint8_t, 2>{128, 255};
    const auto first_mask = veyra::SoftwareMask{
        .width = 2,
        .height = 1,
        .stride = 2,
        .coverage = coverage,
        .generation = 0,
    };
    const auto second_mask = veyra::SoftwareMask{
        .width = 2,
        .height = 1,
        .stride = 2,
        .coverage = coverage,
        .generation = 0,
    };
    const auto left = std::array<veyra::DisplayCommand, 1>{
        veyra::DrawMask{.x = 1, .y = 1, .mask = &first_mask},
    };
    const auto middle = std::array<veyra::DisplayCommand, 1>{
        veyra::DrawMask{.x = 1, .y = 1, .mask = &first_mask},
    };
    const auto right = std::array<veyra::DisplayCommand, 1>{
        veyra::DrawMask{.x = 1, .y = 1, .mask = &second_mask},
    };

    auto first = std::size_t{0};

    require(veyra::equivalent_display_lists(left, middle), "same resource pointer matches");
    require(!veyra::equivalent_display_lists(left, right, first), "distinct resources differ");
    require(first == 0, "resource difference reports index zero");
}

void check_clip_pairs_compare_structurally() {
    const auto left = std::array<veyra::DisplayCommand, 3>{
        veyra::PushClip{
            .bounds = {.x = 1.0F, .y = 1.0F, .width = 2.0F, .height = 1.0F},
            .clip_id = 0
        },
        veyra::FillRect{.bounds = {.width = 4.0F, .height = 3.0F}},
        veyra::PopClip{},
    };

    auto same = left;

    auto popped = left;
    popped[2] = veyra::PopTranslation{};

    auto first = std::size_t{0};

    require(veyra::equivalent_display_lists(left, same), "matching clip pairs match");
    require(!veyra::equivalent_display_lists(left, popped, first), "pop kind differs");
    require(first == 2, "pop difference reports index two");
}

void check_clip_id_distinguishes_scopes() {
    const auto left = std::array<veyra::DisplayCommand, 3>{
        veyra::PushClip{
            .bounds = {.x = 1.0F, .y = 1.0F, .width = 2.0F, .height = 1.0F},
            .clip_id = 3
        },
        veyra::FillRect{.bounds = {.width = 4.0F, .height = 3.0F}},
        veyra::PopClip{},
    };

    auto renamed = left;
    std::get<veyra::PushClip>(renamed[0]).clip_id = 4;

    auto first = std::size_t{0};

    require(veyra::equivalent_display_lists(left, left), "matching clip ids match");
    require(!veyra::equivalent_display_lists(left, renamed, first), "clip id differs");
    require(first == 0, "clip id difference reports index zero");
}

void check_spans_compare_elementwise() {
    const auto points = std::array<veyra::LayoutPoint, 2>{
        veyra::LayoutPoint{.x = 0.0F, .y = 0.0F},
        veyra::LayoutPoint{.x = 2.0F, .y = 1.0F},
    };
    const auto moved = std::array<veyra::LayoutPoint, 2>{
        veyra::LayoutPoint{.x = 0.0F, .y = 0.0F},
        veyra::LayoutPoint{.x = 2.0F, .y = 2.0F},
    };
    const auto short_points = std::array<veyra::LayoutPoint, 1>{
        veyra::LayoutPoint{.x = 0.0F, .y = 0.0F},
    };
    const auto left = std::array<veyra::DisplayCommand, 1>{
        veyra::StrokePolyline{.points = points, .thickness = 1.0F},
    };
    const auto right = std::array<veyra::DisplayCommand, 1>{
        veyra::StrokePolyline{.points = moved, .thickness = 1.0F},
    };
    const auto narrow = std::array<veyra::DisplayCommand, 1>{
        veyra::StrokePolyline{.points = short_points, .thickness = 1.0F},
    };

    auto first = std::size_t{0};

    require(!veyra::equivalent_display_lists(left, right, first), "moved point differs");
    require(first == 0, "point difference reports index zero");
    require(!veyra::equivalent_display_lists(left, narrow), "point count differs");

    const auto straight = std::array<veyra::SoftwarePathSegment, 1>{
        veyra::SoftwareLineTo{.end = {.x = 1.0F, .y = 1.0F}},
    };
    const auto curved = std::array<veyra::SoftwarePathSegment, 1>{
        veyra::SoftwareQuadraticTo{
            .control = {.x = 0.0F, .y = 1.0F},
            .end = {.x = 1.0F, .y = 1.0F},
        },
    };
    const auto plain_contour = std::array<veyra::SoftwarePathContour, 1>{
        veyra::SoftwarePathContour{.segments = straight},
    };
    const auto curved_contour = std::array<veyra::SoftwarePathContour, 1>{
        veyra::SoftwarePathContour{.segments = curved},
    };
    const auto plain = std::array<veyra::DisplayCommand, 1>{
        veyra::FillPath{.contours = plain_contour},
    };
    const auto bent = std::array<veyra::DisplayCommand, 1>{
        veyra::FillPath{.contours = curved_contour},
    };

    require(!veyra::equivalent_display_lists(plain, bent, first), "segment kind differs");
    require(first == 0, "segment difference reports index zero");
    require(veyra::equivalent_display_lists(plain, plain), "identical paths match");
}

void check_partial_scroll_uses_pixels_not_commands() {
    constexpr auto delta_y = 2;

    const auto old_rows = build_rows(0);
    const auto new_rows = build_rows(-delta_y);

    auto plan = veyra::ScrollShiftPlan{};
    const auto shift_result = veyra::resolve_scroll_shift(
        {.width = kWidth, .height = kHeight, .delta_x = 0, .delta_y = delta_y},
        plan
    );

    require(shift_result == veyra::ScrollShiftResult::complete, "partial scroll resolves");
    require(plan.has_blit, "partial scroll keeps a blit");
    require(plan.strip_count == 1, "vertical scroll exposes one strip");

    auto strip_storage = std::array<veyra::DisplayCommand, kRows>{};
    const auto strip_size = collect_strip_commands(new_rows, plan, strip_storage);
    const auto strips = std::span<const veyra::DisplayCommand>(strip_storage.data(), strip_size);

    require(strip_size == 2, "top strip repaints two rows");
    require(strip_size < new_rows.size(), "strip repaint issues fewer paint commands");

    auto first = std::size_t{0};

    require(
        !veyra::equivalent_display_lists(new_rows, strips, first),
        "blit path skips commands by design"
    );
    require(first == strip_size, "strip list matches the full list up to its length");

    auto reference = std::array<veyra::SoftwarePixel, kWidth * kHeight>{};
    auto shifted = std::array<veyra::SoftwarePixel, kWidth * kHeight>{};

    render_full(old_rows, shifted);

    const auto moved = veyra::shift_surface_pixels(
        shifted,
        kWidth,
        kWidth,
        kHeight,
        plan.source,
        plan.destination
    );

    require(moved, "partial scroll shifts surviving pixels");

    auto region_storage = std::array<veyra::LayoutRect, 2>{};

    for (std::size_t index = 0; index < plan.strip_count; ++index) {
        region_storage[index] = veyra::LayoutRect{
            .x = static_cast<float>(plan.strips[index].left),
            .y = static_cast<float>(plan.strips[index].top),
            .width = static_cast<float>(plan.strips[index].width),
            .height = static_cast<float>(plan.strips[index].height),
        };
    }

    const auto regions =
        std::span<const veyra::LayoutRect>(region_storage.data(), plan.strip_count);

    render_strips(strips, regions, shifted);
    render_full(new_rows, reference);

    require(buffers_equal(shifted, reference), "blit plus strip repaint matches a full redraw");

    auto trace_storage = std::array<veyra::TraceEvent, 16>{};
    auto recorder = veyra::TraceRecorder(trace_storage);
    recorder.set_enabled(true);

    record_full_frame(recorder);

    require(recorder.size() == 13, "full frame stores thirteen events");
    require(recorder.dropped() == 0, "roomy trace drops nothing");

    const auto full_misses = cache_misses(recorder);

    require(full_misses == 8, "full frame misses every row cache");

    const auto full_damage = damage_of(recorder);

    require(
        full_damage.region_count == 1 && full_damage.full_surface,
        "full frame flags full-surface damage"
    );

    recorder.clear();

    require(recorder.size() == 0, "cleared trace stores nothing");

    record_scroll_frame(recorder, plan.strip_count, strip_size);

    const auto scroll_hits = cache_hits(recorder);
    const auto scroll_misses = cache_misses(recorder);

    require(scroll_hits == 6 && scroll_misses == 2, "repeat keeps six rows and exposes two");
    require(scroll_hits > scroll_misses, "cache hits dominate on repeat");
    require(scroll_misses < full_misses, "scroll frame misses fewer rows than the full frame");

    const auto scroll_damage = damage_of(recorder);

    require(
        scroll_damage.region_count == static_cast<std::uint32_t>(plan.strip_count),
        "scroll damage matches the strip count"
    );
    require(!scroll_damage.full_surface, "scroll damage stays partial");
    require(recorder.dropped() == 0, "bounded trace drops nothing");
}

void check_full_damage_restores_command_equality() {
    constexpr auto delta_y = 8;

    const auto new_rows = build_rows(-delta_y);

    auto plan = veyra::ScrollShiftPlan{};
    const auto shift_result = veyra::resolve_scroll_shift(
        {.width = kWidth, .height = kHeight, .delta_x = 0, .delta_y = delta_y},
        plan
    );

    require(shift_result == veyra::ScrollShiftResult::complete, "overscroll resolves");
    require(!plan.has_blit, "overscroll keeps no blit");
    require(plan.strip_count == 1, "overscroll exposes one strip");

    auto strip_storage = std::array<veyra::DisplayCommand, kRows>{};
    const auto strip_size = collect_strip_commands(new_rows, plan, strip_storage);
    const auto strips = std::span<const veyra::DisplayCommand>(strip_storage.data(), strip_size);

    require(strip_size == new_rows.size(), "overscroll repaints everything");

    auto first = std::size_t{0};

    require(
        veyra::equivalent_display_lists(new_rows, strips, first),
        "full-area strip matches the full list"
    );
    require(first == new_rows.size(), "equal lists report past the end");

    auto reference = std::array<veyra::SoftwarePixel, kWidth * kHeight>{};
    auto repainted = std::array<veyra::SoftwarePixel, kWidth * kHeight>{};
    const auto regions = std::array<veyra::LayoutRect, 1>{
        veyra::LayoutRect{
            .width = static_cast<float>(kWidth),
            .height = static_cast<float>(kHeight),
        },
    };

    render_strips(strips, regions, repainted);
    render_full(new_rows, reference);

    require(buffers_equal(repainted, reference), "full-area strip matches a full redraw");
}

}

int main() {
    try {
        check_identical_lists_match();
        check_prefix_reports_length();
        check_mutation_reports_index();
        check_variant_swap_reports_index();
        check_floats_compare_bitwise();
        check_resources_compare_by_identity();
        check_clip_pairs_compare_structurally();
        check_clip_id_distinguishes_scopes();
        check_spans_compare_elementwise();
        check_partial_scroll_uses_pixels_not_commands();
        check_full_damage_restores_command_equality();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
