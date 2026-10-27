#ifndef VEYRA_BEHAVIORS_PRESSABLE_HPP
#define VEYRA_BEHAVIORS_PRESSABLE_HPP

#include "veyra/core/semantics/semantics.hpp"
#include "veyra/media/animation/animation_interpolation.hpp"

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

struct PressableTint {
    float hover{};
    float press{};
};

class PressableTintAnimator {
  public:
    PressableTintAnimator() noexcept;

    bool tick(PressableState state, std::uint64_t now_ms) noexcept;
    PressableTint tint() const noexcept;
    bool running() const noexcept;

    void set_paused(bool paused) noexcept;
    void set_throttled(bool throttled) noexcept;
    void set_reduced_motion(bool reduced_motion) noexcept;

    static constexpr AnimatedProperty animated_property() noexcept {
        return AnimatedProperty::color;
    }

  private:
    ScalarAnimator hover_;
    ScalarAnimator press_;
};

}

#endif
