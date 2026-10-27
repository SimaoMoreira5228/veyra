#ifndef VEYRA_BEHAVIORS_DRAG_DROP_HPP
#define VEYRA_BEHAVIORS_DRAG_DROP_HPP

#include "veyra/core/semantics.hpp"
#include "veyra/layout/core/geometry.hpp"

#include <cstdint>

namespace veyra {

enum class DragAction : std::uint8_t {
    none = 0,
    move = 1U << 0U,
    copy = 1U << 1U,
    link = 1U << 2U,
};

constexpr DragAction operator|(DragAction left, DragAction right) noexcept {
    return static_cast<DragAction>(
        static_cast<std::uint8_t>(left) | static_cast<std::uint8_t>(right)
    );
}

constexpr DragAction operator&(DragAction left, DragAction right) noexcept {
    return static_cast<DragAction>(
        static_cast<std::uint8_t>(left) & static_cast<std::uint8_t>(right)
    );
}

constexpr bool has_drag_action(DragAction set, DragAction flag) noexcept {
    return (set & flag) == flag;
}

enum class DragPhase : std::uint8_t {
    idle,
    pending,
    dragging,
};

enum class DragResult : std::uint8_t {
    ignored,
    handled,
    armed,
    moved,
    entered,
    left,
    dropped,
    rejected,
    cancelled,
};

struct DragFeedback {
    DragResult result{DragResult::ignored};
    bool accepts{};
    DragAction negotiated{DragAction::none};
};

struct DragDrop {
    DragResult result{DragResult::ignored};
    std::uint64_t source{};
    std::uint64_t target{};
    DragAction action{DragAction::none};
};

struct DragState {
    DragPhase phase{DragPhase::idle};
    std::uint64_t source{};
    DragAction offered{DragAction::none};
    std::uint64_t pointer{};
    bool over_target{};
    std::uint64_t target{};
    DragAction accepted{DragAction::none};
    DragAction negotiated{DragAction::none};
};

class DragDropController {
  public:
    DragState state() const noexcept;
    bool idle() const noexcept;
    bool dragging() const noexcept;
    bool over_target() const noexcept;

    DragResult begin(
        std::uint64_t source,
        DragAction offered,
        std::uint64_t pointer,
        LayoutPoint at,
        float threshold
    ) noexcept;

    DragResult begin_keyboard(std::uint64_t source, DragAction offered) noexcept;
    DragResult update(std::uint64_t pointer, LayoutPoint at) noexcept;

    DragFeedback enter_target(std::uint64_t target, DragAction accept) noexcept;
    DragFeedback move_target(std::uint64_t target, DragAction accept) noexcept;
    DragResult leave_target(std::uint64_t target) noexcept;

    DragDrop drop() noexcept;
    DragResult cancel(std::uint64_t pointer) noexcept;
    DragResult cancel_target(std::uint64_t target) noexcept;
    DragResult abort() noexcept;

  private:
    static DragAction negotiate(DragAction offered, DragAction accepted) noexcept;
    void reset() noexcept;

    DragPhase phase_{DragPhase::idle};
    std::uint64_t source_{};
    DragAction offered_{DragAction::none};
    std::uint64_t pointer_{};
    LayoutPoint origin_{};
    LayoutPoint position_{};
    float threshold_{};
    bool over_target_{};
    std::uint64_t target_{};
    DragAction accepted_{DragAction::none};
};

}

#endif
