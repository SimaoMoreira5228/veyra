#ifndef VEYRA_BEHAVIORS_TOGGLE_HPP
#define VEYRA_BEHAVIORS_TOGGLE_HPP

#include "veyra/core/semantics/semantics.hpp"

#include <cstdint>
#include <string_view>

namespace veyra {

enum class ToggleValue : std::uint8_t {
    unchecked,
    checked,
    mixed,
};

enum class ToggleKind : std::uint8_t {
    checkbox,
    toggle_switch,
};

enum class ToggleMode : std::uint8_t {
    uncontrolled,
    controlled,
};

enum class ToggleKey : std::uint8_t {
    space,
    enter,
};

enum class ToggleResult : std::uint8_t {
    ignored,
    handled,
    changed,
};

struct ToggleConfig {
    ToggleKind kind{ToggleKind::checkbox};
    ToggleMode mode{ToggleMode::uncontrolled};
    ToggleValue value{ToggleValue::unchecked};
    bool allow_mixed{};
};

struct ToggleTransition {
    ToggleResult result{ToggleResult::ignored};
    ToggleValue value{ToggleValue::unchecked};
};

struct ToggleState {
    ToggleKind kind{ToggleKind::checkbox};
    ToggleMode mode{ToggleMode::uncontrolled};
    ToggleValue value{ToggleValue::unchecked};
    bool hovered{};
    bool pressed{};
    bool focused{};
    bool disabled{};
};

class ToggleController {
  public:
    ToggleState state() const noexcept;
    ToggleValue value() const noexcept;
    ToggleKind kind() const noexcept;
    ToggleMode mode() const noexcept;
    bool mixed_allowed() const noexcept;

    bool configure(const ToggleConfig& config) noexcept;
    bool migrate_mode(ToggleMode mode, ToggleValue value) noexcept;
    bool set_kind(ToggleKind kind) noexcept;
    bool set_allow_mixed(bool allow) noexcept;
    bool set_disabled(bool disabled) noexcept;
    ToggleResult set_focused(bool focused) noexcept;

    ToggleTransition set_controlled_value(ToggleValue value) noexcept;
    ToggleTransition request_value(ToggleValue value) noexcept;

    ToggleResult pointer_enter() noexcept;
    ToggleResult pointer_leave() noexcept;
    ToggleResult pointer_down(std::uint64_t pointer_id, bool primary) noexcept;
    ToggleTransition pointer_up(std::uint64_t pointer_id, bool primary, bool inside) noexcept;
    ToggleResult pointer_cancel(std::uint64_t pointer_id) noexcept;

    ToggleTransition key_down(ToggleKey key) noexcept;
    ToggleTransition key_up(ToggleKey key) noexcept;
    ToggleTransition invoke() noexcept;

    bool write_semantics(std::string_view accessible_name, SemanticNodeData& output) const noexcept;

  private:
    ToggleValue normalize(ToggleValue value) const noexcept;
    ToggleValue next_value() const noexcept;
    ToggleTransition commit_or_emit(ToggleValue value) noexcept;
    void clear_armed() noexcept;

    ToggleKind kind_{ToggleKind::checkbox};
    ToggleMode mode_{ToggleMode::uncontrolled};
    ToggleValue value_{ToggleValue::unchecked};
    bool allow_mixed_{};
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
