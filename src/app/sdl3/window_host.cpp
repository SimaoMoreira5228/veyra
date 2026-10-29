#include "veyra/app/sdl3/window_host.hpp"

namespace veyra::app::sdl3 {
namespace {

struct PresentTask {
    Sdl3WindowHost* host{};
    WindowId window{kInvalidWindowId};
    std::span<SoftwarePixel> backing{};
    WindowSetResult result{WindowSetResult::presentation_failed};
};

}

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
    present_[index] = {};
    backend.emplace(title, width, height, false);
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

    if (windows_.set_geometry(
            logical,
            backend->logical_width(),
            backend->logical_height(),
            backend->scale_x(),
            backend->scale_y()
        ) != WindowSetResult::ok) {
        router_.detach(logical);
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
    if (!backend.has_value()) {
        return WindowSetResult::invalid_window;
    }

    auto& present = present_[index];
    if (present.presented) {
        if (backend->show() != WindowResult::ready) {
            windows_.destroy(window);
            sync_backends();
            return WindowSetResult::invalid_state;
        }
    } else {
        present.show_pending = true;
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

bool Sdl3WindowHost::bind_semantics(WindowId window, NodeTree& tree, NodeId root) noexcept {
    return windows_.bind_semantics(window, tree, root) == WindowSetResult::ok;
}

WindowSetResult Sdl3WindowHost::commit(
    WindowId window,
    std::span<const DisplayCommand> commands,
    std::uint32_t width,
    std::uint32_t height
) noexcept {
    const auto index = slot_for(window);
    if (index == kCapacity || !backends_[index].has_value()) {
        return WindowSetResult::invalid_window;
    }

    auto& present = present_[index];
    const auto result =
        commit_frame(commands, width, height, present.commit.sequence + 1, present.commit);
    if (result == FrameCommitResult::insufficient_capacity) {
        return WindowSetResult::capacity;
    }
    if (result == FrameCommitResult::invalid_size) {
        return WindowSetResult::invalid_geometry;
    }

    present.has_commit = true;
    router_.mark_damaged(window);
    return WindowSetResult::ok;
}

WindowSetResult
Sdl3WindowHost::present(WindowId window, std::span<SoftwarePixel> backing) noexcept {
    if (!render_worker_.running()) {
        return present_now(window, backing);
    }

    auto task = PresentTask{
        .host = this,
        .window = window,
        .backing = backing,
    };
    if (!render_worker_.run(&Sdl3WindowHost::run_present_task, &task)) {
        return WindowSetResult::presentation_failed;
    }

    return task.result;
}

bool Sdl3WindowHost::take_device_lost(WindowId window) noexcept {
    const auto index = slot_for(window);
    if (index == kCapacity) {
        return false;
    }

    const auto lost = present_[index].device_lost;
    present_[index].device_lost = false;
    return lost;
}

bool Sdl3WindowHost::render_thread_active() const noexcept {
    return render_worker_.running();
}

std::thread::id Sdl3WindowHost::render_thread_id() const noexcept {
    return render_worker_.thread_id();
}

std::uint64_t Sdl3WindowHost::render_thread_presents() const noexcept {
    return render_thread_presents_;
}

void Sdl3WindowHost::run_present_task(void* context) noexcept {
    auto* task = static_cast<PresentTask*>(context);
    task->result = task->host->present_now(task->window, task->backing);
}

WindowSetResult
Sdl3WindowHost::present_now(WindowId window, std::span<SoftwarePixel> backing) noexcept {
    if (render_worker_.is_worker_thread()) {
        ++render_thread_presents_;
    }

    const auto index = slot_for(window);
    if (index == kCapacity) {
        return WindowSetResult::invalid_window;
    }

    auto& optional = backends_[index];
    if (!optional.has_value()) {
        return WindowSetResult::invalid_window;
    }
    auto* backend = &*optional;
    auto& present = present_[index];
    if (!present.has_commit) {
        return WindowSetResult::stale_frame;
    }
    if (present.commit.width != backend->pixel_width() ||
        present.commit.height != backend->pixel_height()) {
        return WindowSetResult::stale_frame;
    }

    auto surface = SoftwareSurface{};
    if (backend->acquire_surface(surface, backing) != WindowResult::ready) {
        return WindowSetResult::presentation_failed;
    }
    if (surface.width != present.commit.width || surface.height != present.commit.height) {
        backend->discard_frame();
        return WindowSetResult::stale_frame;
    }

    const SoftwareRenderWorkspace workspace{
        .clips = present.clips,
        .translations = present.translations,
        .path_intersections = present.path_intersections,
        .path_windings = present.path_windings,
    };
    if (render_software(
            std::span<const DisplayCommand>(
                present.commit.commands.data(),
                present.commit.command_count
            ),
            surface,
            workspace
        ) != SoftwareRenderResult::complete) {
        backend->discard_frame();
        return WindowSetResult::presentation_failed;
    }
    if (backend->present() != WindowResult::ready) {
        return WindowSetResult::presentation_failed;
    }

    present.has_commit = false;
    present.presented = true;
    router_.mark_presented(window);
    if (present.show_pending) {
        if (backend->show() != WindowResult::ready) {
            return WindowSetResult::presentation_failed;
        }
        present.show_pending = false;
    }

    return WindowSetResult::ok;
}

Sdl3WindowHost::~Sdl3WindowHost() {
    render_worker_.stop();
}

WindowSetResult Sdl3WindowHost::set_state(WindowId window, WindowState state) noexcept {
    const auto index = slot_for(window);
    if (index == kCapacity) {
        return WindowSetResult::invalid_window;
    }

    auto& backend = backends_[index];
    if (!backend.has_value()) {
        return WindowSetResult::invalid_window;
    }
    if (backend->set_state(state) != WindowResult::ready) {
        return WindowSetResult::invalid_state;
    }

    const auto result = windows_.set_state(window, state);
    if (result == WindowSetResult::ok) {
        router_.mark_damaged(window);
    }

    return result;
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
            if (event.kind == WindowEventKind::state_change) {
                windows_.set_state(logical, event.data.state);
                router_.mark_damaged(logical);
            }
            if (event.kind == WindowEventKind::device_lost) {
                const auto index = slot_for(logical);
                if (index != kCapacity) {
                    present_[index].device_lost = true;
                }
                router_.mark_damaged(logical);
            }
            const auto* state = router_.find(logical);
            if (state != nullptr) {
                windows_.set_geometry(
                    logical,
                    state->logical_width(),
                    state->logical_height(),
                    state->scale_x(),
                    state->scale_y()
                );
            }
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
            present_[index] = {};
            backends_[index].reset();
            sdl_ids_[index] = kInvalidWindowId;
        }
    }
}

}
