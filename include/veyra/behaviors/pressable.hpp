#ifndef VEYRA_BEHAVIORS_PRESSABLE_HPP
#define VEYRA_BEHAVIORS_PRESSABLE_HPP

#include "veyra/core/semantics.hpp"

#include <cstdint>
#include <string_view>

namespace veyra {

enum class PressableKey : std::uint8_t {
    space,
    enter,
};

enum class PressableResult : std::uint8_t {
    ignored,
    handled,
    activated,
};

struct PressableState {
    bool hovered{};
    bool pressed{};
    bool focused{};
    bool disabled{};
};

class PressableController {
  public:
    PressableState state() const noexcept;
    bool set_disabled(bool disabled) noexcept;
    PressableResult set_focused(bool focused) noexcept;

    PressableResult pointer_enter() noexcept;
    PressableResult pointer_leave() noexcept;
    PressableResult pointer_down(std::uint64_t pointer_id, bool primary) noexcept;
    PressableResult pointer_up(std::uint64_t pointer_id, bool primary, bool inside) noexcept;
    PressableResult pointer_cancel(std::uint64_t pointer_id) noexcept;

    PressableResult key_down(PressableKey key) noexcept;
    PressableResult key_up(PressableKey key) noexcept;
    PressableResult invoke() const noexcept;

    bool write_semantics(std::string_view accessible_name, SemanticNodeData& output) const noexcept;

  private:
    bool hovered_{};
    bool pointer_pressed_{};
    std::uint64_t active_pointer_id_{};
    bool focused_{};
    bool disabled_{};
    bool space_armed_{};
    bool enter_armed_{};
};

}

#endif
