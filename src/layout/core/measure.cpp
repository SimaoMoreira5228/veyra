#include "veyra/layout/core/measure.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace veyra {
namespace {

bool valid_constraints(const AxisConstraints& constraints) noexcept {
    return std::isfinite(constraints.minimum) && constraints.minimum >= 0.0F &&
        (!constraints.maximum ||
         (std::isfinite(*constraints.maximum) && *constraints.maximum >= constraints.minimum));
}

bool valid_intrinsic_axis(const MeasuredAxis& axis) noexcept {
    return std::isfinite(axis.min_content) && axis.min_content >= 0.0F &&
        std::isfinite(axis.preferred) && axis.preferred >= axis.min_content &&
        std::isfinite(axis.max_content) && axis.max_content >= axis.preferred;
}

float constrain_preferred(const AxisConstraints& constraints, float preferred) noexcept {
    const auto maximum = constraints.maximum.value_or(std::numeric_limits<float>::max());
    return std::clamp(preferred, constraints.minimum, maximum);
}

}

MeasureResult constrain_measurement(
    const LayoutConstraints& constraints,
    const IntrinsicSize& intrinsic,
    MeasuredSize& measured
) noexcept {
    if (!valid_constraints(constraints.width) || !valid_constraints(constraints.height)) {
        return MeasureResult::invalid_constraints;
    }
    if (!valid_intrinsic_axis(intrinsic.width) || !valid_intrinsic_axis(intrinsic.height) ||
        (intrinsic.baseline &&
         (!std::isfinite(*intrinsic.baseline) || *intrinsic.baseline < 0.0F ||
          *intrinsic.baseline > intrinsic.height.max_content))) {
        return MeasureResult::invalid_intrinsic_size;
    }

    auto result = MeasuredSize{
        .width = intrinsic.width,
        .height = intrinsic.height,
        .baseline = intrinsic.baseline,
    };
    result.width.preferred = constrain_preferred(constraints.width, result.width.preferred);
    result.height.preferred = constrain_preferred(constraints.height, result.height.preferred);
    if (result.baseline) {
        result.baseline = std::min(*result.baseline, result.height.preferred);
    }

    measured = result;
    const bool overflows_width =
        constraints.width.maximum && *constraints.width.maximum < intrinsic.width.min_content;
    const bool overflows_height =
        constraints.height.maximum && *constraints.height.maximum < intrinsic.height.min_content;
    return overflows_width || overflows_height ? MeasureResult::overflow : MeasureResult::complete;
}

}
