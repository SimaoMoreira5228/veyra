#include "veyra/app/window_set.hpp"

#include "veyra/core/tree/node_tree.hpp"

#include <cmath>

namespace veyra {
namespace {

bool in_scope(const NodeTree& tree, NodeId root, NodeId node) noexcept {
    if (!tree.contains(node)) {
        return false;
    }

    for (auto current = node; !current.is_null(); current = tree.parent_of(current)) {
        if (current == root) {
            return true;
        }
    }

    return false;
}

}

WindowSet::WindowSet(
    std::span<WindowSlot> storage,
    ShutdownMode shutdown,
    CloseHandler close_handler,
    void* close_user
) noexcept :
    storage_(storage), close_handler_(close_handler), close_user_(close_user), shutdown_(shutdown) {
}

WindowSetResult WindowSet::create(const WindowDesc& desc, WindowId& out_id) noexcept {
    if (desc.modal && !desc.dialog) {
        return WindowSetResult::invalid_state;
    }
    if (desc.modal && desc.owner == kInvalidWindowId) {
        return WindowSetResult::invalid_owner;
    }
    if (desc.owner != kInvalidWindowId) {
        const auto* owner = find_slot(desc.owner);
        if (owner == nullptr || owner->state != WindowSlotState::open) {
            return WindowSetResult::invalid_owner;
        }
    }

    auto* slot = find_reusable();
    if (slot == nullptr) {
        return WindowSetResult::capacity;
    }

    *slot = {};
    slot->id = next_id_++;
    if (next_id_ == 0) {
        next_id_ = 1;
    }
    slot->owner = desc.owner;
    slot->state = WindowSlotState::open;
    slot->dialog = desc.dialog;
    slot->modal = desc.modal;
    out_id = slot->id;
    return WindowSetResult::ok;
}

WindowSetResult WindowSet::show(WindowId window) noexcept {
    auto* slot = find_slot(window);
    if (slot == nullptr || slot->state != WindowSlotState::open) {
        return WindowSetResult::invalid_window;
    }
    if (slot->shown) {
        return WindowSetResult::already_shown;
    }
    if (slot->owner != kInvalidWindowId) {
        const auto* owner = find_slot(slot->owner);
        if (owner == nullptr || owner->state != WindowSlotState::open || !owner->shown) {
            return WindowSetResult::owner_not_visible;
        }
    }

    slot->shown = true;
    return WindowSetResult::ok;
}

WindowSetResult WindowSet::request_close(WindowId window, CloseReason reason) noexcept {
    auto* slot = find_slot(window);
    if (slot == nullptr || slot->state != WindowSlotState::open) {
        return WindowSetResult::invalid_window;
    }
    if (!approve_subtree(*slot, reason)) {
        return WindowSetResult::cancelled;
    }

    close_children(*slot);
    finish_close(*slot, false, 0);
    return WindowSetResult::ok;
}

WindowSetResult
WindowSet::close_with_result(WindowId window, bool accepted, std::uint64_t value) noexcept {
    auto* slot = find_slot(window);
    if (slot == nullptr || slot->state != WindowSlotState::open) {
        return WindowSetResult::invalid_window;
    }
    if (!slot->dialog) {
        return WindowSetResult::not_dialog;
    }
    if (!approve_subtree(*slot, CloseReason::application)) {
        return WindowSetResult::cancelled;
    }

    close_children(*slot);
    finish_close(*slot, accepted, value);
    return WindowSetResult::ok;
}

WindowSetResult WindowSet::destroy(WindowId window) noexcept {
    auto* slot = find_slot(window);
    if (slot == nullptr || slot->state != WindowSlotState::open) {
        return WindowSetResult::invalid_window;
    }

    close_children(*slot);
    finish_close(*slot, false, 0);
    return WindowSetResult::ok;
}

WindowSetResult WindowSet::set_main_window(WindowId window) noexcept {
    const auto* slot = find_slot(window);
    if (slot == nullptr || slot->state != WindowSlotState::open) {
        return WindowSetResult::invalid_window;
    }

    main_window_ = window;
    return WindowSetResult::ok;
}

WindowSetResult WindowSet::take_dialog_outcome(WindowId window, DialogOutcome& out) noexcept {
    auto* slot = find_slot(window);
    if (slot == nullptr || slot->state != WindowSlotState::closed || !slot->dialog_ready) {
        return WindowSetResult::invalid_state;
    }

    out = {
        .accepted = slot->dialog_accepted,
        .value = slot->dialog_value,
    };
    *slot = {};
    return WindowSetResult::ok;
}

bool WindowSet::info(WindowId window, WindowInfo& out) const noexcept {
    const auto* slot = find_slot(window);
    if (slot == nullptr || slot->state != WindowSlotState::open) {
        return false;
    }

    out = {
        .id = slot->id,
        .owner = slot->owner,
        .dialog = slot->dialog,
        .modal = slot->modal,
        .shown = slot->shown,
        .child_count = child_count(window),
        .focused_node = slot->focused_node,
        .accessibility_root = slot->accessibility_root,
        .state = slot->window_state,
        .width = slot->width,
        .height = slot->height,
        .scale_x = slot->scale_x,
        .scale_y = slot->scale_y,
        .scale_generation = slot->scale_generation,
    };
    return true;
}

WindowSetResult WindowSet::set_geometry(
    WindowId window,
    float width,
    float height,
    float scale_x,
    float scale_y
) noexcept {
    auto* slot = find_slot(window);
    if (slot == nullptr || slot->state != WindowSlotState::open) {
        return WindowSetResult::invalid_window;
    }
    if (!std::isfinite(width) || !std::isfinite(height) || !std::isfinite(scale_x) ||
        !std::isfinite(scale_y) || width <= 0.0F || height <= 0.0F || scale_x <= 0.0F ||
        scale_y <= 0.0F) {
        return WindowSetResult::invalid_geometry;
    }

    if (slot->scale_generation == 0) {
        slot->scale_generation = 1;
    } else if (slot->scale_x != scale_x || slot->scale_y != scale_y) {
        ++slot->scale_generation;
    }
    slot->width = width;
    slot->height = height;
    slot->scale_x = scale_x;
    slot->scale_y = scale_y;
    return WindowSetResult::ok;
}

WindowSetResult WindowSet::set_state(WindowId window, WindowState state) noexcept {
    auto* slot = find_slot(window);
    if (slot == nullptr || slot->state != WindowSlotState::open) {
        return WindowSetResult::invalid_window;
    }
    if (!valid_window_state(state)) {
        return WindowSetResult::invalid_state;
    }

    slot->window_state = state;
    return WindowSetResult::ok;
}

WindowSetResult WindowSet::bind_semantics(WindowId window, NodeTree& tree, NodeId root) noexcept {
    auto* slot = find_slot(window);
    if (slot == nullptr || slot->state != WindowSlotState::open) {
        return WindowSetResult::invalid_window;
    }
    if (!tree.contains(root)) {
        return WindowSetResult::invalid_root;
    }

    slot->semantics_tree = &tree;
    slot->accessibility_root = root;
    if (!slot->focused_node.is_null() && !in_scope(tree, root, slot->focused_node)) {
        slot->focused_node = {};
    }
    return WindowSetResult::ok;
}

bool WindowSet::set_focused_node(WindowId window, NodeId node) noexcept {
    auto* slot = find_slot(window);
    if (slot == nullptr || slot->state != WindowSlotState::open ||
        slot->semantics_tree == nullptr) {
        return false;
    }

    if (node.is_null()) {
        slot->focused_node = {};
        return true;
    }
    if (!in_scope(*slot->semantics_tree, slot->accessibility_root, node)) {
        return false;
    }

    slot->focused_node = node;
    return true;
}

bool WindowSet::register_focus(WindowId window, std::uint64_t id, bool enabled) noexcept {
    auto* focus = find_focus(window);
    return focus != nullptr && focus->register_item(id, enabled);
}

bool WindowSet::unregister_focus(WindowId window, std::uint64_t id) noexcept {
    auto* focus = find_focus(window);
    return focus != nullptr && focus->unregister(id);
}

bool WindowSet::focus_control(WindowId window, std::uint64_t id) noexcept {
    auto* focus = find_focus(window);
    return focus != nullptr && focus->focus(id);
}

bool WindowSet::focus_next(WindowId window) noexcept {
    auto* focus = find_focus(window);
    return focus != nullptr && focus->focus_next();
}

bool WindowSet::focus_prev(WindowId window) noexcept {
    auto* focus = find_focus(window);
    return focus != nullptr && focus->focus_prev();
}

std::uint64_t WindowSet::focused_control(WindowId window) const noexcept {
    const auto* focus = find_focus(window);
    return focus != nullptr ? focus->focused() : 0;
}

std::size_t WindowSet::focus_count(WindowId window) const noexcept {
    const auto* focus = find_focus(window);
    return focus != nullptr ? focus->count() : 0;
}

HitTestResult WindowSet::hit_test_logical(
    WindowId window,
    LayoutPoint point,
    std::span<const LayoutRect> bounds_by_node_index,
    NodeId& target
) const noexcept {
    target = {};
    const auto* slot = find_slot(window);
    if (slot == nullptr || slot->state != WindowSlotState::open ||
        slot->semantics_tree == nullptr) {
        return HitTestResult::invalid_root;
    }

    return hit_test(
        *slot->semantics_tree,
        slot->accessibility_root,
        point,
        bounds_by_node_index,
        target
    );
}

HitTestResult WindowSet::hit_test_physical(
    WindowId window,
    LayoutPoint point,
    std::span<const LayoutRect> bounds_by_node_index,
    NodeId& target
) const noexcept {
    target = {};
    const auto* slot = find_slot(window);
    if (slot == nullptr || slot->state != WindowSlotState::open ||
        slot->semantics_tree == nullptr) {
        return HitTestResult::invalid_root;
    }
    if (!std::isfinite(point.x) || !std::isfinite(point.y) || slot->scale_x <= 0.0F ||
        slot->scale_y <= 0.0F) {
        return HitTestResult::invalid_point;
    }

    const auto logical = LayoutPoint{
        .x = point.x / slot->scale_x,
        .y = point.y / slot->scale_y,
    };
    return hit_test(
        *slot->semantics_tree,
        slot->accessibility_root,
        logical,
        bounds_by_node_index,
        target
    );
}

NodeId WindowSet::accessibility_root(WindowId window) const noexcept {
    const auto* slot = find_slot(window);
    if (slot == nullptr || slot->state != WindowSlotState::open) {
        return {};
    }

    return slot->accessibility_root;
}

NodeId WindowSet::focused_node(WindowId window) const noexcept {
    const auto* slot = find_slot(window);
    if (slot == nullptr || slot->state != WindowSlotState::open) {
        return {};
    }

    return slot->focused_node;
}

Theme* WindowSet::theme(WindowId window) noexcept {
    auto* slot = find_slot(window);
    if (slot == nullptr || slot->state != WindowSlotState::open) {
        return nullptr;
    }

    return &slot->theme;
}

const Theme* WindowSet::theme(WindowId window) const noexcept {
    const auto* slot = find_slot(window);
    if (slot == nullptr || slot->state != WindowSlotState::open) {
        return nullptr;
    }

    return &slot->theme;
}

bool WindowSet::is_open(WindowId window) const noexcept {
    const auto* slot = find_slot(window);
    return slot != nullptr && slot->state == WindowSlotState::open;
}

bool WindowSet::blocks_interaction(WindowId window) const noexcept {
    if (!is_open(window)) {
        return false;
    }

    for (const auto& slot : storage_) {
        if (slot.state == WindowSlotState::open && slot.modal && slot.owner == window) {
            return true;
        }
    }

    return false;
}

bool WindowSet::should_shutdown() const noexcept {
    switch (shutdown_) {
    case ShutdownMode::on_last_window_close:
        return open_count() == 0;
    case ShutdownMode::on_main_window_close:
        return main_window_ != kInvalidWindowId && !is_open(main_window_);
    case ShutdownMode::on_explicit_shutdown:
        return false;
    }

    return false;
}

WindowId WindowSet::main_window() const noexcept {
    return main_window_;
}

ShutdownMode WindowSet::shutdown_mode() const noexcept {
    return shutdown_;
}

std::size_t WindowSet::open_count() const noexcept {
    auto count = std::size_t{0};

    for (const auto& slot : storage_) {
        if (slot.state == WindowSlotState::open) {
            ++count;
        }
    }

    return count;
}

std::size_t WindowSet::open_windows(std::span<WindowId> output) const noexcept {
    auto count = std::size_t{0};

    for (const auto& slot : storage_) {
        if (slot.state != WindowSlotState::open) {
            continue;
        }
        if (count < output.size()) {
            output[count] = slot.id;
        }
        ++count;
    }

    return count;
}

std::size_t WindowSet::capacity() const noexcept {
    return storage_.size();
}

WindowSlot* WindowSet::find_slot(WindowId window) noexcept {
    for (auto& slot : storage_) {
        if (slot.id == window && slot.state != WindowSlotState::empty) {
            return &slot;
        }
    }

    return nullptr;
}

const WindowSlot* WindowSet::find_slot(WindowId window) const noexcept {
    for (const auto& slot : storage_) {
        if (slot.id == window && slot.state != WindowSlotState::empty) {
            return &slot;
        }
    }

    return nullptr;
}

FocusChain* WindowSet::find_focus(WindowId window) noexcept {
    auto* slot = find_slot(window);
    return slot != nullptr && slot->state == WindowSlotState::open ? &slot->focus : nullptr;
}

const FocusChain* WindowSet::find_focus(WindowId window) const noexcept {
    const auto* slot = find_slot(window);
    return slot != nullptr && slot->state == WindowSlotState::open ? &slot->focus : nullptr;
}

WindowSlot* WindowSet::find_reusable() noexcept {
    for (auto& slot : storage_) {
        if (slot.state == WindowSlotState::empty) {
            return &slot;
        }
    }

    return nullptr;
}

bool WindowSet::approve_subtree(WindowSlot& slot, CloseReason reason) noexcept {
    for (auto& candidate : storage_) {
        if (candidate.state == WindowSlotState::open && candidate.owner == slot.id) {
            if (!approve_subtree(candidate, CloseReason::owner)) {
                return false;
            }
        }
    }

    if (close_handler_ == nullptr) {
        return true;
    }

    const CloseRequest request{
        .window = slot.id,
        .reason = reason,
    };
    return close_handler_(close_user_, request) == CloseDecision::allow;
}

void WindowSet::close_children(WindowSlot& slot) noexcept {
    for (auto& candidate : storage_) {
        if (candidate.state == WindowSlotState::open && candidate.owner == slot.id) {
            close_children(candidate);
            finish_close(candidate, false, 0);
        }
    }
}

void WindowSet::finish_close(WindowSlot& slot, bool accepted, std::uint64_t value) noexcept {
    if (slot.dialog) {
        slot.dialog_accepted = accepted;
        slot.dialog_ready = true;
        slot.dialog_value = value;
        slot.shown = false;
        slot.state = WindowSlotState::closed;
        slot.focused_node = {};
        slot.accessibility_root = {};
        slot.semantics_tree = nullptr;
        return;
    }

    slot = {};
}

std::size_t WindowSet::child_count(WindowId window) const noexcept {
    auto count = std::size_t{0};

    for (const auto& slot : storage_) {
        if (slot.state == WindowSlotState::open && slot.owner == window) {
            ++count;
        }
    }

    return count;
}

}
