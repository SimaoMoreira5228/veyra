#ifndef VEYRA_LAYOUT_SCROLL_AXIS_HPP
#define VEYRA_LAYOUT_SCROLL_AXIS_HPP

#include <cstdint>

namespace veyra {

// NOTE: scroll generations count committed geometry changes, not frames.
constexpr bool same_scroll_generation(std::uint64_t current, std::uint64_t retained) noexcept {
    return current == retained;
}

class ScrollAxis {
  public:
    bool set_extents(double content_extent, double viewport_extent) noexcept;
    bool set_offset(double offset) noexcept;
    bool scroll_by(double delta, double& unconsumed_delta) noexcept;
    void set_generation(std::uint64_t generation) noexcept;

    double content_extent() const noexcept;
    double viewport_extent() const noexcept;
    double offset() const noexcept;
    double maximum_offset() const noexcept;
    std::uint64_t generation() const noexcept;

  private:
    double content_extent_{};
    double viewport_extent_{};
    double offset_{};
    std::uint64_t generation_{};
};

}

namespace veyra::layout {
using ::veyra::ScrollAxis;
using ::veyra::same_scroll_generation;
}

namespace veyra::layout::scroll {
using ::veyra::ScrollAxis;
using ::veyra::same_scroll_generation;
}

#endif
