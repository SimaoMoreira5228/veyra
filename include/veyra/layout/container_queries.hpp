#ifndef VEYRA_LAYOUT_CONTAINER_QUERIES_HPP
#define VEYRA_LAYOUT_CONTAINER_QUERIES_HPP

#include "veyra/layout/breakpoints.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace veyra {

struct ContainerVariant {
    bool known{};
    std::size_t variant{};
    bool holding{};
};

class ContainerQuerySet {
  public:
    static constexpr std::size_t kMaxContainers = 32;

    ContainerQuerySet() noexcept = default;

    bool configure(std::span<const float> thresholds) noexcept;
    bool attach(std::uint64_t id) noexcept;
    bool detach(std::uint64_t id) noexcept;
    BreakpointUpdate update(std::uint64_t id, float width) noexcept;

    ContainerVariant variant_of(std::uint64_t id) const noexcept;
    std::size_t container_count() const noexcept;
    std::size_t band_count() const noexcept;
    std::size_t threshold_count() const noexcept;
    float threshold_at(std::size_t index) const noexcept;

  private:
    struct Slot {
        std::uint64_t id{};
        BreakpointController controller;
    };

    std::size_t find(std::uint64_t id) const noexcept;

    std::array<Slot, kMaxContainers> slots_{};
    std::array<float, BreakpointController::kMaxThresholds> thresholds_{};
    std::size_t threshold_count_{};
    std::size_t count_{};
};

}

#endif
