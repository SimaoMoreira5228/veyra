#include "veyra/layout/flex.hpp"

#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

void require(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

bool near(float actual, float expected) {
    return std::abs(actual - expected) < 0.001F;
}

void check_unrepresentable_coordinates() {
    const auto maximum = std::numeric_limits<float>::max();
    const auto container = veyra::FlexContainer{
        .width = maximum,
        .height = maximum,
        .padding = {.top = maximum},
        .align_items = veyra::FlexAlign::baseline,
    };
    const std::array items{
        veyra::FlexItem{
            .baseline = 0.0F,
        },
        veyra::FlexItem{
            .cross_size = maximum,
            .baseline = maximum,
        },
    };
    std::array<veyra::LayoutRect, 2> output{
        veyra::LayoutRect{.x = 3.0F, .y = 4.0F},
        veyra::LayoutRect{.x = 5.0F, .y = 6.0F},
    };

    const auto result = veyra::layout_flex(container, items, output);
    require(
        result == veyra::FlexLayoutResult::invalid_input,
        "unrepresentable coordinates are rejected"
    );
    require(
        near(output[0].x, 3.0F) && near(output[0].y, 4.0F),
        "invalid first item leaves output unchanged"
    );
    require(
        near(output[1].x, 5.0F) && near(output[1].y, 6.0F),
        "invalid layout leaves later output unchanged"
    );
}

void check_growth_shrink_and_justification() {
    const std::array items{
        veyra::FlexItem{
            .basis = 20.0F,
            .min_main = 10.0F,
            .max_main = 40.0F,
            .grow = 1.0F,
            .cross_size = 10.0F,
        },
        veyra::FlexItem{
            .basis = 20.0F,
            .min_main = 10.0F,
            .max_main = 100.0F,
            .grow = 1.0F,
            .cross_size = 10.0F,
        },
    };
    std::array<veyra::LayoutRect, 2> output{};
    auto container = veyra::FlexContainer{
        .width = 100.0F,
        .height = 30.0F,
        .gap = 10.0F,
        .align_items = veyra::FlexAlign::center,
    };

    auto result = veyra::layout_flex(container, items, output);
    require(result == veyra::FlexLayoutResult::complete, "grow fills the available main axis");
    require(near(output[0].width, 40.0F), "grow respects the first item maximum");
    require(near(output[1].width, 50.0F), "remaining free space reaches the second item");
    require(near(output[0].x, 0.0F) && near(output[1].x, 50.0F), "gap separates items");
    require(near(output[0].y, 10.0F) && near(output[0].height, 10.0F), "cross alignment centers");

    container.width = 40.0F;
    container.justify_content = veyra::FlexJustify::end;
    result = veyra::layout_flex(container, items, output);
    require(result == veyra::FlexLayoutResult::complete, "shrink fits the container");
    require(
        near(output[0].width, 15.0F) && near(output[1].width, 15.0F),
        "shrink is weighted by basis"
    );
    require(near(output[0].x, 0.0F), "shrink leaves no extra justification space");

    const std::array constrained_items{
        veyra::FlexItem{.basis = 20.0F, .min_main = 18.0F},
        veyra::FlexItem{.basis = 20.0F},
    };
    container.width = 40.0F;
    container.justify_content = veyra::FlexJustify::start;
    result = veyra::layout_flex(container, constrained_items, output);
    require(result == veyra::FlexLayoutResult::complete, "shrink redistributes around minimums");
    require(
        near(output[0].width, 18.0F) && near(output[1].width, 12.0F),
        "minimum freezes its item"
    );
}

void check_padding_column_and_space_between() {
    const std::array items{
        veyra::FlexItem{.basis = 10.0F, .min_main = 10.0F, .max_main = 10.0F},
        veyra::FlexItem{.basis = 10.0F, .min_main = 10.0F, .max_main = 10.0F},
    };
    std::array<veyra::LayoutRect, 2> output{};
    const auto container = veyra::FlexContainer{
        .width = 40.0F,
        .height = 100.0F,
        .padding = {.top = 5.0F, .right = 4.0F, .bottom = 5.0F, .left = 4.0F},
        .direction = veyra::FlexDirection::column,
        .align_items = veyra::FlexAlign::stretch,
        .justify_content = veyra::FlexJustify::space_between,
    };

    const auto result = veyra::layout_flex(container, items, output);
    require(result == veyra::FlexLayoutResult::complete, "column layout completes");
    require(
        near(output[0].x, 4.0F) && near(output[0].width, 32.0F),
        "stretch fills padded cross axis"
    );
    require(
        near(output[0].y, 5.0F) && near(output[1].y, 85.0F),
        "space-between distributes free space"
    );
}

void check_overflow_and_invalid_inputs() {
    const std::array items{
        veyra::FlexItem{.basis = 20.0F, .min_main = 20.0F, .max_main = 20.0F},
        veyra::FlexItem{.basis = 20.0F, .min_main = 20.0F, .max_main = 20.0F},
    };
    std::array<veyra::LayoutRect, 2> output{};
    const auto container = veyra::FlexContainer{
        .width = 30.0F,
        .height = 10.0F,
    };

    auto result = veyra::layout_flex(container, items, output);
    require(result == veyra::FlexLayoutResult::overflow, "minimum sizes report overflow");
    require(
        near(output[1].x, 20.0F) && near(output[1].width, 20.0F),
        "overflow keeps child geometry"
    );

    const std::array cross_overflow_items{
        veyra::FlexItem{.basis = 10.0F, .cross_size = 20.0F},
    };
    const auto short_cross_container = veyra::FlexContainer{
        .width = 30.0F,
        .height = 10.0F,
        .align_items = veyra::FlexAlign::start,
    };
    result = veyra::layout_flex(short_cross_container, cross_overflow_items, output);
    require(result == veyra::FlexLayoutResult::overflow, "cross-axis overflow is reported");

    const std::array invalid_items{
        veyra::FlexItem{.basis = 10.0F, .min_main = 20.0F, .max_main = 10.0F},
    };
    result = veyra::layout_flex(container, invalid_items, output);
    require(result == veyra::FlexLayoutResult::invalid_input, "invalid min and max are rejected");

    std::array short_output{veyra::LayoutRect{.x = 7.0F}};
    result = veyra::layout_flex(container, items, short_output);
    require(result == veyra::FlexLayoutResult::insufficient_output, "short output is rejected");
    require(near(short_output[0].x, 7.0F), "short output remains unchanged");
}

void check_measured_layout_and_child_constraints() {
    const auto container = veyra::FlexContainer{
        .width = 100.0F,
        .height = 40.0F,
        .align_items = veyra::FlexAlign::start,
    };
    const std::array styles{
        veyra::FlexItemStyle{
            .max_main = 80.0F,
            .grow = 1.0F,
            .max_cross = 30.0F,
        },
        veyra::FlexItemStyle{
            .basis = 20.0F,
            .max_main = 40.0F,
            .grow = 1.0F,
        },
    };
    const std::array intrinsic{
        veyra::IntrinsicSize{
            .width = {.min_content = 10.0F, .preferred = 30.0F, .max_content = 80.0F},
            .height = {.min_content = 5.0F, .preferred = 10.0F, .max_content = 20.0F},
            .baseline = 8.0F,
        },
        veyra::IntrinsicSize{
            .width = {.min_content = 5.0F, .preferred = 10.0F, .max_content = 40.0F},
            .height = {.min_content = 4.0F, .preferred = 20.0F, .max_content = 30.0F},
        },
    };
    std::array<veyra::FlexItem, 2> resolved{};
    std::array<veyra::LayoutRect, 2> output{};

    veyra::LayoutConstraints constraints;
    require(
        veyra::flex_item_constraints(container, styles[0], constraints),
        "parent derives constraints for the child"
    );
    require(
        constraints.width.maximum && near(*constraints.width.maximum, 80.0F),
        "main-axis maximum respects the item style"
    );
    require(
        constraints.height.maximum && near(*constraints.height.maximum, 30.0F),
        "cross-axis maximum respects the item style"
    );

    const auto result = veyra::layout_flex_measured(container, styles, intrinsic, resolved, output);
    require(result == veyra::FlexLayoutResult::complete, "measured children produce flex geometry");
    require(
        near(output[0].width, 60.0F) && near(output[1].width, 40.0F),
        "measured basis drives growth"
    );
    require(
        near(output[0].height, 10.0F) && near(output[1].height, 20.0F),
        "measured cross sizes are used"
    );
    require(near(output[1].x, 60.0F), "explicit flex basis overrides intrinsic preference");
}

void check_measured_overflow_and_capacity() {
    const auto container = veyra::FlexContainer{
        .width = 8.0F,
        .height = 15.0F,
        .align_items = veyra::FlexAlign::start,
    };
    const std::array styles{veyra::FlexItemStyle{}};
    const std::array intrinsic{
        veyra::IntrinsicSize{
            .width = {.min_content = 20.0F, .preferred = 25.0F, .max_content = 30.0F},
            .height = {.min_content = 5.0F, .preferred = 10.0F, .max_content = 12.0F},
        },
    };
    std::array<veyra::FlexItem, 1> resolved{};
    std::array<veyra::LayoutRect, 1> output{};

    auto result = veyra::layout_flex_measured(container, styles, intrinsic, resolved, output);
    require(result == veyra::FlexLayoutResult::overflow, "min-content overflow reaches the parent");
    require(near(output[0].width, 8.0F), "overflow still returns constrained geometry");

    result = veyra::layout_flex_measured(
        container,
        styles,
        std::span<const veyra::IntrinsicSize>{},
        resolved,
        output
    );
    require(
        result == veyra::FlexLayoutResult::insufficient_measurements,
        "missing measurements are reported"
    );

    result = veyra::layout_flex_measured(
        container,
        styles,
        intrinsic,
        std::span<veyra::FlexItem>{},
        output
    );
    require(
        result == veyra::FlexLayoutResult::insufficient_workspace,
        "missing scratch is reported"
    );

    result = veyra::layout_flex_measured(
        container,
        styles,
        intrinsic,
        resolved,
        std::span<veyra::LayoutRect>{}
    );
    require(result == veyra::FlexLayoutResult::insufficient_output, "missing output is reported");

    const std::array invalid_styles{
        veyra::FlexItemStyle{.min_main = 10.0F, .max_main = 5.0F},
    };
    result = veyra::layout_flex_measured(container, invalid_styles, intrinsic, resolved, output);
    require(result == veyra::FlexLayoutResult::invalid_input, "invalid item styles are rejected");

    veyra::LayoutConstraints constraints;
    require(
        !veyra::flex_item_constraints(container, invalid_styles[0], constraints),
        "invalid item styles cannot produce child constraints"
    );
}

void check_measured_column_constraints() {
    const auto container = veyra::FlexContainer{
        .width = 40.0F,
        .height = 80.0F,
        .padding = {.top = 5.0F, .left = 4.0F},
        .direction = veyra::FlexDirection::column,
        .align_items = veyra::FlexAlign::start,
    };
    const std::array styles{
        veyra::FlexItemStyle{
            .min_main = 5.0F,
            .max_main = 60.0F,
            .min_cross = 6.0F,
            .max_cross = 30.0F,
        },
    };
    const std::array intrinsic{
        veyra::IntrinsicSize{
            .width = {.min_content = 10.0F, .preferred = 12.0F, .max_content = 20.0F},
            .height = {.min_content = 8.0F, .preferred = 20.0F, .max_content = 40.0F},
            .baseline = 15.0F,
        },
    };
    std::array<veyra::FlexItem, 1> resolved{};
    std::array<veyra::LayoutRect, 1> output{};
    veyra::LayoutConstraints constraints;

    require(
        veyra::flex_item_constraints(container, styles[0], constraints),
        "column parent derives child constraints"
    );
    require(
        constraints.width.minimum == 6.0F && constraints.height.minimum == 5.0F,
        "column constraints map main and cross axes"
    );

    const auto result = veyra::layout_flex_measured(container, styles, intrinsic, resolved, output);
    require(result == veyra::FlexLayoutResult::complete, "measured column layout completes");
    require(
        near(output[0].x, 4.0F) && near(output[0].y, 5.0F),
        "column measurement preserves padded origin"
    );
    require(
        near(output[0].width, 12.0F) && near(output[0].height, 20.0F),
        "column measurement maps intrinsic dimensions"
    );
}

void check_baseline_alignment() {
    auto container = veyra::FlexContainer{
        .width = 100.0F,
        .height = 40.0F,
        .align_items = veyra::FlexAlign::baseline,
    };
    const std::array styles{veyra::FlexItemStyle{}, veyra::FlexItemStyle{}};
    const std::array intrinsic{
        veyra::IntrinsicSize{
            .width = {.min_content = 20.0F, .preferred = 20.0F, .max_content = 20.0F},
            .height = {.min_content = 20.0F, .preferred = 20.0F, .max_content = 20.0F},
            .baseline = 15.0F,
        },
        veyra::IntrinsicSize{
            .width = {.min_content = 20.0F, .preferred = 20.0F, .max_content = 20.0F},
            .height = {.min_content = 30.0F, .preferred = 30.0F, .max_content = 30.0F},
        },
    };
    std::array<veyra::FlexItem, 2> resolved{};
    std::array<veyra::LayoutRect, 2> output{};

    auto result = veyra::layout_flex_measured(container, styles, intrinsic, resolved, output);
    require(result == veyra::FlexLayoutResult::complete, "measured baselines align in a row");
    require(
        near(output[0].y + resolved[0].baseline.value_or(-1.0F), output[1].y + output[1].height),
        "items without a baseline align their bottom edge"
    );
    require(near(output[0].y, 15.0F) && near(output[1].y, 0.0F), "baseline offsets are applied");
    result = veyra::layout_flex(container, resolved, output);
    require(
        result == veyra::FlexLayoutResult::complete,
        "direct flex items accept valid baselines"
    );

    const std::array invalid_baseline_items{
        veyra::FlexItem{
            .cross_size = 10.0F,
            .baseline = 11.0F,
        },
    };
    result = veyra::layout_flex(container, invalid_baseline_items, output);
    require(result == veyra::FlexLayoutResult::invalid_input, "baselines cannot exceed item size");

    container.height = 20.0F;
    result = veyra::layout_flex_measured(container, styles, intrinsic, resolved, output);
    require(result == veyra::FlexLayoutResult::overflow, "baseline offset overflow is reported");

    container.height = 50.0F;
    container.gap = 5.0F;
    container.wrap = veyra::FlexWrap::wrap;
    container.width = 65.0F;
    const std::array wrapped_styles{
        veyra::FlexItemStyle{},
        veyra::FlexItemStyle{},
        veyra::FlexItemStyle{},
    };
    const std::array wrapped_intrinsic{
        veyra::IntrinsicSize{
            .width = {.min_content = 30.0F, .preferred = 30.0F, .max_content = 30.0F},
            .height = {.min_content = 20.0F, .preferred = 20.0F, .max_content = 20.0F},
            .baseline = 15.0F,
        },
        veyra::IntrinsicSize{
            .width = {.min_content = 30.0F, .preferred = 30.0F, .max_content = 30.0F},
            .height = {.min_content = 30.0F, .preferred = 30.0F, .max_content = 30.0F},
            .baseline = 10.0F,
        },
        veyra::IntrinsicSize{
            .width = {.min_content = 30.0F, .preferred = 30.0F, .max_content = 30.0F},
            .height = {.min_content = 10.0F, .preferred = 10.0F, .max_content = 10.0F},
            .baseline = 5.0F,
        },
    };
    std::array<veyra::FlexItem, 3> wrapped_resolved{};
    std::array<veyra::LayoutRect, 3> wrapped_output{};

    result = veyra::layout_flex_measured(
        container,
        wrapped_styles,
        wrapped_intrinsic,
        wrapped_resolved,
        wrapped_output
    );
    require(result == veyra::FlexLayoutResult::complete, "wrapped rows fit baseline line metrics");
    require(
        near(wrapped_output[0].y, 0.0F) && near(wrapped_output[1].y, 5.0F) &&
            near(wrapped_output[2].y, 40.0F),
        "wrapped baseline groups size their lines before placement"
    );

    container.direction = veyra::FlexDirection::column;
    require(
        veyra::layout_flex(container, std::span<const veyra::FlexItem>{}, output) ==
            veyra::FlexLayoutResult::invalid_input,
        "baseline alignment requires a row direction"
    );
}

void check_wrapped_flex_lines() {
    const auto container = veyra::FlexContainer{
        .width = 100.0F,
        .height = 60.0F,
        .padding = {.top = 3.0F, .left = 5.0F},
        .gap = 10.0F,
        .wrap = veyra::FlexWrap::wrap,
        .align_items = veyra::FlexAlign::start,
    };
    const std::array items{
        veyra::FlexItem{.basis = 40.0F, .cross_size = 12.0F},
        veyra::FlexItem{.basis = 40.0F, .cross_size = 18.0F},
        veyra::FlexItem{.basis = 40.0F, .cross_size = 15.0F},
    };
    std::array<veyra::LayoutRect, 3> output{};

    auto result = veyra::layout_flex(container, items, output);
    require(result == veyra::FlexLayoutResult::complete, "wrapped row fits its lines");
    require(
        near(output[0].x, 5.0F) && near(output[1].x, 55.0F) && near(output[2].x, 5.0F),
        "row wraps before the item that exceeds the available main size"
    );
    require(
        near(output[0].y, 3.0F) && near(output[1].y, 3.0F) && near(output[2].y, 31.0F),
        "row lines use their largest child cross size and the configured gap"
    );

    auto aligned_container = container;
    aligned_container.align_content = veyra::FlexAlignContent::center;
    result = veyra::layout_flex(aligned_container, items, output);
    require(
        result == veyra::FlexLayoutResult::complete && near(output[0].y, 10.0F) &&
            near(output[2].y, 38.0F),
        "center alignment distributes free cross-axis space around the lines"
    );

    aligned_container.align_content = veyra::FlexAlignContent::end;
    result = veyra::layout_flex(aligned_container, items, output);
    require(
        result == veyra::FlexLayoutResult::complete && near(output[0].y, 17.0F) &&
            near(output[2].y, 45.0F),
        "end alignment places free cross-axis space before the lines"
    );

    aligned_container.align_content = veyra::FlexAlignContent::space_between;
    result = veyra::layout_flex(aligned_container, items, output);
    require(
        result == veyra::FlexLayoutResult::complete && near(output[0].y, 3.0F) &&
            near(output[2].y, 45.0F),
        "space-between alignment distributes free space between lines"
    );

    aligned_container.align_content = veyra::FlexAlignContent::stretch;
    aligned_container.align_items = veyra::FlexAlign::stretch;
    result = veyra::layout_flex(aligned_container, items, output);
    require(
        result == veyra::FlexLayoutResult::complete && near(output[0].height, 25.0F) &&
            near(output[2].height, 22.0F),
        "stretch alignment expands each line into the available cross space"
    );

    const auto short_container = veyra::FlexContainer{
        .width = 100.0F,
        .height = 20.0F,
        .gap = 10.0F,
        .wrap = veyra::FlexWrap::wrap,
        .align_items = veyra::FlexAlign::start,
    };
    result = veyra::layout_flex(short_container, items, output);
    require(result == veyra::FlexLayoutResult::overflow, "wrapped cross-axis overflow is reported");

    const std::array oversized_items{
        veyra::FlexItem{
            .basis = 120.0F,
            .min_main = 120.0F,
            .max_main = 120.0F,
            .cross_size = 10.0F,
        },
    };
    result = veyra::layout_flex(container, oversized_items, output);
    require(result == veyra::FlexLayoutResult::overflow, "oversized wrapped items report overflow");
    require(near(output[0].width, 120.0F), "wrapped overflow preserves the item's minimum size");

    const auto column_container = veyra::FlexContainer{
        .width = 60.0F,
        .height = 100.0F,
        .gap = 10.0F,
        .direction = veyra::FlexDirection::column,
        .wrap = veyra::FlexWrap::wrap,
        .align_items = veyra::FlexAlign::start,
    };
    const std::array column_items{
        veyra::FlexItem{.basis = 40.0F, .cross_size = 12.0F},
        veyra::FlexItem{.basis = 40.0F, .cross_size = 18.0F},
        veyra::FlexItem{.basis = 40.0F, .cross_size = 15.0F},
    };
    result = veyra::layout_flex(column_container, column_items, output);
    require(result == veyra::FlexLayoutResult::complete, "wrapped column fits its lines");
    require(
        near(output[0].y, 0.0F) && near(output[1].y, 50.0F) && near(output[2].y, 0.0F),
        "column wraps along its main axis"
    );
    require(
        near(output[0].x, 0.0F) && near(output[1].x, 0.0F) && near(output[2].x, 28.0F),
        "column lines advance along the cross axis"
    );
}

void check_measured_flex_wrap() {
    const auto container = veyra::FlexContainer{
        .width = 40.0F,
        .height = 30.0F,
        .wrap = veyra::FlexWrap::wrap,
        .align_items = veyra::FlexAlign::start,
    };
    const std::array styles{veyra::FlexItemStyle{}, veyra::FlexItemStyle{}};
    const std::array intrinsic{
        veyra::IntrinsicSize{
            .width = {.min_content = 10.0F, .preferred = 25.0F, .max_content = 30.0F},
            .height = {.min_content = 5.0F, .preferred = 10.0F, .max_content = 12.0F},
        },
        veyra::IntrinsicSize{
            .width = {.min_content = 10.0F, .preferred = 25.0F, .max_content = 30.0F},
            .height = {.min_content = 5.0F, .preferred = 10.0F, .max_content = 12.0F},
        },
    };
    std::array<veyra::FlexItem, 2> resolved{};
    std::array<veyra::LayoutRect, 2> output{};

    const auto result = veyra::layout_flex_measured(container, styles, intrinsic, resolved, output);
    require(result == veyra::FlexLayoutResult::complete, "measured items wrap by intrinsic basis");
    require(
        near(output[0].y, 0.0F) && near(output[1].y, 10.0F),
        "measured line positions use intrinsic cross sizes"
    );
}

}

int main() {
    try {
        check_unrepresentable_coordinates();
        check_growth_shrink_and_justification();
        check_padding_column_and_space_between();
        check_overflow_and_invalid_inputs();
        check_measured_layout_and_child_constraints();
        check_measured_overflow_and_capacity();
        check_measured_column_constraints();
        check_baseline_alignment();
        check_wrapped_flex_lines();
        check_measured_flex_wrap();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
