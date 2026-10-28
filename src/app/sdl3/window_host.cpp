#include "veyra/app/sdl3/window_host.hpp"

namespace veyra::app::sdl3 {

WindowSetResult Sdl3WindowHost::create(
    const char* title,
    int width,
    int height,
    const WindowDesc& desc,
    WindowId& out_window
) noexcept {
    auto logical = WindowId{};
    const auto result = windows_.create(desc, logical);
    if (result != WindowSetResult::ok) {
        return result;
    }

    const auto index = slot_for(logical);
    if (index == kCapacity) {
        windows_.destroy(logical);
        return WindowSetResult::capacity;
    }

    auto& backend = backends_[index];
    backend.emplace(title, width, height);
    if (backend->creation_result() != WindowResult::ready) {
        backend.reset();
        windows_.destroy(logical);
        return WindowSetResult::creation_failed;
    }

    if (router_.attach(
            logical,
            backend->logical_width(),
            backend->logical_height(),
            backend->scale_x(),
            backend->scale_y()
        ) == nullptr) {
        backend.reset();
        windows_.destroy(logical);
        return WindowSetResult::creation_failed;
    }

    sdl_ids_[index] = backend->window_id();
    out_window = logical;
    return WindowSetResult::ok;
}

WindowSetResult Sdl3WindowHost::show(WindowId window) noexcept {
    const auto index = slot_for(window);
    if (index == kCapacity || !backends_[index].has_value()) {
        return WindowSetResult::invalid_window;
    }

    const auto result = windows_.show(window);
    if (result != WindowSetResult::ok) {
        return result;
    }

    auto& backend = backends_[index];
    if (!backend.has_value() || backend->show() != WindowResult::ready) {
        windows_.destroy(window);
        sync_backends();
        return WindowSetResult::invalid_state;
    }

    router_.mark_damaged(window);
    return WindowSetResult::ok;
}

WindowSetResult Sdl3WindowHost::request_close(WindowId window, CloseReason reason) noexcept {
    const auto result = windows_.request_close(window, reason);
    if (result == WindowSetResult::ok) {
        sync_backends();
    }

    return result;
}

WindowSetResult
Sdl3WindowHost::close_with_result(WindowId window, bool accepted, std::uint64_t value) noexcept {
    const auto result = windows_.close_with_result(window, accepted, value);
    if (result == WindowSetResult::ok) {
        sync_backends();
    }

    return result;
}

bool Sdl3WindowHost::set_focused_node(WindowId window, NodeId node) noexcept {
    if (!windows_.set_focused_node(window, node)) {
        return false;
    }

    return router_.set_focused_node(window, node);
}

bool Sdl3WindowHost::set_accessibility_root(WindowId window, NodeId node) noexcept {
    return windows_.set_accessibility_root(window, node);
}

WindowSetResult Sdl3WindowHost::destroy(WindowId window) noexcept {
    const auto result = windows_.destroy(window);
    if (result == WindowSetResult::ok) {
        sync_backends();
    }

    return result;
}

bool Sdl3WindowHost::poll_event(WindowEvent& event, WindowId& logical_window) noexcept {
    for (auto& optional : backends_) {
        if (!optional.has_value()) {
            continue;
        }
        auto* backend = &*optional;

        while (backend->poll_event(event)) {
            const auto logical = logical_for_sdl(event.window);
            if (logical == kInvalidWindowId) {
                continue;
            }

            event.window = logical;
            router_.route(event);
            logical_window = logical;
            return true;
        }
    }

    return false;
}

Sdl3Window* Sdl3WindowHost::backend(WindowId window) noexcept {
    const auto index = slot_for(window);
    if (index == kCapacity) {
        return nullptr;
    }

    auto& backend = backends_[index];
    if (!backend.has_value()) {
        return nullptr;
    }

    return &*backend;
}

const Sdl3Window* Sdl3WindowHost::backend(WindowId window) const noexcept {
    const auto index = slot_for(window);
    if (index == kCapacity) {
        return nullptr;
    }

    const auto& backend = backends_[index];
    if (!backend.has_value()) {
        return nullptr;
    }

    return &*backend;
}

WindowSet& Sdl3WindowHost::windows() noexcept {
    return windows_;
}

const WindowSet& Sdl3WindowHost::windows() const noexcept {
    return windows_;
}

WindowRouter& Sdl3WindowHost::router() noexcept {
    return router_;
}

const WindowRouter& Sdl3WindowHost::router() const noexcept {
    return router_;
}

std::size_t Sdl3WindowHost::slot_for(WindowId window) const noexcept {
    for (auto index = std::size_t{}; index < kCapacity; ++index) {
        if (slots_[index].id == window) {
            return index;
        }
    }

    return kCapacity;
}

WindowId Sdl3WindowHost::logical_for_sdl(WindowId sdl_window) const noexcept {
    for (auto index = std::size_t{}; index < kCapacity; ++index) {
        if (sdl_ids_[index] == sdl_window && backends_[index].has_value()) {
            return slots_[index].id;
        }
    }

    return kInvalidWindowId;
}

void Sdl3WindowHost::sync_backends() noexcept {
    for (auto index = std::size_t{}; index < kCapacity; ++index) {
        if (backends_[index].has_value() && !windows_.is_open(slots_[index].id)) {
            router_.detach(slots_[index].id);
            backends_[index].reset();
            sdl_ids_[index] = kInvalidWindowId;
        }
    }
}

}
