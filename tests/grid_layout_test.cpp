#include "veyra/layout/grid.hpp"

#include <array>
#include <cmath>
#include <iostream>
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
        veyra::GridItem{.column = 0},
        veyra::GridItem{.column = 1},
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
    std::array<veyra::LayoutRect, 2> output{};

    const auto result = veyra::layout_grid(
        container,
        columns,
        rows,
        items,
        measurements,
        column_workspace,
        row_workspace,
        output
    );
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
    std::array<veyra::LayoutRect, 1> output{};

    const auto result = veyra::layout_grid(
        container,
        columns,
        rows,
        items,
        measurements,
        column_workspace,
        row_workspace,
        output
    );
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
        veyra::GridItem{.column = 0},
        veyra::GridItem{.column = 1},
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
    std::array<veyra::LayoutRect, 2> output{};

    const auto result = veyra::layout_grid(
        container,
        columns,
        rows,
        items,
        measurements,
        column_workspace,
        row_workspace,
        output
    );
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
        veyra::GridItem{.column = 0},
        veyra::GridItem{.column = 1},
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
    std::array<veyra::LayoutRect, 2> output{};

    const auto result = veyra::layout_grid(
        container,
        columns,
        rows,
        items,
        measurements,
        column_workspace,
        row_workspace,
        output
    );
    require(result == veyra::GridLayoutResult::complete, "max-content tracks fit the container");
    require(
        near(output[0].width, 37.14286F) && near(output[1].width, 22.85714F),
        "max-content contributions shape track growth before the grid shrinks"
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
    std::array<veyra::LayoutRect, 1> output{};

    auto result = veyra::layout_grid(
        container,
        columns,
        rows,
        items,
        measurements,
        column_workspace,
        row_workspace,
        output
    );
    require(result == veyra::GridLayoutResult::overflow, "min-content wider than a span overflows");
    require(near(output[0].width, 40.0F), "overflow still returns the constrained grid area");

    const std::array invalid_items{veyra::GridItem{.column_span = 0}};
    result = veyra::layout_grid(
        container,
        columns,
        rows,
        invalid_items,
        measurements,
        column_workspace,
        row_workspace,
        output
    );
    require(result == veyra::GridLayoutResult::invalid_input, "zero track spans are rejected");

    const std::array invalid_tracks{
        veyra::GridTrack{.minimum = 12.0F, .maximum = 8.0F},
        veyra::GridTrack{.minimum = 20.0F, .maximum = 20.0F},
    };
    result = veyra::layout_grid(
        container,
        invalid_tracks,
        rows,
        items,
        measurements,
        column_workspace,
        row_workspace,
        output
    );
    require(result == veyra::GridLayoutResult::invalid_input, "reversed track limits are rejected");

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
        column_workspace,
        row_workspace,
        output
    );
    require(
        result == veyra::GridLayoutResult::invalid_input,
        "invalid intrinsic sizes are rejected"
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
    std::array<veyra::LayoutRect, 1> output{};

    auto result = veyra::layout_grid(
        container,
        columns,
        rows,
        items,
        std::span<const veyra::IntrinsicSize>{},
        column_workspace,
        row_workspace,
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
        std::span<veyra::GridTrackWorkspace>{},
        row_workspace,
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
        column_workspace,
        row_workspace,
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
    std::vector<veyra::LayoutRect> output(item_count);
    for (std::size_t index = 0; index < item_count; ++index) {
        items[index] = {
            .row = index / side,
            .column = index % side,
        };
    }

    const auto result = veyra::layout_grid(
        container,
        columns,
        rows,
        items,
        measurements,
        column_workspace,
        row_workspace,
        output
    );
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
    std::vector<veyra::LayoutRect> output(row_count);
    for (std::size_t row = 0; row < row_count; ++row) {
        items[row].row = row;
    }

    const auto result = veyra::layout_grid(
        container,
        columns,
        rows,
        items,
        measurements,
        column_workspace,
        row_workspace,
        output
    );
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
        check_overflow_and_invalid_inputs();
        check_required_buffers();
        check_large_grid();
        check_many_tracks_fit_after_float_rounding();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
