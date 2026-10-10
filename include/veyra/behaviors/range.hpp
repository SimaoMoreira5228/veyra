#ifndef VEYRA_BEHAVIORS_RANGE_HPP
#define VEYRA_BEHAVIORS_RANGE_HPP

#include "veyra/core/semantics.hpp"

#include <cstdint>
#include <string_view>

namespace veyra {

enum class RangeKind : std::uint8_t {
    slider,
    scrollbar,
    spinbox,
    progress,
};

enum class RangeResult : std::uint8_t {
    ignored,
    handled,
    changed,
};

struct RangeConfig {
    double minimum{0.0};
    double maximum{100.0};
    double step{1.0};
    double value{0.0};
    double page_step{10.0};
    RangeKind kind{RangeKind::slider};
};

struct RangeTransition {
    RangeResult result{RangeResult::ignored};
    double value{0.0};
};

struct RangeState {
    double minimum{0.0};
    double maximum{100.0};
    double step{1.0};
    double value{0.0};
    double page_step{10.0};
    RangeKind kind{RangeKind::slider};
    bool disabled{};
};

class RangeController {
  public:
    RangeState state() const noexcept;
    double value() const noexcept;
    double minimum() const noexcept;
    double maximum() const noexcept;
    double step() const noexcept;
    double page_step() const noexcept;
    RangeKind kind() const noexcept;
    bool disabled() const noexcept;

    bool configure(const RangeConfig& config) noexcept;
    bool set_disabled(bool disabled) noexcept;

    double fraction() const noexcept;

    bool write_semantics(
        std::string_view accessible_name,
        std::string_view value_text,
        SemanticNodeData& output
    ) const noexcept;

    RangeTransition set_value(double value) noexcept;
    RangeTransition set_fraction(double fraction) noexcept;
    RangeTransition increment() noexcept;
    RangeTransition decrement() noexcept;
    RangeTransition page_increment() noexcept;
    RangeTransition page_decrement() noexcept;
    RangeTransition set_to_min() noexcept;
    RangeTransition set_to_max() noexcept;

  private:
    static bool valid_domain(const RangeConfig& config) noexcept;
    double snap(double clamped) const noexcept;
    RangeTransition commit(double raw) noexcept;

    double minimum_{0.0};
    double maximum_{100.0};
    double step_{1.0};
    double value_{0.0};
    double page_step_{10.0};
    RangeKind kind_{RangeKind::slider};
    bool disabled_{};
};

enum class RangeThumb : std::uint8_t {
    low,
    high,
};

struct RangeSliderConfig {
    double minimum{0.0};
    double maximum{100.0};
    double step{1.0};
    double low{0.0};
    double high{100.0};
    double page_step{10.0};
};

struct RangeSliderState {
    double minimum{0.0};
    double maximum{100.0};
    double step{1.0};
    double low{0.0};
    double high{100.0};
    double page_step{10.0};
    bool disabled{};
};

struct RangeSliderTransition {
    RangeResult result{RangeResult::ignored};
    double low{0.0};
    double high{0.0};
};

class RangeSliderController {
  public:
    RangeSliderState state() const noexcept;
    double low() const noexcept;
    double high() const noexcept;
    double minimum() const noexcept;
    double maximum() const noexcept;
    double step() const noexcept;
    double page_step() const noexcept;
    bool disabled() const noexcept;

    bool configure(const RangeSliderConfig& config) noexcept;
    bool set_disabled(bool disabled) noexcept;

    double fraction(RangeThumb thumb) const noexcept;

    bool write_semantics(
        RangeThumb thumb,
        std::string_view accessible_name,
        std::string_view value_text,
        SemanticNodeData& output
    ) const noexcept;

    RangeSliderTransition set_low(double value) noexcept;
    RangeSliderTransition set_high(double value) noexcept;
    RangeSliderTransition set_value(RangeThumb thumb, double value) noexcept;
    RangeSliderTransition set_fraction(RangeThumb thumb, double fraction) noexcept;
    RangeSliderTransition increment(RangeThumb thumb) noexcept;
    RangeSliderTransition decrement(RangeThumb thumb) noexcept;
    RangeSliderTransition page_increment(RangeThumb thumb) noexcept;
    RangeSliderTransition page_decrement(RangeThumb thumb) noexcept;
    RangeSliderTransition set_to_min(RangeThumb thumb) noexcept;
    RangeSliderTransition set_to_max(RangeThumb thumb) noexcept;

  private:
    static bool valid_domain(const RangeSliderConfig& config) noexcept;
    double snap(double clamped) const noexcept;
    double thumb_value(RangeThumb thumb) const noexcept;
    double fraction_at(double value) const noexcept;
    RangeSliderTransition commit(RangeThumb thumb, double raw) noexcept;
    RangeSliderTransition report(RangeResult result) const noexcept;

    double minimum_{0.0};
    double maximum_{100.0};
    double step_{1.0};
    double low_{0.0};
    double high_{100.0};
    double page_step_{10.0};
    bool disabled_{};
};

}

#endif
