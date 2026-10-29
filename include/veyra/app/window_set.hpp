#ifndef VEYRA_APP_WINDOW_SET_HPP
#define VEYRA_APP_WINDOW_SET_HPP

#include "veyra/core/tree/node_id_pool.hpp"
#include "veyra/platform/core/window.hpp"
#include "veyra/style/theme/theme.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

namespace veyra {

class NodeTree;

enum class ShutdownMode : std::uint8_t {
    on_last_window_close,
    on_main_window_close,
    on_explicit_shutdown,
};

enum class CloseReason : std::uint8_t {
    user,
    owner,
    application,
    shutdown,
};

enum class CloseDecision : std::uint8_t {
    allow,
    cancel,
};

enum class WindowSetResult : std::uint8_t {
    ok,
    invalid_window,
    invalid_owner,
    owner_not_visible,
    already_shown,
    capacity,
    creation_failed,
    invalid_root,
    invalid_geometry,
    stale_frame,
    presentation_failed,
    cancelled,
    not_dialog,
    invalid_state,
};

enum class WindowSlotState : std::uint8_t {
    empty,
    open,
    closed,
};

struct WindowDesc {
    WindowId owner{kInvalidWindowId};
    bool dialog{};
    bool modal{};
};

struct WindowSlot {
    WindowId id{kInvalidWindowId};
    WindowId owner{kInvalidWindowId};
    WindowSlotState state{WindowSlotState::empty};
    bool dialog{};
    bool modal{};
    bool shown{};
    bool dialog_accepted{};
    bool dialog_ready{};
    std::uint64_t dialog_value{};
    NodeId focused_node{};
    NodeId accessibility_root{};
    NodeTree* semantics_tree{};
    WindowState window_state{WindowState::normal};
    float width{};
    float height{};
    float scale_x{1.0F};
    float scale_y{1.0F};
    std::uint64_t scale_generation{};
    Theme theme{};
};

struct WindowInfo {
    WindowId id{kInvalidWindowId};
    WindowId owner{kInvalidWindowId};
    bool dialog{};
    bool modal{};
    bool shown{};
    std::size_t child_count{};
    NodeId focused_node{};
    NodeId accessibility_root{};
    WindowState state{WindowState::normal};
    float width{};
    float height{};
    float scale_x{1.0F};
    float scale_y{1.0F};
    std::uint64_t scale_generation{};
};

struct DialogOutcome {
    bool accepted{};
    std::uint64_t value{};
};

struct CloseRequest {
    WindowId window{kInvalidWindowId};
    CloseReason reason{CloseReason::user};
};

using CloseHandler = CloseDecision (*)(void* user, const CloseRequest& request);

class WindowSet {
  public:
    WindowSet(
        std::span<WindowSlot> storage,
        ShutdownMode shutdown = ShutdownMode::on_last_window_close,
        CloseHandler close_handler = nullptr,
        void* close_user = nullptr
    ) noexcept;

    WindowSetResult create(const WindowDesc& desc, WindowId& out_id) noexcept;
    WindowSetResult show(WindowId window) noexcept;
    WindowSetResult request_close(WindowId window, CloseReason reason) noexcept;
    WindowSetResult close_with_result(WindowId window, bool accepted, std::uint64_t value) noexcept;
    WindowSetResult destroy(WindowId window) noexcept;
    WindowSetResult set_main_window(WindowId window) noexcept;
    WindowSetResult take_dialog_outcome(WindowId window, DialogOutcome& out) noexcept;
    WindowSetResult
    set_geometry(WindowId window, float width, float height, float scale_x, float scale_y) noexcept;
    WindowSetResult set_state(WindowId window, WindowState state) noexcept;
    WindowSetResult bind_semantics(WindowId window, NodeTree& tree, NodeId root) noexcept;
    bool set_focused_node(WindowId window, NodeId node) noexcept;
    NodeId focused_node(WindowId window) const noexcept;
    NodeId accessibility_root(WindowId window) const noexcept;
    Theme* theme(WindowId window) noexcept;
    const Theme* theme(WindowId window) const noexcept;

    bool info(WindowId window, WindowInfo& out) const noexcept;
    bool is_open(WindowId window) const noexcept;
    bool blocks_interaction(WindowId window) const noexcept;
    bool should_shutdown() const noexcept;

    WindowId main_window() const noexcept;
    ShutdownMode shutdown_mode() const noexcept;
    std::size_t open_count() const noexcept;
    std::size_t open_windows(std::span<WindowId> output) const noexcept;
    std::size_t capacity() const noexcept;

  private:
    WindowSlot* find_slot(WindowId window) noexcept;
    const WindowSlot* find_slot(WindowId window) const noexcept;
    WindowSlot* find_reusable() noexcept;
    bool approve_subtree(WindowSlot& slot, CloseReason reason) noexcept;
    void close_children(WindowSlot& slot) noexcept;
    void finish_close(WindowSlot& slot, bool accepted, std::uint64_t value) noexcept;
    std::size_t child_count(WindowId window) const noexcept;

    std::span<WindowSlot> storage_;
    CloseHandler close_handler_{};
    void* close_user_{};
    WindowId next_id_{1};
    WindowId main_window_{kInvalidWindowId};
    ShutdownMode shutdown_{ShutdownMode::on_last_window_close};
};

}

namespace veyra::app {
using ::veyra::CloseDecision;
using ::veyra::CloseHandler;
using ::veyra::CloseReason;
using ::veyra::DialogOutcome;
using ::veyra::ShutdownMode;
using ::veyra::WindowDesc;
using ::veyra::WindowInfo;
using ::veyra::WindowSet;
using ::veyra::WindowSetResult;
using ::veyra::WindowSlot;
using ::veyra::WindowSlotState;
using ::veyra::WindowState;
}

#endif
