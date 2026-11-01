#ifndef VEYRA_PLATFORM_SEAT_HPP
#define VEYRA_PLATFORM_SEAT_HPP

#include "veyra/core/tree/node_id_pool.hpp"
#include "veyra/platform/core/window.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace veyra {

using SeatId = std::uint32_t;

inline constexpr SeatId kInvalidSeatId = 0;
inline constexpr std::size_t kSeatCapacity = 4;
inline constexpr std::size_t kSeatPointerCapacity = 4;

enum class SeatResult : std::uint8_t {
    ok,
    invalid_seat,
    invalid_target,
    capacity,
};

struct SeatInputState {
    SeatId id() const noexcept;
    WindowId focused_window() const noexcept;
    NodeId focused_node() const noexcept;
    std::uint32_t modifiers() const noexcept;
    std::size_t capture_count() const noexcept;
    std::uint32_t captured_pointer(std::size_t index) const noexcept;
    NodeId capture_target(std::size_t index) const noexcept;
    bool pointer_captured(std::uint32_t pointer) const noexcept;
    NodeId capture_target_for(std::uint32_t pointer) const noexcept;
    NodeId hovered_node() const noexcept;

  private:
    friend class SeatInputSet;

    struct Capture {
        std::uint32_t pointer{};
        NodeId target{};
    };

    SeatId id_{kInvalidSeatId};
    WindowId focused_window_{kInvalidWindowId};
    NodeId focused_node_{};
    std::uint32_t modifiers_{};
    std::array<Capture, kSeatPointerCapacity> captures_{};
    std::size_t capture_count_{};
    NodeId hovered_node_{};
};

class SeatInputSet {
  public:
    explicit SeatInputSet(std::span<SeatInputState> storage) noexcept;

    SeatResult create(SeatId& out_seat) noexcept;
    bool release(SeatId seat) noexcept;

    SeatInputState* find(SeatId seat) noexcept;
    const SeatInputState* find(SeatId seat) const noexcept;

    std::size_t size() const noexcept;
    std::size_t capacity() const noexcept;

    bool focus(SeatId seat, WindowId window, NodeId node) noexcept;
    bool set_modifiers(SeatId seat, std::uint32_t modifiers) noexcept;
    SeatResult capture_pointer(SeatId seat, std::uint32_t pointer, NodeId target) noexcept;
    bool release_pointer(SeatId seat, std::uint32_t pointer) noexcept;
    bool set_hover(SeatId seat, NodeId node) noexcept;
    bool clear_hover(SeatId seat) noexcept;
    bool clear_pointer_state(SeatId seat) noexcept;

  private:
    SeatInputState* find_mutable(SeatId seat) noexcept;
    std::size_t capture_index(const SeatInputState& state, std::uint32_t pointer) const noexcept;

    std::span<SeatInputState> storage_;
    std::size_t size_{};
    SeatId next_id_{1};
};

}

namespace veyra::platform {
using ::veyra::kInvalidSeatId;
using ::veyra::kSeatCapacity;
using ::veyra::kSeatPointerCapacity;
using ::veyra::SeatId;
using ::veyra::SeatInputSet;
using ::veyra::SeatInputState;
using ::veyra::SeatResult;
}

namespace veyra::platform::core {
using ::veyra::kInvalidSeatId;
using ::veyra::kSeatCapacity;
using ::veyra::kSeatPointerCapacity;
using ::veyra::SeatId;
using ::veyra::SeatInputSet;
using ::veyra::SeatInputState;
using ::veyra::SeatResult;
}

#endif
