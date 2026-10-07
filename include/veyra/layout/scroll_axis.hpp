#ifndef VEYRA_LAYOUT_SCROLL_AXIS_HPP
#define VEYRA_LAYOUT_SCROLL_AXIS_HPP

namespace veyra {

class ScrollAxis {
  public:
    bool set_extents(double content_extent, double viewport_extent) noexcept;
    bool set_offset(double offset) noexcept;
    bool scroll_by(double delta, double& unconsumed_delta) noexcept;

    double content_extent() const noexcept;
    double viewport_extent() const noexcept;
    double offset() const noexcept;
    double maximum_offset() const noexcept;

  private:
    double content_extent_{};
    double viewport_extent_{};
    double offset_{};
};

}

#endif
