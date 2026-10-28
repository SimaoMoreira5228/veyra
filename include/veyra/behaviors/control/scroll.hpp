#ifndef VEYRA_BEHAVIORS_SCROLL_HPP
#define VEYRA_BEHAVIORS_SCROLL_HPP

#include "veyra/layout/scroll/scroll_axis.hpp"

#include <cstdint>

namespace veyra {

enum class ScrollResult : std::uint8_t {
    ignored,
    handled,
    changed,
};

struct ScrollState {
    double content_extent{};
    double viewport_extent{};
    double offset{};
    bool disabled{};
    std::uint64_t generation{};
};

struct ScrollMove {
    ScrollResult result{ScrollResult::ignored};
    double overshoot{};
};

class ScrollController {
  public:
    ScrollController() noexcept;

    ScrollState state() const noexcept;
    double content_extent() const noexcept;
    double viewport_extent() const noexcept;
    double offset() const noexcept;
    double maximum_offset() const noexcept;
    bool disabled() const noexcept;
    bool at_start() const noexcept;
    bool at_end() const noexcept;
    std::uint64_t generation() const noexcept;

    bool configure(double content_extent, double viewport_extent) noexcept;
    bool set_disabled(bool disabled) noexcept;

    ScrollResult set_offset(double offset) noexcept;
    ScrollMove scroll_to(double offset) noexcept;
    ScrollMove scroll_by(double delta) noexcept;
    ScrollMove scroll_lines(double lines, double line_height) noexcept;
    ScrollMove scroll_pages(double pages, double page_size) noexcept;

  private:
    ScrollMove commit(double target) noexcept;

    ScrollAxis axis_{};
    bool disabled_{};
};

}

namespace veyra::behaviors {
using ::veyra::ScrollController;
using ::veyra::ScrollMove;
using ::veyra::ScrollResult;
using ::veyra::ScrollState;
}

namespace veyra::behaviors::control {
using ::veyra::ScrollController;
using ::veyra::ScrollMove;
using ::veyra::ScrollResult;
using ::veyra::ScrollState;
}

#endif
