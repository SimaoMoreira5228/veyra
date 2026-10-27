#ifndef VEYRA_LAYOUT_VIEWPORT_HPP
#define VEYRA_LAYOUT_VIEWPORT_HPP

#include "veyra/layout/core/measure.hpp"

#include <cstdint>

namespace veyra {

struct ViewportInvalidation {
    std::uint8_t bits{};

    static constexpr std::uint8_t kNone = 0;
    static constexpr std::uint8_t kLayout = 1 << 0;
    static constexpr std::uint8_t kText = 1 << 1;
    static constexpr std::uint8_t kImages = 1 << 2;
    static constexpr std::uint8_t kDamage = 1 << 3;

    bool empty() const noexcept {
        return bits == kNone;
    }

    bool has(std::uint8_t bit) const noexcept {
        return (bits & bit) != 0;
    }
};

struct ViewportUpdate {
    bool accepted{};
    ViewportInvalidation invalidation;
};

class Viewport {
  public:
    Viewport() noexcept = default;

    ViewportUpdate set_size(float width, float height) noexcept;
    ViewportUpdate set_scale(float scale) noexcept;

    float width() const noexcept;
    float height() const noexcept;
    float scale() const noexcept;
    bool has_area() const noexcept;
    std::uint64_t layout_generation() const noexcept;
    std::uint64_t scale_generation() const noexcept;
    LayoutConstraints root_constraints() const noexcept;

  private:
    float width_{};
    float height_{};
    float scale_{1.0F};
    std::uint64_t layout_generation_{1};
    std::uint64_t scale_generation_{1};
};

}

#endif
