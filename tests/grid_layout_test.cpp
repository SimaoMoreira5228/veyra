#include "veyra/layout/grid/grid.hpp"

#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

void require(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

bool near(float actual, float expected) {
    return std::abs(actual - expected) < 0.001F;
}

void check_content_sized_auto_tracks() {
    const auto container = veyra::GridContainer{
        .width = 100.0F,
        .height = 60.0F,
        .padding = {.top = 2.0F, .left = 3.0F},
        .gap = 4.0F,
    };
    const std::array columns{veyra::GridTrack{}, veyra::GridTrack{}};
    const std::array rows{veyra::GridTrack{}};
    const std::array items{
        veyra::GridItem{.row = 0, .column = 0},
        veyra::GridItem{.row = 0, .column = 1},
    };
    const std::array measurements{
        veyra::IntrinsicSize{
            .width = {.min_content = 10.0F, .preferred = 20.0F, .max_content = 30.0F},
            .height = {.min_content = 5.0F, .preferred = 10.0F, .max_content = 12.0F},
        },
        veyra::IntrinsicSize{
            .width = {.min_content = 15.0F, .preferred = 30.0F, .max_content = 40.0F},
            .height = {.min_content = 8.0F, .preferred = 15.0F, .max_content = 20.0F},
        },
    };
    std::array<veyra::GridTrackWorkspace, 2> column_workspace{};
    std::array<veyra::GridTrackWorkspace, 1> row_workspace{};
    std::array<veyra::GridItemPlacement, 2> placements{};
    std::array<std::uint64_t, 1> occupancy{};
    const auto workspace = veyra::GridLayoutWorkspace{
        .columns = column_workspace,
        .rows = row_workspace,
        .placements = placements,
        .occupancy = occupancy,
    };
    std::array<veyra::LayoutRect, 2> output{};

    const auto result =
        veyra::layout_grid(container, columns, rows, items, measurements, workspace, output);
    require(result == veyra::GridLayoutResult::complete, "auto tracks fit measured content");
    require(
        near(output[0].x, 3.0F) && near(output[0].width, 41.5F) && near(output[1].x, 48.5F) &&
            near(output[1].width, 51.5F),
        "auto columns size from content and share remaining space"
    );
    require(
        near(output[0].y, 2.0F) && near(output[0].height, 58.0F),
        "auto rows stretch to the available height"
    );
}

void check_track_limits_and_spanning() {
    const auto container = veyra::GridContainer{
        .width = 100.0F,
        .height = 80.0F,
    };
    const std::array columns{
        veyra::GridTrack{.minimum = 20.0F, .maximum = 20.0F},
        veyra::GridTrack{.minimum = 5.0F, .maximum = 60.0F},
    };
    const std::array rows{veyra::GridTrack{}, veyra::GridTrack{}};
    const std::array items{
        veyra::GridItem{.row_span = 2, .column_span = 2},
    };
    const std::array measurements{
        veyra::IntrinsicSize{
            .width = {.min_content = 50.0F, .preferred = 70.0F, .max_content = 90.0F},
            .height = {.min_content = 10.0F, .preferred = 20.0F, .max_content = 30.0F},
        },
    };
    std::array<veyra::GridTrackWorkspace, 2> column_workspace{};
    std::array<veyra::GridTrackWorkspace, 2> row_workspace{};
    std::array<veyra::GridItemPlacement, 1> placements{};
    std::array<std::uint64_t, 1> occupancy{};
    const auto workspace = veyra::GridLayoutWorkspace{
        .columns = column_workspace,
        .rows = row_workspace,
        .placements = placements,
        .occupancy = occupancy,
    };
    std::array<veyra::LayoutRect, 1> output{};

    const auto result =
        veyra::layout_grid(container, columns, rows, items, measurements, workspace, output);
    require(result == veyra::GridLayoutResult::complete, "spanning content fits track limits");
    require(
        near(output[0].width, 80.0F) && near(output[0].height, 80.0F),
        "spanning items use the sum of their resolved tracks"
    );
    require(
        near(column_workspace[0].size, 20.0F) && near(column_workspace[1].size, 60.0F),
        "fixed tracks stay fixed and minmax tracks respect their maximum"
    );
}

void check_tracks_shrink_to_minimum_content() {
    const auto container = veyra::GridContainer{
        .width = 60.0F,
        .height = 10.0F,
    };
    const std::array columns{
        veyra::GridTrack{.minimum = 0.0F, .maximum = 100.0F},
        veyra::GridTrack{.minimum = 0.0F, .maximum = 100.0F},
    };
    const std::array rows{veyra::GridTrack{.minimum = 10.0F, .maximum = 10.0F}};
    const std::array items{
        veyra::GridItem{.row = 0, .column = 0},
        veyra::GridItem{.row = 0, .column = 1},
    };
    const std::array measurements{
        veyra::IntrinsicSize{
            .width = {.min_content = 10.0F, .preferred = 40.0F, .max_content = 50.0F},
            .height = {.min_content = 5.0F, .preferred = 10.0F, .max_content = 10.0F},
        },
        veyra::IntrinsicSize{
            .width = {.min_content = 10.0F, .preferred = 40.0F, .max_content = 50.0F},
            .height = {.min_content = 5.0F, .preferred = 10.0F, .max_content = 10.0F},
        },
    };
    std::array<veyra::GridTrackWorkspace, 2> column_workspace{};
    std::array<veyra::GridTrackWorkspace, 1> row_workspace{};
    std::array<veyra::GridItemPlacement, 2> placements{};
    std::array<std::uint64_t, 1> occupancy{};
    const auto workspace = veyra::GridLayoutWorkspace{
        .columns = column_workspace,
        .rows = row_workspace,
        .placements = placements,
        .occupancy = occupancy,
    };
    std::array<veyra::LayoutRect, 2> output{};

    const auto result =
        veyra::layout_grid(container, columns, rows, items, measurements, workspace, output);
    require(result == veyra::GridLayoutResult::complete, "tracks shrink without crossing minimums");
    require(
        near(output[0].width, 30.0F) && near(output[1].width, 30.0F),
        "preferred tracks share shrinkage above min-content"
    );
}

void check_maximum_content_contributions() {
    const auto container = veyra::GridContainer{
        .width = 60.0F,
        .height = 10.0F,
    };
    const std::array columns{veyra::GridTrack{}, veyra::GridTrack{}};
    const std::array rows{veyra::GridTrack{.minimum = 10.0F, .maximum = 10.0F}};
    const std::array items{
        veyra::GridItem{.row = 0, .column = 0},
        veyra::GridItem{.row = 0, .column = 1},
    };
    const std::array measurements{
        veyra::IntrinsicSize{
            .width = {.min_content = 10.0F, .preferred = 20.0F, .max_content = 80.0F},
            .height = {.min_content = 5.0F, .preferred = 10.0F, .max_content = 10.0F},
        },
        veyra::IntrinsicSize{
            .width = {.min_content = 10.0F, .preferred = 20.0F, .max_content = 30.0F},
            .height = {.min_content = 5.0F, .preferred = 10.0F, .max_content = 10.0F},
        },
    };
    std::array<veyra::GridTrackWorkspace, 2> column_workspace{};
    std::array<veyra::GridTrackWorkspace, 1> row_workspace{};
    std::array<veyra::GridItemPlacement, 2> placements{};
    std::array<std::uint64_t, 1> occupancy{};
    const auto workspace = veyra::GridLayoutWorkspace{
        .columns = column_workspace,
        .rows = row_workspace,
        .placements = placements,
        .occupancy = occupancy,
    };
    std::array<veyra::LayoutRect, 2> output{};

    const auto result =
        veyra::layout_grid(container, columns, rows, items, measurements, workspace, output);
    require(result == veyra::GridLayoutResult::complete, "max-content tracks fit the container");
    require(
        near(output[0].width, 37.14286F) && near(output[1].width, 22.85714F),
        "max-content contributions shape track growth before the grid shrinks"
    );
}

void check_fractional_tracks() {
    auto container = veyra::GridContainer{
        .width = 90.0F,
        .height = 10.0F,
    };
    const std::array fractional_columns{
        veyra::GridTrack{.fraction = 1.0F},
        veyra::GridTrack{.fraction = 2.0F},
    };
    const std::array rows{veyra::GridTrack{.minimum = 10.0F, .maximum = 10.0F}};
    const std::span<const veyra::GridItem> items;
    const std::span<const veyra::IntrinsicSize> measurements;
    std::array<veyra::GridTrackWorkspace, 2> column_workspace{};
    std::array<veyra::GridTrackWorkspace, 1> row_workspace{};
    const auto fractional_workspace = veyra::GridLayoutWorkspace{
        .columns = column_workspace,
        .rows = row_workspace,
    };

    auto result = veyra::layout_grid(
        container,
        fractional_columns,
        rows,
        items,
        measurements,
        fractional_workspace,
        std::span<veyra::LayoutRect>{}
    );
    require(result == veyra::GridLayoutResult::complete, "fractional tracks fill free space");
    require(
        near(column_workspace[0].size, 30.0F) && near(column_workspace[1].size, 60.0F),
        "fraction weights divide remaining space proportionally"
    );

    container.width = 140.0F;
    const std::array capped_columns{
        veyra::GridTrack{.maximum = 20.0F, .fraction = 1.0F},
        veyra::GridTrack{.maximum = 100.0F, .fraction = 1.0F},
        veyra::GridTrack{},
    };
    std::array<veyra::GridTrackWorkspace, 3> capped_workspace{};
    const auto capped_grid_workspace = veyra::GridLayoutWorkspace{
        .columns = capped_workspace,
        .rows = row_workspace,
    };
    result = veyra::layout_grid(
        container,
        capped_columns,
        rows,
        items,
        measurements,
        capped_grid_workspace,
        std::span<veyra::LayoutRect>{}
    );
    require(
        result == veyra::GridLayoutResult::complete,
        "fraction limits leave space to auto tracks"
    );
    require(
        near(capped_workspace[0].size, 20.0F) && near(capped_workspace[1].size, 100.0F) &&
            near(capped_workspace[2].size, 20.0F),
        "fractional limits cap growth before auto tracks expand"
    );
}

void check_automatic_placement() {
    const auto container = veyra::GridContainer{
        .width = 30.0F,
        .height = 20.0F,
    };
    const std::array columns{
        veyra::GridTrack{.minimum = 10.0F, .maximum = 10.0F},
        veyra::GridTrack{.minimum = 10.0F, .maximum = 10.0F},
        veyra::GridTrack{.minimum = 10.0F, .maximum = 10.0F},
    };
    const std::array rows{
        veyra::GridTrack{.minimum = 10.0F, .maximum = 10.0F},
        veyra::GridTrack{.minimum = 10.0F, .maximum = 10.0F},
    };
    const std::array items{
        veyra::GridItem{},
        veyra::GridItem{.row = 0, .column = 1},
        veyra::GridItem{.row = 0},
        veyra::GridItem{.column_span = 2},
    };
    const std::array measurements{
        veyra::IntrinsicSize{
            .width = {.min_content = 10.0F, .preferred = 10.0F, .max_content = 10.0F},
            .height = {.min_content = 10.0F, .preferred = 10.0F, .max_content = 10.0F},
        },
        veyra::IntrinsicSize{
            .width = {.min_content = 10.0F, .preferred = 10.0F, .max_content = 10.0F},
            .height = {.min_content = 10.0F, .preferred = 10.0F, .max_content = 10.0F},
        },
        veyra::IntrinsicSize{
            .width = {.min_content = 10.0F, .preferred = 10.0F, .max_content = 10.0F},
            .height = {.min_content = 10.0F, .preferred = 10.0F, .max_content = 10.0F},
        },
        veyra::IntrinsicSize{
            .width = {.min_content = 20.0F, .preferred = 20.0F, .max_content = 20.0F},
            .height = {.min_content = 10.0F, .preferred = 10.0F, .max_content = 10.0F},
        },
    };
    std::array<veyra::GridTrackWorkspace, 3> column_workspace{};
    std::array<veyra::GridTrackWorkspace, 2> row_workspace{};
    std::array<veyra::GridItemPlacement, 4> placements{};
    std::array<std::uint64_t, 1> occupancy{};
    const auto workspace = veyra::GridLayoutWorkspace{
        .columns = column_workspace,
        .rows = row_workspace,
        .placements = placements,
        .occupancy = occupancy,
    };
    std::array<veyra::LayoutRect, 4> output{};

    const auto result =
        veyra::layout_grid(container, columns, rows, items, measurements, workspace, output);
    require(result == veyra::GridLayoutResult::complete, "automatic placement handles mixed items");
    require(
        near(output[0].x, 0.0F) && near(output[0].y, 0.0F) && near(output[1].x, 10.0F) &&
            near(output[1].y, 0.0F) && near(output[2].x, 20.0F) && near(output[2].y, 0.0F),
        "auto items avoid explicitly placed items"
    );
    require(
        near(output[3].x, 0.0F) && near(output[3].y, 10.0F) && near(output[3].width, 20.0F),
        "auto placement finds a row for a spanning item"
    );

    const std::array one_column{veyra::GridTrack{.minimum = 10.0F, .maximum = 10.0F}};
    const std::array one_row{veyra::GridTrack{.minimum = 10.0F, .maximum = 10.0F}};
    const std::array crowded_items{veyra::GridItem{}, veyra::GridItem{}};
    const std::array crowded_measurements{measurements[0], measurements[0]};
    std::array<veyra::GridTrackWorkspace, 1> crowded_column_workspace{};
    std::array<veyra::GridTrackWorkspace, 1> crowded_row_workspace{};
    std::array<veyra::GridItemPlacement, 2> crowded_placements{};
    std::array<std::uint64_t, 1> crowded_occupancy{};
    const auto crowded_workspace = veyra::GridLayoutWorkspace{
        .columns = crowded_column_workspace,
        .rows = crowded_row_workspace,
        .placements = crowded_placements,
        .occupancy = crowded_occupancy,
    };
    std::array<veyra::LayoutRect, 2> crowded_output{};

    const auto crowded_result = veyra::layout_grid(
        container,
        one_column,
        one_row,
        crowded_items,
        crowded_measurements,
        crowded_workspace,
        crowded_output
    );
    require(
        crowded_result == veyra::GridLayoutResult::no_placement,
        "grid reports when automatic items do not fit"
    );
}

void check_degenerate_grids_and_column_pinned_placement() {
    const auto container = veyra::GridContainer{
        .width = 40.0F,
        .height = 20.0F,
    };
    std::array<veyra::GridTrack, 0> no_tracks{};
    std::array<veyra::GridItem, 0> no_items{};
    std::array<veyra::IntrinsicSize, 0> no_measurements{};
    std::array<veyra::GridTrackWorkspace, 0> no_column_workspace{};
    std::array<veyra::GridTrackWorkspace, 0> no_row_workspace{};
    std::array<veyra::GridItemPlacement, 0> no_placements{};
    std::array<std::uint64_t, 0> no_occupancy{};
    const auto empty_workspace = veyra::GridLayoutWorkspace{
        .columns = no_column_workspace,
        .rows = no_row_workspace,
        .placements = no_placements,
        .occupancy = no_occupancy,
    };
    std::array<veyra::LayoutRect, 0> no_output{};

    require(
        veyra::layout_grid(
            container,
            no_tracks,
            no_tracks,
            no_items,
            no_measurements,
            empty_workspace,
            no_output
        ) == veyra::GridLayoutResult::complete,
        "a grid without tracks or items completes with no geometry"
    );

    const std::array one_row{veyra::GridTrack{.minimum = 10.0F, .maximum = 10.0F}};
    const std::array unplaced{veyra::GridItem{}};
    const std::array unplaced_measurements{
        veyra::IntrinsicSize{
            .width = {.min_content = 10.0F, .preferred = 10.0F, .max_content = 10.0F},
            .height = {.min_content = 10.0F, .preferred = 10.0F, .max_content = 10.0F},
        },
    };
    std::array<veyra::GridTrackWorkspace, 1> cell_row_workspace{};
    std::array<veyra::GridItemPlacement, 1> cell_placements{};
    std::array<std::uint64_t, 1> cell_occupancy{};
    const auto cell_workspace = veyra::GridLayoutWorkspace{
        .columns = no_column_workspace,
        .rows = cell_row_workspace,
        .placements = cell_placements,
        .occupancy = cell_occupancy,
    };
    std::array<veyra::LayoutRect, 1> cell_output{veyra::LayoutRect{.x = 7.0F}};

    require(
        veyra::layout_grid(
            container,
            no_tracks,
            one_row,
            unplaced,
            unplaced_measurements,
            cell_workspace,
            cell_output
        ) == veyra::GridLayoutResult::no_placement,
        "automatic items in a grid without cells report no placement"
    );
    require(near(cell_output[0].x, 7.0F), "failed placement leaves output untouched");

    const std::array two_columns{
        veyra::GridTrack{.minimum = 10.0F, .maximum = 10.0F},
        veyra::GridTrack{.minimum = 10.0F, .maximum = 10.0F},
    };
    const std::array too_wide{veyra::GridItem{.column_span = 3}};
    std::array<veyra::GridTrackWorkspace, 2> wide_column_workspace{};
    const auto wide_workspace = veyra::GridLayoutWorkspace{
        .columns = wide_column_workspace,
        .rows = cell_row_workspace,
        .placements = cell_placements,
        .occupancy = cell_occupancy,
    };

    require(
        veyra::layout_grid(
            container,
            two_columns,
            one_row,
            too_wide,
            unplaced_measurements,
            wide_workspace,
            cell_output
        ) == veyra::GridLayoutResult::no_placement,
        "spans wider than the grid report no placement"
    );

    const std::array one_column{veyra::GridTrack{.minimum = 10.0F, .maximum = 10.0F}};
    const std::array four_rows{
        veyra::GridTrack{.minimum = 10.0F, .maximum = 10.0F},
        veyra::GridTrack{.minimum = 10.0F, .maximum = 10.0F},
        veyra::GridTrack{.minimum = 10.0F, .maximum = 10.0F},
        veyra::GridTrack{.minimum = 10.0F, .maximum = 10.0F},
    };
    const std::array pinned{
        veyra::GridItem{.row = 0, .column = 0},
        veyra::GridItem{.column = 0},
        veyra::GridItem{.column = 0},
        veyra::GridItem{.column = 0},
    };
    const std::array pinned_measurements{
        unplaced_measurements[0],
        unplaced_measurements[0],
        unplaced_measurements[0],
        unplaced_measurements[0],
    };
    std::array<veyra::GridTrackWorkspace, 1> pinned_column_workspace{};
    std::array<veyra::GridTrackWorkspace, 4> pinned_row_workspace{};
    std::array<veyra::GridItemPlacement, 4> pinned_placements{};
    std::array<std::uint64_t, 1> pinned_occupancy{};
    const auto pinned_workspace = veyra::GridLayoutWorkspace{
        .columns = pinned_column_workspace,
        .rows = pinned_row_workspace,
        .placements = pinned_placements,
        .occupancy = pinned_occupancy,
    };
    std::array<veyra::LayoutRect, 4> pinned_output{};
    const auto tall_container = veyra::GridContainer{
        .width = 40.0F,
        .height = 40.0F,
    };

    require(
        veyra::layout_grid(
            tall_container,
            one_column,
            four_rows,
            pinned,
            pinned_measurements,
            pinned_workspace,
            pinned_output
        ) == veyra::GridLayoutResult::complete,
        "column-pinned items stack into automatic rows"
    );
    require(
        near(pinned_output[0].y, 0.0F) && near(pinned_output[1].y, 10.0F) &&
            near(pinned_output[2].y, 20.0F) && near(pinned_output[3].y, 30.0F),
        "column-pinned items skip taken cells and fill successive rows"
    );
}

void check_overflow_and_invalid_inputs() {
    const auto container = veyra::GridContainer{
        .width = 40.0F,
        .height = 20.0F,
    };
    const std::array columns{
        veyra::GridTrack{.minimum = 20.0F, .maximum = 20.0F},
        veyra::GridTrack{.minimum = 20.0F, .maximum = 20.0F},
    };
    const std::array rows{veyra::GridTrack{.minimum = 10.0F, .maximum = 10.0F}};
    const std::array items{veyra::GridItem{.column_span = 2}};
    const std::array measurements{
        veyra::IntrinsicSize{
            .width = {.min_content = 50.0F, .preferred = 60.0F, .max_content = 70.0F},
            .height = {.min_content = 5.0F, .preferred = 8.0F, .max_content = 10.0F},
        },
    };
    std::array<veyra::GridTrackWorkspace, 2> column_workspace{};
    std::array<veyra::GridTrackWorkspace, 1> row_workspace{};
    std::array<veyra::GridItemPlacement, 1> placements{};
    std::array<std::uint64_t, 1> occupancy{};
    const auto workspace = veyra::GridLayoutWorkspace{
        .columns = column_workspace,
        .rows = row_workspace,
        .placements = placements,
        .occupancy = occupancy,
    };
    std::array<veyra::LayoutRect, 1> output{};

    auto result =
        veyra::layout_grid(container, columns, rows, items, measurements, workspace, output);
    require(result == veyra::GridLayoutResult::overflow, "min-content wider than a span overflows");
    require(near(output[0].width, 40.0F), "overflow still returns the constrained grid area");

    const std::array invalid_items{veyra::GridItem{.column_span = 0}};
    result = veyra::layout_grid(
        container,
        columns,
        rows,
        invalid_items,
        measurements,
        workspace,
        output
    );
    require(result == veyra::GridLayoutResult::invalid_input, "zero track spans are rejected");

    const std::array invalid_tracks{
        veyra::GridTrack{.minimum = 12.0F, .maximum = 8.0F},
        veyra::GridTrack{.minimum = 20.0F, .maximum = 20.0F},
    };
    result =
        veyra::layout_grid(container, invalid_tracks, rows, items, measurements, workspace, output);
    require(result == veyra::GridLayoutResult::invalid_input, "reversed track limits are rejected");

    const std::array invalid_fraction_tracks{
        veyra::GridTrack{.fraction = -1.0F},
        veyra::GridTrack{},
    };
    result = veyra::layout_grid(
        container,
        invalid_fraction_tracks,
        rows,
        items,
        measurements,
        workspace,
        output
    );
    require(
        result == veyra::GridLayoutResult::invalid_input,
        "negative track fractions are rejected"
    );

    const std::array invalid_measurements{
        veyra::IntrinsicSize{
            .width = {.min_content = 12.0F, .preferred = 10.0F, .max_content = 20.0F},
            .height = {.min_content = 5.0F, .preferred = 8.0F, .max_content = 10.0F},
        },
    };
    result = veyra::layout_grid(
        container,
        columns,
        rows,
        items,
        invalid_measurements,
        workspace,
        output
    );
    require(
        result == veyra::GridLayoutResult::invalid_input,
        "invalid intrinsic sizes are rejected"
    );

    const std::array row_overflow_items{veyra::GridItem{.row = 0, .column = 0, .row_span = 2}};
    result = veyra::layout_grid(
        container,
        columns,
        rows,
        row_overflow_items,
        measurements,
        workspace,
        output
    );
    require(
        result == veyra::GridLayoutResult::invalid_input,
        "row spans past the grid are rejected"
    );

    const std::array column_overflow_items{
        veyra::GridItem{.row = 0, .column = 1, .column_span = 2}
    };
    result = veyra::layout_grid(
        container,
        columns,
        rows,
        column_overflow_items,
        measurements,
        workspace,
        output
    );
    require(
        result == veyra::GridLayoutResult::invalid_input,
        "column spans past the grid are rejected"
    );

    const auto indefinite = veyra::GridContainer{
        .width = std::numeric_limits<float>::quiet_NaN(),
        .height = 20.0F,
    };
    result = veyra::layout_grid(indefinite, columns, rows, items, measurements, workspace, output);
    require(result == veyra::GridLayoutResult::invalid_input, "NaN container extents are rejected");
}

void check_negative_gap_is_rejected() {
    const auto container = veyra::GridContainer{
        .width = 40.0F,
        .height = 20.0F,
        .gap = -1.0F,
    };
    const std::array columns{veyra::GridTrack{.minimum = 10.0F, .maximum = 10.0F}};
    const std::array rows{veyra::GridTrack{.minimum = 10.0F, .maximum = 10.0F}};
    const std::array items{veyra::GridItem{}};
    const std::array measurements{
        veyra::IntrinsicSize{
            .width = {.min_content = 1.0F, .preferred = 2.0F, .max_content = 3.0F},
            .height = {.min_content = 1.0F, .preferred = 2.0F, .max_content = 3.0F},
        },
    };
    std::array<veyra::GridTrackWorkspace, 1> column_workspace{};
    std::array<veyra::GridTrackWorkspace, 1> row_workspace{};
    std::array<veyra::GridItemPlacement, 1> placements{};
    std::array<std::uint64_t, 1> occupancy{};
    const auto workspace = veyra::GridLayoutWorkspace{
        .columns = column_workspace,
        .rows = row_workspace,
        .placements = placements,
        .occupancy = occupancy,
    };
    std::array<veyra::LayoutRect, 1> output{};

    require(
        veyra::layout_grid(container, columns, rows, items, measurements, workspace, output) ==
            veyra::GridLayoutResult::invalid_input,
        "negative gaps are rejected"
    );
}

void check_required_buffers() {
    const auto container = veyra::GridContainer{
        .width = 20.0F,
        .height = 20.0F,
    };
    const std::array columns{veyra::GridTrack{}};
    const std::array rows{veyra::GridTrack{}};
    const std::array items{veyra::GridItem{}};
    const std::array measurements{
        veyra::IntrinsicSize{
            .width = {.min_content = 1.0F, .preferred = 2.0F, .max_content = 3.0F},
            .height = {.min_content = 1.0F, .preferred = 2.0F, .max_content = 3.0F},
        },
    };
    std::array<veyra::GridTrackWorkspace, 1> column_workspace{};
    std::array<veyra::GridTrackWorkspace, 1> row_workspace{};
    std::array<veyra::GridItemPlacement, 1> placements{};
    std::array<std::uint64_t, 1> occupancy{};
    const auto workspace = veyra::GridLayoutWorkspace{
        .columns = column_workspace,
        .rows = row_workspace,
        .placements = placements,
        .occupancy = occupancy,
    };
    std::array<veyra::LayoutRect, 1> output{};

    auto result = veyra::layout_grid(
        container,
        columns,
        rows,
        items,
        std::span<const veyra::IntrinsicSize>{},
        workspace,
        output
    );
    require(
        result == veyra::GridLayoutResult::insufficient_measurements,
        "missing intrinsic measurements are reported"
    );

    result = veyra::layout_grid(
        container,
        columns,
        rows,
        items,
        measurements,
        veyra::GridLayoutWorkspace{
            .rows = row_workspace,
            .placements = placements,
            .occupancy = occupancy,
        },
        output
    );
    require(
        result == veyra::GridLayoutResult::insufficient_workspace,
        "missing track scratch is reported"
    );

    result = veyra::layout_grid(
        container,
        columns,
        rows,
        items,
        measurements,
        veyra::GridLayoutWorkspace{
            .columns = column_workspace,
            .rows = row_workspace,
            .occupancy = occupancy,
        },
        output
    );
    require(
        result == veyra::GridLayoutResult::insufficient_workspace,
        "missing automatic placement scratch is reported"
    );

    result = veyra::layout_grid(
        container,
        columns,
        rows,
        items,
        measurements,
        veyra::GridLayoutWorkspace{
            .columns = column_workspace,
            .rows = row_workspace,
            .placements = placements,
        },
        output
    );
    require(
        result == veyra::GridLayoutResult::insufficient_workspace,
        "missing occupancy scratch is reported"
    );

    result = veyra::layout_grid(
        container,
        columns,
        rows,
        items,
        measurements,
        workspace,
        std::span<veyra::LayoutRect>{}
    );
    require(
        result == veyra::GridLayoutResult::insufficient_output,
        "missing geometry output is reported"
    );
}

void check_large_grid() {
    constexpr std::size_t side = 64;
    constexpr std::size_t item_count = side * side;
    const auto container = veyra::GridContainer{
        .width = 640.0F,
        .height = 640.0F,
        .gap = 2.0F,
    };
    const std::vector<veyra::GridTrack> columns(side);
    const std::vector<veyra::GridTrack> rows(side);
    std::vector<veyra::GridItem> items(item_count);
    const std::vector<veyra::IntrinsicSize> measurements(
        item_count,
        veyra::IntrinsicSize{
            .width = {.min_content = 8.0F, .preferred = 8.0F, .max_content = 8.0F},
            .height = {.min_content = 8.0F, .preferred = 8.0F, .max_content = 8.0F},
        }
    );
    std::vector<veyra::GridTrackWorkspace> column_workspace(side);
    std::vector<veyra::GridTrackWorkspace> row_workspace(side);
    const auto workspace = veyra::GridLayoutWorkspace{
        .columns = column_workspace,
        .rows = row_workspace,
    };
    std::vector<veyra::LayoutRect> output(item_count);
    for (std::size_t index = 0; index < item_count; ++index) {
        items[index] = {
            .row = index / side,
            .column = index % side,
        };
    }

    const auto result =
        veyra::layout_grid(container, columns, rows, items, measurements, workspace, output);
    require(result == veyra::GridLayoutResult::complete, "large fixed-placement grid completes");
    require(
        near(output.back().x, 631.96875F) && near(output.back().y, 631.96875F),
        "large grid preserves final row and column positions"
    );
}

void check_many_tracks_fit_after_float_rounding() {
    constexpr std::size_t row_count = 12'500;
    const auto container = veyra::GridContainer{
        .width = 40.0F,
        .height = static_cast<float>(row_count) * 24.0F,
        .gap = 2.0F,
    };
    const std::array columns{veyra::GridTrack{}};
    const std::vector<veyra::GridTrack> rows(row_count);
    std::vector<veyra::GridItem> items(row_count);
    const std::vector<veyra::IntrinsicSize> measurements(
        row_count,
        veyra::IntrinsicSize{
            .width = {.min_content = 8.0F, .preferred = 8.0F, .max_content = 8.0F},
            .height = {.min_content = 12.0F, .preferred = 20.0F, .max_content = 32.0F},
        }
    );
    std::array<veyra::GridTrackWorkspace, 1> column_workspace{};
    std::vector<veyra::GridTrackWorkspace> row_workspace(row_count);
    std::vector<veyra::GridItemPlacement> placements(row_count);
    std::vector<std::uint64_t> occupancy((row_count + 63) / 64);
    const auto workspace = veyra::GridLayoutWorkspace{
        .columns = column_workspace,
        .rows = row_workspace,
        .placements = placements,
        .occupancy = occupancy,
    };
    std::vector<veyra::LayoutRect> output(row_count);
    for (std::size_t row = 0; row < row_count; ++row) {
        items[row].row = row;
    }

    const auto result =
        veyra::layout_grid(container, columns, rows, items, measurements, workspace, output);
    require(
        result == veyra::GridLayoutResult::complete,
        "float rounding does not overflow a grid that fits its container"
    );
}

}

int main() {
    try {
        check_content_sized_auto_tracks();
        check_track_limits_and_spanning();
        check_tracks_shrink_to_minimum_content();
        check_maximum_content_contributions();
        check_fractional_tracks();
        check_automatic_placement();
        check_degenerate_grids_and_column_pinned_placement();
        check_overflow_and_invalid_inputs();
        check_negative_gap_is_rejected();
        check_required_buffers();
        check_large_grid();
        check_many_tracks_fit_after_float_rounding();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
