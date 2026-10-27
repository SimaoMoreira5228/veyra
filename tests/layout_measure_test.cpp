#include "veyra/layout/core/measure.hpp"

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

veyra::IntrinsicSize text_size() {
    return {
        .width = {.min_content = 20.0F, .preferred = 60.0F, .max_content = 100.0F},
        .height = {.min_content = 10.0F, .preferred = 30.0F, .max_content = 50.0F},
        .baseline = 40.0F,
    };
}

void check_finite_and_unbounded_constraints() {
    const auto intrinsic = text_size();
    const auto finite = veyra::LayoutConstraints{
        .width = {.minimum = 30.0F, .maximum = 40.0F},
        .height = {.minimum = 20.0F, .maximum = 25.0F},
    };
    veyra::MeasuredSize measured;

    auto result = veyra::constrain_measurement(finite, intrinsic, measured);
    require(result == veyra::MeasureResult::complete, "finite constraints are measured");
    require(near(measured.width.preferred, 40.0F), "finite maximum clamps preferred width");
    require(near(measured.height.preferred, 25.0F), "finite maximum clamps preferred height");
    require(
        near(measured.baseline.value_or(-1.0F), 25.0F),
        "baseline stays within measured height"
    );
    require(near(measured.width.min_content, 20.0F), "intrinsic minimum remains available");
    require(near(measured.width.max_content, 100.0F), "intrinsic maximum remains available");

    const auto too_narrow = veyra::LayoutConstraints{
        .width = {.maximum = 10.0F},
        .height = {},
    };
    result = veyra::constrain_measurement(too_narrow, intrinsic, measured);
    require(result == veyra::MeasureResult::overflow, "maximum below min-content reports overflow");
    require(near(measured.width.preferred, 10.0F), "overflow still returns constrained geometry");

    const auto unbounded = veyra::LayoutConstraints{
        .width = {.minimum = 75.0F},
        .height = {},
    };
    result = veyra::constrain_measurement(unbounded, intrinsic, measured);
    require(result == veyra::MeasureResult::complete, "unbounded maximum is accepted");
    require(near(measured.width.preferred, 75.0F), "minimum constraint raises preferred width");
    require(near(measured.height.preferred, 30.0F), "unbounded axis keeps preferred height");
    require(near(measured.baseline.value_or(-1.0F), 30.0F), "baseline clamps to measured height");
}

void check_invalid_inputs_preserve_output() {
    const auto intrinsic = text_size();
    const auto valid_constraints = veyra::LayoutConstraints{};
    veyra::MeasuredSize measured{
        .width = {.min_content = 1.0F, .preferred = 2.0F, .max_content = 3.0F},
        .height = {.min_content = 4.0F, .preferred = 5.0F, .max_content = 6.0F},
        .baseline = 5.0F,
    };

    const auto invalid_constraints = veyra::LayoutConstraints{
        .width = {.minimum = 50.0F, .maximum = 40.0F},
        .height = {},
    };
    auto result = veyra::constrain_measurement(invalid_constraints, intrinsic, measured);
    require(
        result == veyra::MeasureResult::invalid_constraints,
        "inverted constraints are rejected"
    );
    require(near(measured.width.preferred, 2.0F), "invalid constraints leave output unchanged");

    auto invalid_intrinsic = intrinsic;
    invalid_intrinsic.width.max_content = 10.0F;
    result = veyra::constrain_measurement(valid_constraints, invalid_intrinsic, measured);
    require(
        result == veyra::MeasureResult::invalid_intrinsic_size,
        "inconsistent intrinsic size is rejected"
    );
    require(
        near(measured.height.preferred, 5.0F),
        "invalid intrinsic size leaves output unchanged"
    );

    invalid_intrinsic = intrinsic;
    invalid_intrinsic.baseline = std::numeric_limits<float>::infinity();
    result = veyra::constrain_measurement(valid_constraints, invalid_intrinsic, measured);
    require(
        result == veyra::MeasureResult::invalid_intrinsic_size,
        "non-finite baseline is rejected"
    );
}

}

int main() {
    try {
        check_finite_and_unbounded_constraints();
        check_invalid_inputs_preserve_output();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
