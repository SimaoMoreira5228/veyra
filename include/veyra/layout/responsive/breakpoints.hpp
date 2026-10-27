#ifndef VEYRA_LAYOUT_BREAKPOINTS_HPP
#define VEYRA_LAYOUT_BREAKPOINTS_HPP

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace veyra {

struct BreakpointUpdate {
    bool accepted{};
    std::size_t variant{};
    bool changed{};
    bool oscillating{};
};

class BreakpointController {
  public:
    static constexpr std::size_t kMaxThresholds = 8;
    static constexpr std::uint32_t kFlipBound = 3;
    static constexpr std::uint32_t kReleaseSteady = 2;

    BreakpointController() noexcept = default;

    bool configure(std::span<const float> thresholds) noexcept;
    BreakpointUpdate update(float width) noexcept;

    std::size_t active_variant() const noexcept;
    std::size_t band_count() const noexcept;
    std::size_t threshold_count() const noexcept;
    float threshold_at(std::size_t index) const noexcept;
    bool holding() const noexcept;

  private:
    std::size_t resolve(float width) const noexcept;

    std::array<float, kMaxThresholds> thresholds_{};
    std::size_t count_{};
    std::size_t active_{};
    std::size_t previous_{};
    std::uint32_t alternations_{};
    std::uint32_t steady_{};
    bool holding_{};
};

}

#endif
