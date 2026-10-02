#ifndef VEYRA_LAYOUT_MEASURE_HPP
#define VEYRA_LAYOUT_MEASURE_HPP

#include <cstdint>
#include <optional>

namespace veyra {

struct AxisConstraints {
    float minimum{};
    std::optional<float> maximum;
};

struct LayoutConstraints {
    AxisConstraints width;
    AxisConstraints height;
};

struct MeasuredAxis {
    float min_content{};
    float preferred{};
    float max_content{};
};

struct IntrinsicSize {
    MeasuredAxis width;
    MeasuredAxis height;
    std::optional<float> baseline;
};

struct MeasuredSize {
    MeasuredAxis width;
    MeasuredAxis height;
    std::optional<float> baseline;
};

enum class MeasureResult : std::uint8_t {
    complete,
    overflow,
    invalid_constraints,
    invalid_intrinsic_size,
};

MeasureResult constrain_measurement(
    const LayoutConstraints& constraints,
    const IntrinsicSize& intrinsic,
    MeasuredSize& measured
) noexcept;

}

#endif
