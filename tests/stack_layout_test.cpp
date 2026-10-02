#include "veyra/layout/stack.hpp"

#include <array>
#include <cmath>
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

bool near(float actual, float expected) {
    return std::abs(actual - expected) < 0.001F;
}

void check_center_and_z_order() {
    const std::array items{
        veyra::StackItem{.width = 20.0F, .height = 10.0F},
        veyra::StackItem{.width = 10.0F, .height = 20.0F},
    };
    std::array<veyra::LayoutRect, 2> output{};
    const auto container = veyra::StackContainer{
        .width = 100.0F,
        .height = 80.0F,
        .padding = {.top = 10.0F, .right = 10.0F, .bottom = 10.0F, .left = 10.0F},
        .align_x = veyra::StackAlignment::center,
        .align_y = veyra::StackAlignment::center,
    };

    const auto result = veyra::layout_stack(container, items, output);
    require(result == veyra::StackLayoutResult::complete, "centered stack completes");
    require(near(output[0].x, 40.0F) && near(output[0].y, 35.0F), "first item is centered");
    require(
        near(output[1].x, 45.0F) && near(output[1].y, 30.0F),
        "second item bounds are centered independently"
    );
}

void check_stretch_and_overflow() {
    const std::array items{
        veyra::StackItem{.width = 20.0F, .height = 10.0F, .min_width = 30.0F},
    };
    std::array<veyra::LayoutRect, 1> output{};
    const auto container = veyra::StackContainer{
        .width = 100.0F,
        .height = 60.0F,
        .padding = {.top = 5.0F, .right = 10.0F, .bottom = 5.0F, .left = 10.0F},
        .align_x = veyra::StackAlignment::stretch,
        .align_y = veyra::StackAlignment::stretch,
    };

    auto result = veyra::layout_stack(container, items, output);
    require(result == veyra::StackLayoutResult::complete, "stretch fills the padded content box");
    require(
        near(output[0].x, 10.0F) && near(output[0].width, 80.0F),
        "stretch spans available width"
    );
    require(
        near(output[0].y, 5.0F) && near(output[0].height, 50.0F),
        "stretch spans available height"
    );

    const std::array oversized{veyra::StackItem{.width = 120.0F, .height = 10.0F}};
    auto overflow_container = container;
    overflow_container.align_x = veyra::StackAlignment::start;
    overflow_container.align_y = veyra::StackAlignment::start;
    result = veyra::layout_stack(overflow_container, oversized, output);
    require(result == veyra::StackLayoutResult::overflow, "oversized items report overflow");
}

void check_invalid_input_and_capacity() {
    const auto container = veyra::StackContainer{.width = 40.0F, .height = 40.0F};
    const std::array invalid_items{
        veyra::StackItem{.width = 20.0F, .min_width = 30.0F, .max_width = 10.0F},
    };
    std::array<veyra::LayoutRect, 1> output{veyra::LayoutRect{.x = 7.0F}};

    auto result = veyra::layout_stack(container, invalid_items, output);
    require(
        result == veyra::StackLayoutResult::invalid_input,
        "invalid item constraints are rejected"
    );
    require(near(output[0].x, 7.0F), "invalid input leaves output unchanged");

    const std::array items{
        veyra::StackItem{},
        veyra::StackItem{},
    };
    result = veyra::layout_stack(container, items, output);
    require(result == veyra::StackLayoutResult::insufficient_output, "short output is rejected");
    require(near(output[0].x, 7.0F), "short output leaves supplied storage unchanged");
}

}

int main() {
    try {
        check_center_and_z_order();
        check_stretch_and_overflow();
        check_invalid_input_and_capacity();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
