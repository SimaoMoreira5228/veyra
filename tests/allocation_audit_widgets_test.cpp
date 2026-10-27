#include "allocation_counter.hpp"
#include "veyra/behaviors/navigation/command_palette.hpp"
#include "veyra/behaviors/selection/selection.hpp"
#include "veyra/behaviors/selection/tree_cursor.hpp"
#include "veyra/data/pipeline/list_pipeline.hpp"
#include "veyra/data/source/server_rows.hpp"
#include "veyra/render/damage/retained_scroll.hpp"
#include "veyra/text/rendering/line_measure_cache.hpp"
#include "veyra/widgets/data/data_grid.hpp"
#include "veyra/widgets/data/menu_list.hpp"
#include "veyra/widgets/data/menubar.hpp"
#include "veyra/widgets/data/progress_bar.hpp"
#include "veyra/widgets/data/tree_view.hpp"
#include "veyra/widgets/data/virtual_list.hpp"
#include "veyra/widgets/overlay/popover.hpp"
#include "veyra/widgets/overlay/toast_card.hpp"
#include "veyra/widgets/overlay/tooltip_card.hpp"

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

std::uint64_t mix_hash(std::uint64_t hash, std::uint64_t value) noexcept {
    hash ^= value;
    hash *= 1099511628211ULL;
    return hash;
}

constexpr auto kRetainWidth = std::size_t{16};
constexpr auto kRetainHeight = std::size_t{8};

veyra::DisplayColor retain_row_color(std::size_t row) {
    return veyra::DisplayColor{
        .red = static_cast<std::uint8_t>(10 + row * 20),
        .green = 30,
        .blue = 50,
    };
}

struct RetainFrame {
    std::array<veyra::DisplayCommand, kRetainHeight> draws{};
};

RetainFrame retain_scrolled_rows(int offset_y) {
    auto frame = RetainFrame{};
    for (auto row = std::size_t{}; row < kRetainHeight; ++row) {
        const auto source = static_cast<int>(row) - offset_y;
        const auto wrapped = (source % 8 + 8) % 8;
        frame.draws[row] = veyra::FillRect{
            .bounds =
                {
                    .y = static_cast<float>(row),
                    .width = static_cast<float>(kRetainWidth),
                    .height = 1.0F,
                },
            .color = retain_row_color(static_cast<std::size_t>(wrapped)),
        };
    }
    return frame;
}

std::uint64_t hash_pixels(std::span<const veyra::SoftwarePixel> pixels) noexcept {
    auto hash = std::uint64_t{14695981039346656037ULL};
    for (const auto& pixel : pixels) {
        const auto packed = static_cast<std::uint64_t>(pixel.red) |
            (static_cast<std::uint64_t>(pixel.green) << 8U) |
            (static_cast<std::uint64_t>(pixel.blue) << 16U) |
            (static_cast<std::uint64_t>(pixel.alpha) << 24U);
        hash = mix_hash(hash, packed);
    }
    return hash;
}

struct RetainHarness {
    std::array<veyra::SoftwarePixel, kRetainWidth * kRetainHeight> pixels{};
    std::array<veyra::SoftwarePixel, kRetainWidth * kRetainHeight> mirror{};
    std::array<veyra::SoftwareClipRect, 4> clips{};
    veyra::RetainedScrollSurface retained{};
    veyra::RetainedScrollStats stats{};
    veyra::RetainedScrollVerifyStats verify_stats{};

    veyra::SoftwareSurface surface() {
        return veyra::SoftwareSurface{
            .width = kRetainWidth,
            .height = kRetainHeight,
            .stride = kRetainWidth,
            .pixels = pixels,
        };
    }

    veyra::SoftwareSurface scratch() {
        return veyra::SoftwareSurface{
            .width = kRetainWidth,
            .height = kRetainHeight,
            .stride = kRetainWidth,
            .pixels = mirror,
        };
    }

    veyra::SoftwareRenderWorkspace workspace() {
        return veyra::SoftwareRenderWorkspace{.clips = clips};
    }
};

void check_retained_scrolled_repeat_allocates_nothing() {
    auto harness = RetainHarness{};
    const auto top = retain_scrolled_rows(0);
    const auto moved = retain_scrolled_rows(2);

    require(
        veyra::render_retained_scrolled(
            top.draws,
            harness.surface(),
            harness.workspace(),
            {.content_generation = 1},
            harness.retained,
            harness.stats
        ) == veyra::RetainedScrollResult::complete,
        "retained warm frame presents"
    );
    require(
        veyra::render_retained_scrolled(
            moved.draws,
            harness.surface(),
            harness.workspace(),
            {.delta_y = 2, .content_generation = 1},
            harness.retained,
            harness.stats
        ) == veyra::RetainedScrollResult::complete,
        "retained warm scroll presents"
    );
    require(harness.stats.reused, "retained warm scroll reuses the blit");
    const auto moved_hash = hash_pixels(harness.pixels);
    require(
        veyra::render_retained_scrolled(
            top.draws,
            harness.surface(),
            harness.workspace(),
            {.delta_y = -2, .content_generation = 1},
            harness.retained,
            harness.stats
        ) == veyra::RetainedScrollResult::complete,
        "retained warm scroll returns"
    );
    const auto top_hash = hash_pixels(harness.pixels);

    auto allocations = std::size_t{0};
    {
        auto measure = Measure{};
        for (auto repeat = 0; repeat < 100; ++repeat) {
            const auto& frame = (repeat % 2 == 0) ? moved : top;
            const auto delta = (repeat % 2 == 0) ? 2 : -2;
            const auto expected = (repeat % 2 == 0) ? moved_hash : top_hash;
            require(
                veyra::render_retained_scrolled(
                    frame.draws,
                    harness.surface(),
                    harness.workspace(),
                    {.delta_y = delta, .content_generation = 1},
                    harness.retained,
                    harness.stats
                ) == veyra::RetainedScrollResult::complete,
                "steady retained scroll presents"
            );
            require(harness.stats.reused, "steady retained scroll reuses the blit");
            require(
                hash_pixels(harness.pixels) == expected,
                "steady retained scroll keeps its pixels"
            );
        }
        allocations = measure.count();
    }
    require(allocations == 0, "steady retained scroll repeats allocate nothing");
}

void check_retained_verified_repeat_allocates_nothing() {
    auto harness = RetainHarness{};
    const auto top = retain_scrolled_rows(0);
    const auto moved = retain_scrolled_rows(2);

    require(
        veyra::render_retained_scrolled_verified(
            top.draws,
            harness.surface(),
            harness.workspace(),
            {.content_generation = 1},
            harness.retained,
            harness.scratch(),
            harness.stats,
            harness.verify_stats
        ) == veyra::RetainedVerifyResult::complete,
        "verified warm frame presents"
    );
    require(
        veyra::render_retained_scrolled_verified(
            moved.draws,
            harness.surface(),
            harness.workspace(),
            {.delta_y = 2, .content_generation = 1},
            harness.retained,
            harness.scratch(),
            harness.stats,
            harness.verify_stats
        ) == veyra::RetainedVerifyResult::complete,
        "verified warm scroll presents"
    );
    require(harness.verify_stats.verified, "verified warm scroll runs the comparison");
    const auto moved_hash = hash_pixels(harness.pixels);
    require(
        veyra::render_retained_scrolled_verified(
            top.draws,
            harness.surface(),
            harness.workspace(),
            {.delta_y = -2, .content_generation = 1},
            harness.retained,
            harness.scratch(),
            harness.stats,
            harness.verify_stats
        ) == veyra::RetainedVerifyResult::complete,
        "verified warm scroll returns"
    );
    const auto top_hash = hash_pixels(harness.pixels);

    auto allocations = std::size_t{0};
    {
        auto measure = Measure{};
        for (auto repeat = 0; repeat < 50; ++repeat) {
            const auto& frame = (repeat % 2 == 0) ? moved : top;
            const auto delta = (repeat % 2 == 0) ? 2 : -2;
            const auto expected = (repeat % 2 == 0) ? moved_hash : top_hash;
            require(
                veyra::render_retained_scrolled_verified(
                    frame.draws,
                    harness.surface(),
                    harness.workspace(),
                    {.delta_y = delta, .content_generation = 1},
                    harness.retained,
                    harness.scratch(),
                    harness.stats,
                    harness.verify_stats
                ) == veyra::RetainedVerifyResult::complete,
                "steady verified scroll presents"
            );
            require(harness.verify_stats.verified, "steady verified scroll runs the comparison");
            require(
                hash_pixels(harness.pixels) == expected,
                "steady verified scroll keeps its pixels"
            );
        }
        allocations = measure.count();
    }
    require(allocations == 0, "steady verified scroll repeats allocate nothing");
}

void check_list_pipeline_request_resolve_allocates_nothing() {
    auto pipeline = veyra::ListPipeline{};
    auto pending = veyra::ListLoadRequest{};
    require(
        pipeline.request(veyra::ListLoadDirection::append, 100, 25) ==
            veyra::ListPipelineResult::changed,
        "pipeline warm request starts a load"
    );
    require(
        pipeline.pending_request(veyra::ListLoadDirection::append, pending),
        "pipeline warm request is pending"
    );
    require(
        pipeline.resolve(veyra::ListLoadDirection::append, pending.generation, true) ==
            veyra::ListPipelineResult::changed,
        "pipeline warm resolve applies"
    );

    auto allocations = std::size_t{0};
    {
        auto measure = Measure{};
        auto checksum = std::uint64_t{14695981039346656037ULL};
        for (auto repeat = 0; repeat < 200; ++repeat) {
            require(
                pipeline.request(veyra::ListLoadDirection::append, 100, 25) ==
                    veyra::ListPipelineResult::changed,
                "steady pipeline request starts a load"
            );
            auto live = veyra::ListLoadRequest{};
            require(
                pipeline.pending_request(veyra::ListLoadDirection::append, live),
                "steady pipeline request is pending"
            );
            require(
                pipeline.resolve(veyra::ListLoadDirection::append, live.generation, true) ==
                    veyra::ListPipelineResult::changed,
                "steady pipeline resolve applies"
            );
            checksum = mix_hash(checksum, live.generation);
        }
        require(checksum != 14695981039346656037ULL, "steady pipeline cycles advance generations");
        allocations = measure.count();
    }
    require(allocations == 0, "steady pipeline request/resolve repeats allocate nothing");
}

void check_server_rows_mutation_cycle_allocates_nothing() {
    auto rows = veyra::ServerRows{};
    const auto key = rows.request_key();
    require(
        rows.note_loaded(10, 10, key) == veyra::ServerRowsResult::changed,
        "server warm load records"
    );
    require(rows.evict(10, 10) == veyra::ServerRowsResult::changed, "server warm evict clears");

    auto allocations = std::size_t{0};
    {
        auto measure = Measure{};
        auto probe_sorts = std::array<veyra::ServerSort, 2>{};
        auto probe_filters = std::array<veyra::ServerFilter, 2>{};
        auto probe_groups = std::array<std::uint64_t, 4>{};
        auto probe_ranges = std::array<veyra::ServerLoadedRange, 4>{};
        for (auto repeat = 0; repeat < 200; ++repeat) {
            require(
                rows.note_loaded(10, 10, key) == veyra::ServerRowsResult::changed,
                "steady server load records"
            );
            require(rows.is_covered(10, 10), "steady server load covers its range");
            require(rows.copy_sorts(probe_sorts) == 0, "steady server sorts copy back");
            require(rows.copy_filters(probe_filters) == 0, "steady server filters copy back");
            require(rows.copy_group_keys(probe_groups) == 0, "steady server groups copy back");
            require(rows.copy_intervals(probe_ranges) == 1, "steady server intervals copy back");
            require(rows.confirm(key) == veyra::ServerRowsResult::handled, "steady key confirms");
            require(
                rows.evict(10, 10) == veyra::ServerRowsResult::changed,
                "steady server evict clears"
            );
            require(!rows.is_covered(10, 10), "steady server evict uncovers its range");
        }
        allocations = measure.count();
    }
    require(allocations == 0, "steady server request/resolve repeats allocate nothing");
}

void check_virtual_list_emit_allocates_nothing() {
    const auto theme = veyra::Theme{};
    constexpr std::array<veyra::VirtualListRowState, 3> rows{{
        {.selected = true},
        {},
        {.current = true},
    }};
    const veyra::VirtualListRowsGeometry geometry{
        .list_bounds = {.x = 10.0F, .y = 20.0F, .width = 200.0F, .height = 120.0F},
        .row_height = 24.0F,
    };
    auto commands = std::array<veyra::DisplayCommand, 8>{};
    const auto warm = veyra::emit_virtual_list_rows(theme, rows, geometry, commands);
    require(
        warm.result == veyra::VirtualListEmitResult::complete,
        "virtual-list warm emit completes"
    );

    auto allocations = std::size_t{0};
    {
        auto measure = Measure{};
        for (auto repeat = 0; repeat < 200; ++repeat) {
            const auto output = veyra::emit_virtual_list_rows(theme, rows, geometry, commands);
            require(
                output.result == warm.result && output.placed == warm.placed &&
                    output.required == warm.required,
                "steady virtual-list emit repeats its paint"
            );
        }
        allocations = measure.count();
    }
    require(allocations == 0, "steady virtual-list emit repeats allocate nothing");
}

void check_tree_view_emit_allocates_nothing() {
    const auto theme = veyra::Theme{};
    constexpr std::array<veyra::TreeCursorRow, 3> rows{{
        {.id = 1, .parent = 0, .depth = 0, .has_children = true, .expanded = true},
        {.id = 2, .parent = 1, .depth = 1, .has_children = false},
        {.id = 3, .parent = 0, .depth = 0, .has_children = false},
    }};
    constexpr std::array<veyra::TreeViewRowState, 3> states{{
        {.selected = true},
        {},
        {.current = true},
    }};
    const veyra::TreeViewRowsGeometry geometry{
        .list_bounds = {.x = 0.0F, .y = 0.0F, .width = 200.0F, .height = 120.0F},
        .row_height = 24.0F,
        .indent_width = 16.0F,
    };
    auto commands = std::array<veyra::DisplayCommand, 24>{};
    const auto warm = veyra::emit_tree_view_rows(theme, rows, states, geometry, commands);
    require(warm.result == veyra::TreeViewEmitResult::complete, "tree-view warm emit completes");

    auto allocations = std::size_t{0};
    {
        auto measure = Measure{};
        for (auto repeat = 0; repeat < 200; ++repeat) {
            const auto output = veyra::emit_tree_view_rows(theme, rows, states, geometry, commands);
            require(
                output.result == warm.result && output.placed == warm.placed &&
                    output.required == warm.required,
                "steady tree-view emit repeats its paint"
            );
        }
        allocations = measure.count();
    }
    require(allocations == 0, "steady tree-view emit repeats allocate nothing");
}

void check_data_grid_emit_allocates_nothing() {
    const auto theme = veyra::Theme{};
    constexpr std::array<veyra::DataGridColumn, 3> columns{{
        {.id = 1, .width = 100.0F, .min_width = 40.0F, .max_width = 200.0F},
        {.id = 2, .width = 80.0F, .min_width = 40.0F, .max_width = 200.0F},
        {.id = 3, .width = 120.0F, .min_width = 40.0F, .max_width = 200.0F},
    }};
    constexpr std::array<veyra::SortSpec, 2> sorts{{
        {.column = 3, .direction = veyra::SortDirection::descending},
        {.column = 1, .direction = veyra::SortDirection::ascending},
    }};
    const veyra::DataGridHeaderGeometry geometry{.x = 8.0F, .y = 4.0F, .row_height = 32.0F};
    auto commands = std::array<veyra::DisplayCommand, 16>{};
    const auto warm = veyra::emit_data_grid_header(theme, columns, sorts, geometry, commands);
    require(!warm.overflow, "data-grid warm emit fits");

    auto allocations = std::size_t{0};
    {
        auto measure = Measure{};
        for (auto repeat = 0; repeat < 200; ++repeat) {
            const auto output =
                veyra::emit_data_grid_header(theme, columns, sorts, geometry, commands);
            require(
                output.count == warm.count && output.overflow == warm.overflow,
                "steady data-grid emit repeats its paint"
            );
        }
        allocations = measure.count();
    }
    require(allocations == 0, "steady data-grid emit repeats allocate nothing");
}

void check_menu_list_emit_allocates_nothing() {
    const auto theme = veyra::Theme{};
    constexpr std::array<veyra::MenuListRowState, 4> rows{{
        {.active = true},
        {.is_separator = true},
        {.checked = true},
        {.disabled = true},
    }};
    const veyra::MenuListGeometry geometry{
        .list_bounds = {.x = 0.0F, .y = 0.0F, .width = 160.0F, .height = 120.0F},
        .row_height = 24.0F,
        .shortcut_column_x = 120.0F,
    };
    auto commands = std::array<veyra::DisplayCommand, 16>{};
    const auto warm = veyra::emit_menu_list(theme, rows, geometry, commands);
    require(warm.result == veyra::MenuListEmitResult::complete, "menu-list warm emit completes");

    auto allocations = std::size_t{0};
    {
        auto measure = Measure{};
        for (auto repeat = 0; repeat < 200; ++repeat) {
            const auto output = veyra::emit_menu_list(theme, rows, geometry, commands);
            require(
                output.result == warm.result && output.placed == warm.placed &&
                    output.required == warm.required,
                "steady menu-list emit repeats its paint"
            );
        }
        allocations = measure.count();
    }
    require(allocations == 0, "steady menu-list emit repeats allocate nothing");
}

void check_menubar_emit_allocates_nothing() {
    const auto theme = veyra::Theme{};
    constexpr std::array<veyra::MenubarLabelState, 3> labels{{
        {.active = true},
        {},
        {.disabled = true},
    }};
    constexpr std::array<float, 3> widths{{48.0F, 64.0F, 56.0F}};
    const veyra::MenubarGeometry geometry{
        .strip_bounds = {.x = 0.0F, .y = 0.0F, .width = 240.0F, .height = 28.0F},
        .label_widths = widths,
    };
    auto commands = std::array<veyra::DisplayCommand, 16>{};
    const auto warm = veyra::emit_menubar(theme, labels, geometry, commands);
    require(warm.result == veyra::MenubarEmitResult::complete, "menubar warm emit completes");

    auto allocations = std::size_t{0};
    {
        auto measure = Measure{};
        for (auto repeat = 0; repeat < 200; ++repeat) {
            const auto output = veyra::emit_menubar(theme, labels, geometry, commands);
            require(
                output.result == warm.result && output.placed == warm.placed &&
                    output.required == warm.required,
                "steady menubar emit repeats its paint"
            );
        }
        allocations = measure.count();
    }
    require(allocations == 0, "steady menubar emit repeats allocate nothing");
}

void check_popover_emit_allocates_nothing() {
    const auto theme = veyra::Theme{};
    const veyra::PopoverGeometry geometry{
        .panel = {.x = 8.0F, .y = 8.0F, .width = 160.0F, .height = 96.0F},
        .viewport = {.x = 0.0F, .y = 0.0F, .width = 320.0F, .height = 240.0F},
    };
    auto commands = std::array<veyra::DisplayCommand, 8>{};
    const auto warm = veyra::emit_popover(theme, geometry, commands);
    require(warm.result == veyra::PopoverEmitResult::complete, "popover warm emit completes");

    auto allocations = std::size_t{0};
    {
        auto measure = Measure{};
        for (auto repeat = 0; repeat < 200; ++repeat) {
            const auto output = veyra::emit_popover(theme, geometry, commands);
            require(
                output.result == warm.result && output.placed == warm.placed &&
                    output.required == warm.required,
                "steady popover emit repeats its paint"
            );
        }
        allocations = measure.count();
    }
    require(allocations == 0, "steady popover emit repeats allocate nothing");
}

void check_toast_emit_allocates_nothing() {
    const auto theme = veyra::Theme{};
    const veyra::ToastCardGeometry geometry{
        .card = {.x = 4.0F, .y = 4.0F, .width = 200.0F, .height = 48.0F},
    };
    auto commands = std::array<veyra::DisplayCommand, 4>{};
    const auto warm =
        veyra::emit_toast_card(theme, {.kind = veyra::ToastKind::info}, geometry, commands);
    require(warm.result == veyra::ToastCardEmitResult::complete, "toast warm emit completes");

    auto allocations = std::size_t{0};
    {
        auto measure = Measure{};
        for (auto repeat = 0; repeat < 200; ++repeat) {
            const auto output =
                veyra::emit_toast_card(theme, {.kind = veyra::ToastKind::info}, geometry, commands);
            require(
                output.result == warm.result && output.placed == warm.placed &&
                    output.required == warm.required,
                "steady toast emit repeats its paint"
            );
        }
        allocations = measure.count();
    }
    require(allocations == 0, "steady toast emit repeats allocate nothing");
}

void check_tooltip_emit_allocates_nothing() {
    const auto theme = veyra::Theme{};
    const veyra::TooltipCardGeometry geometry{
        .rect = {.x = 12.0F, .y = 6.0F, .width = 120.0F, .height = 28.0F},
    };
    auto commands = std::array<veyra::DisplayCommand, 4>{};
    const auto warm = veyra::emit_tooltip_card(theme, geometry, commands);
    require(warm.result == veyra::TooltipCardEmitResult::complete, "tooltip warm emit completes");

    auto allocations = std::size_t{0};
    {
        auto measure = Measure{};
        for (auto repeat = 0; repeat < 200; ++repeat) {
            const auto output = veyra::emit_tooltip_card(theme, geometry, commands);
            require(
                output.result == warm.result && output.placed == warm.placed &&
                    output.required == warm.required,
                "steady tooltip emit repeats its paint"
            );
        }
        allocations = measure.count();
    }
    require(allocations == 0, "steady tooltip emit repeats allocate nothing");
}

void check_progress_bar_emit_allocates_nothing() {
    const auto theme = veyra::Theme{};
    const veyra::ProgressState state{.fraction = 0.5, .indeterminate = false};
    const veyra::ProgressBarGeometry geometry{
        .bar = {.x = 0.0F, .y = 0.0F, .width = 160.0F, .height = 8.0F},
        .animation_tick_ms = 400,
    };
    auto commands = std::array<veyra::DisplayCommand, 4>{};
    const auto warm = veyra::emit_progress_bar(theme, state, geometry, commands);
    require(
        warm.result == veyra::ProgressBarEmitResult::complete,
        "progress-bar warm emit completes"
    );

    auto allocations = std::size_t{0};
    {
        auto measure = Measure{};
        for (auto repeat = 0; repeat < 200; ++repeat) {
            const auto output = veyra::emit_progress_bar(theme, state, geometry, commands);
            require(
                output.result == warm.result && output.placed == warm.placed &&
                    output.required == warm.required,
                "steady progress-bar emit repeats its paint"
            );
        }
        allocations = measure.count();
    }
    require(allocations == 0, "steady progress-bar emit repeats allocate nothing");
}

void check_line_measure_session_cycles_allocate_nothing() {
    auto session = veyra::LineMeasureSession{};
    require(!session.begin_frame(3, 11), "session warm frame remeasures");
    session.end_frame(4, 64.0F);
    require(session.begin_frame(3, 11), "session warm adopt hits");

    auto allocations = std::size_t{0};
    {
        auto measure = Measure{};
        for (auto repeat = 0; repeat < 500; ++repeat) {
            require(session.begin_frame(3, 11), "steady session adopt hits");
        }
        require(session.adopted_frames() == 501, "steady adopts keep counting");
        auto generation = std::uint64_t{100};
        for (auto repeat = 0; repeat < 200; ++repeat) {
            ++generation;
            require(!session.begin_frame(generation, 11), "steady remeasure opens");
            session.end_frame(4, 64.0F);
        }
        auto frame = veyra::LineMeasureFrame{};
        require(
            session.last_frame(frame) && frame.text_generation == generation,
            "steady remeasures commit the latest generation"
        );
        allocations = measure.count();
    }
    require(allocations == 0, "steady line-measure begin/end cycles allocate nothing");
}

void check_tree_expansion_cycles_allocate_nothing() {
    auto expansion = veyra::TreeExpansion{};
    require(expansion.expand(7) == veyra::TreeCursorResult::expand, "expansion warm expand opens");
    require(
        expansion.collapse(7) == veyra::TreeCursorResult::collapse,
        "expansion warm collapse closes"
    );

    auto allocations = std::size_t{0};
    {
        auto measure = Measure{};
        for (auto repeat = 0; repeat < 300; ++repeat) {
            require(
                expansion.expand(7) == veyra::TreeCursorResult::expand,
                "steady expansion expand opens"
            );
            require(expansion.is_expanded(7), "steady expansion marks its row");
            require(
                expansion.collapse(7) == veyra::TreeCursorResult::collapse,
                "steady expansion collapse closes"
            );
            require(expansion.count() == 0, "steady expansion returns to empty");
        }
        allocations = measure.count();
    }
    require(allocations == 0, "steady tree-expansion mutation cycles allocate nothing");
}

void check_selection_mutation_cycles_allocate_nothing() {
    auto selection = veyra::SelectionController{};
    require(
        selection.configure({.mode = veyra::SelectionMode::multiple, .allow_empty = true}),
        "selection warm configure takes multiple mode"
    );
    require(
        selection.toggle_item(5) == veyra::SelectionResult::changed,
        "selection warm toggle arms"
    );

    auto allocations = std::size_t{0};
    {
        auto measure = Measure{};
        auto selected = std::array<std::uint64_t, 4>{};
        for (auto repeat = 0; repeat < 300; ++repeat) {
            require(
                selection.toggle_item(5) == veyra::SelectionResult::changed,
                "steady selection toggle flips"
            );
            require(
                selection.toggle_item(5) == veyra::SelectionResult::changed,
                "steady selection toggle flips back"
            );
            require(selection.count() == 1, "steady selection keeps one row armed");
            require(selection.copy_selected(selected) == 1, "steady selection copies back");
        }
        allocations = measure.count();
    }
    require(allocations == 0, "steady selection mutation cycles allocate nothing");
}

std::span<const char> palette_chars_of(std::string_view text) {
    return {text.data(), text.size()};
}

void check_command_palette_filter_cycles_allocate_nothing() {
    auto palette = veyra::CommandPalette{};
    const std::array<veyra::PaletteItem, 4> items{{
        {.id = 11, .group_id = 2},
        {.id = 12, .group_id = 1},
        {.id = 13, .group_id = 2},
        {.id = 14, .group_id = 1},
    }};
    const std::array<std::string_view, 4> titles{{"apple", "banana", "apricot", "cherry"}};
    const std::array<std::string_view, 4> keywords{{"", "", "", ""}};
    require(
        palette.set_items(items) == veyra::PaletteResult::changed,
        "palette warm items register"
    );
    require(
        palette.apply_filter(titles, keywords) == veyra::PaletteResult::changed,
        "palette warm filter parks"
    );

    static constexpr char kNarrow[] = "ap";
    static constexpr char kWider[] = "apr";
    auto matches = std::array<std::uint64_t, 8>{};

    const auto warm_query = palette_chars_of(kNarrow);
    require(
        palette.set_query(warm_query) == veyra::PaletteResult::changed,
        "palette warm query stores"
    );
    require(
        palette.apply_filter(titles, keywords) == veyra::PaletteResult::changed,
        "palette warm filtered query applies"
    );
    const auto warm_count = palette.copy_matches(matches);
    require(warm_count == 2, "palette warm query keeps its matches");

    auto allocations = std::size_t{0};
    {
        auto measure = Measure{};
        for (auto repeat = 0; repeat < 200; ++repeat) {
            const auto query_text = (repeat % 2 == 0) ? kWider : kNarrow;
            const auto expected = (repeat % 2 == 0) ? std::size_t{1} : std::size_t{2};
            require(
                palette.set_query(palette_chars_of(query_text)) == veyra::PaletteResult::changed,
                "steady palette query stores"
            );
            require(
                palette.apply_filter(titles, keywords) == veyra::PaletteResult::changed,
                "steady palette filter applies"
            );
            require(
                palette.copy_matches(matches) == expected,
                "steady palette filter keeps its matches"
            );
        }
        allocations = measure.count();
    }
    require(allocations == 0, "steady palette filter cycles allocate nothing");
}

void check_harness_armed_in_this_binary() {
    auto counted = std::size_t{0};
    {
        auto measure = Measure{};
        auto* probe = new int(7);
        require(probe != nullptr && *probe == 7, "scalar new works while armed");
        counted = measure.count();
        delete probe;
    }
    require(counted == 1, "scalar new counts once while armed");
}

}

int main() {
    try {
        check_harness_armed_in_this_binary();
        check_retained_scrolled_repeat_allocates_nothing();
        check_retained_verified_repeat_allocates_nothing();
        check_list_pipeline_request_resolve_allocates_nothing();
        check_server_rows_mutation_cycle_allocates_nothing();
        check_virtual_list_emit_allocates_nothing();
        check_tree_view_emit_allocates_nothing();
        check_data_grid_emit_allocates_nothing();
        check_menu_list_emit_allocates_nothing();
        check_menubar_emit_allocates_nothing();
        check_popover_emit_allocates_nothing();
        check_toast_emit_allocates_nothing();
        check_tooltip_emit_allocates_nothing();
        check_progress_bar_emit_allocates_nothing();
        check_line_measure_session_cycles_allocate_nothing();
        check_tree_expansion_cycles_allocate_nothing();
        check_selection_mutation_cycles_allocate_nothing();
        check_command_palette_filter_cycles_allocate_nothing();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
