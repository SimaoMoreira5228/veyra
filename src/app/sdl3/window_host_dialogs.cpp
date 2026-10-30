#include "veyra/app/sdl3/window_host.hpp"

namespace veyra::app::sdl3 {

void Sdl3WindowHost::set_dialog_service(DialogService* service) noexcept {
    std::lock_guard lock(state_mutex_);
    dialog_service_ = service;
}

ServiceResult Sdl3WindowHost::open_dialog(WindowId owner, DialogKind kind) noexcept {
    std::lock_guard lock(state_mutex_);
    if (dialog_service_ == nullptr || !windows_.is_open(owner)) {
        return ServiceResult::unavailable;
    }

    for (auto& entry : pending_dialogs_) {
        if (entry.owner != kInvalidWindowId) {
            continue;
        }
        const auto open = dialog_service_->dialog_open(kind);
        if (open.result != ServiceResult::ok) {
            return open.result;
        }
        entry = {.owner = owner, .handle = open.handle};
        return ServiceResult::ok;
    }
    return ServiceResult::insufficient_capacity;
}

bool Sdl3WindowHost::has_pending_dialog(WindowId owner) const noexcept {
    std::lock_guard lock(state_mutex_);
    return has_pending_dialog_locked(owner);
}

bool Sdl3WindowHost::has_pending_dialog_locked(WindowId owner) const noexcept {
    for (const auto& entry : pending_dialogs_) {
        if (entry.owner == owner) {
            return true;
        }
    }
    return false;
}

bool Sdl3WindowHost::poll_service_dialog(WindowEvent& event, WindowId& logical_window) noexcept {
    if (dialog_service_ == nullptr) {
        return false;
    }

    for (auto& entry : pending_dialogs_) {
        if (entry.owner == kInvalidWindowId) {
            continue;
        }
        const auto poll = dialog_service_->dialog_poll(entry.handle);
        if (poll.result == ServiceResult::pending) {
            continue;
        }

        const auto owner = entry.owner;
        const auto handle = entry.handle;
        entry = {};
        if (poll.result != ServiceResult::ok) {
            continue;
        }

        event = {};
        event.kind = WindowEventKind::dialog_complete;
        event.window = owner;
        event.data.dialog = {
            .handle = handle,
            .choice = poll.answer.choice,
            .cancelled = poll.answer.cancelled,
        };
        logical_window = owner;
        router_.mark_damaged(owner);
        return true;
    }
    return false;
}

void Sdl3WindowHost::discard_dialogs(WindowId owner) noexcept {
    for (auto& entry : pending_dialogs_) {
        if (entry.owner != owner) {
            continue;
        }
        if (dialog_service_ != nullptr) {
            dialog_service_->dialog_close(entry.handle);
        }
        entry = {};
    }
}

}
