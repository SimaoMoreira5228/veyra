#ifndef VEYRA_APP_SDL3_WINDOW_HOST_HPP
#define VEYRA_APP_SDL3_WINDOW_HOST_HPP

#include "veyra/app/async_image_decode.hpp"
#include "veyra/app/decode_tracker.hpp"
#include "veyra/app/window_set.hpp"
#include "veyra/platform/core/window_router.hpp"
#include "veyra/platform/sdl3/sdl3_window.hpp"
#include "veyra/render/present/frame_commit.hpp"
#include "veyra/render/present/render_worker.hpp"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <thread>

namespace veyra::app::sdl3 {

enum class TeardownStage : std::uint8_t {
    decoders,
    renderers,
    windows,
};

using TeardownObserver = void (*)(void* user, TeardownStage stage) noexcept;

class Sdl3WindowHost {
  public:
    static constexpr std::size_t kCapacity = 8;

    explicit Sdl3WindowHost(ShutdownMode shutdown = ShutdownMode::on_last_window_close) noexcept :
        windows_{slots_, shutdown} {
        render_worker_.start();
    }
    ~Sdl3WindowHost();

    Sdl3WindowHost(const Sdl3WindowHost&) = delete;
    Sdl3WindowHost& operator=(const Sdl3WindowHost&) = delete;
    Sdl3WindowHost(Sdl3WindowHost&&) = delete;
    Sdl3WindowHost& operator=(Sdl3WindowHost&&) = delete;

    WindowSetResult create(
        const char* title,
        int width,
        int height,
        const WindowDesc& desc,
        WindowId& out_window
    ) noexcept;
    WindowSetResult show(WindowId window) noexcept;
    WindowSetResult request_close(WindowId window, CloseReason reason) noexcept;
    WindowSetResult close_with_result(WindowId window, bool accepted, std::uint64_t value) noexcept;
    WindowSetResult destroy(WindowId window) noexcept;
    WindowSetResult set_state(WindowId window, WindowState state) noexcept;
    WindowSetResult commit(
        WindowId window,
        std::span<const DisplayCommand> commands,
        std::uint32_t width,
        std::uint32_t height
    ) noexcept;
    WindowSetResult present(WindowId window, std::span<SoftwarePixel> backing) noexcept;
    WindowSetResult present_async(WindowId window, std::span<SoftwarePixel> backing) noexcept;
    bool wait_presented() noexcept;
    bool take_present_result(WindowId window, WindowSetResult& out) noexcept;
    WindowSetResult recover_device(WindowId window) noexcept;
    using PresentObserver = void (*)(void*) noexcept;
    // NOTE: the observer runs on the render thread while host state is locked.
    // It must not call back into the host.
    void set_present_observer(PresentObserver observer, void* user) noexcept;
    void set_route_observer(RouteObserver observer, void* user) noexcept;
    void set_teardown_observer(TeardownObserver observer, void* user) noexcept;
    NodeId activate(WindowId window) noexcept;
    HitTestResult hit_test_and_hover(
        WindowId window,
        LayoutPoint logical_point,
        std::span<const LayoutRect> bounds_by_node_index,
        NodeId& target
    ) noexcept;
    HitTestResult press_pointer(
        WindowId window,
        std::uint32_t pointer,
        LayoutPoint logical_point,
        std::span<const LayoutRect> bounds_by_node_index,
        NodeId& target
    ) noexcept;
    bool begin_decode(WindowId window, std::uint64_t request) noexcept;
    bool begin_decode(
        WindowId window,
        std::uint64_t request,
        std::span<const std::uint8_t> bytes,
        const ImageDecodeBudget& budget,
        std::span<ImageFrameTarget> frames,
        std::span<std::uint32_t> delays_ms,
        DecodeFunction decode
    ) noexcept;
    bool poll_decode(AsyncDecodeResult& out) noexcept;
    bool wait_decode(AsyncDecodeResult& out, unsigned timeout_ms) noexcept;
    bool complete_decode(std::uint64_t request) noexcept;
    bool take_decode(std::uint64_t request) noexcept;
    bool decode_discarded(std::uint64_t request) noexcept;
    bool capture_pointer(WindowId window, std::uint32_t pointer, NodeId target) noexcept;
    bool release_pointer(WindowId window, std::uint32_t pointer) noexcept;
    bool set_hover(WindowId window, NodeId node) noexcept;
    bool clear_hover(WindowId window) noexcept;
    bool surface_size(WindowId window, std::uint32_t& width, std::uint32_t& height) noexcept;
    bool take_device_lost(WindowId window) noexcept;
    bool render_thread_active() const noexcept;
    std::thread::id render_thread_id() const noexcept;
    std::uint64_t render_thread_presents() const noexcept;
    bool bind_semantics(WindowId window, NodeTree& tree, NodeId root) noexcept;
    bool set_focused_node(WindowId window, NodeId node) noexcept;

    bool poll_event(WindowEvent& event, WindowId& logical_window) noexcept;

    Sdl3Window* backend(WindowId window) noexcept;
    const Sdl3Window* backend(WindowId window) const noexcept;
    WindowSet& windows() noexcept;
    const WindowSet& windows() const noexcept;
    WindowRouter& router() noexcept;
    const WindowRouter& router() const noexcept;

  private:
    struct PresentTask {
        Sdl3WindowHost* host{};
        WindowId window{kInvalidWindowId};
        std::span<SoftwarePixel> backing{};
    };

    struct PresentSlot {
        FrameCommit commit{};
        std::array<SoftwareClipRect, 64> clips{};
        std::array<LayoutPoint, 64> translations{};
        std::array<double, 128> path_intersections{};
        std::array<std::int8_t, 128> path_windings{};
        PresentTask task{};
        WindowSetResult result{WindowSetResult::presentation_failed};
        bool has_commit{};
        bool in_flight{};
        bool completed{};
        bool show_pending{};
        bool presented{};
        bool device_lost{};
        bool needs_recovery{};
    };

    WindowSetResult present_now(WindowId window, std::span<SoftwarePixel> backing) noexcept;
    void notify_teardown(TeardownStage stage) noexcept;
    HitTestResult hit_test_hover_locked(
        WindowId window,
        LayoutPoint logical_point,
        std::span<const LayoutRect> bounds_by_node_index,
        NodeId& target
    ) noexcept;
    static void run_present_task(void* context) noexcept;
    std::size_t slot_for(WindowId window) const noexcept;
    WindowId logical_for_sdl(WindowId sdl_window) const noexcept;
    void sync_backends() noexcept;

    std::array<WindowSlot, kCapacity> slots_{};
    std::array<std::optional<Sdl3Window>, kCapacity> backends_{};
    std::array<WindowId, kCapacity> sdl_ids_{};
    std::array<PresentSlot, kCapacity> present_{};
    DecodeTracker decodes_{};
    std::array<WindowRouteState, kCapacity> route_storage_{};
    WindowRouter router_{route_storage_};
    WindowSet windows_{slots_};
    mutable std::mutex state_mutex_;
    PresentObserver present_observer_{};
    void* present_observer_user_{};
    TeardownObserver teardown_observer_{};
    void* teardown_observer_user_{};
    std::atomic<std::uint64_t> render_thread_presents_{};
    RenderWorker render_worker_;
    AsyncImageDecoder decoder_;
};

}

namespace veyra::app {
using ::veyra::app::sdl3::Sdl3WindowHost;
}

#endif
