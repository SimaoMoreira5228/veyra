#ifndef VEYRA_PLATFORM_NULL_WINDOW_HPP
#define VEYRA_PLATFORM_NULL_WINDOW_HPP

#include "veyra/platform/fake_services.hpp"
#include "veyra/platform/window.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace veyra {

class NullWindow {
  public:
    static constexpr std::size_t kEventCapacity = 32;
    static constexpr std::size_t kClipboardCapacity = 256;

    NullWindow(const char* title, int width, int height, WindowId id = 1) noexcept;

    NullWindow(const NullWindow&) = delete;
    NullWindow& operator=(const NullWindow&) = delete;
    NullWindow(NullWindow&&) = delete;
    NullWindow& operator=(NullWindow&&) = delete;

    WindowResult creation_result() const noexcept;
    WindowId window_id() const noexcept;
    float logical_width() const noexcept;
    float logical_height() const noexcept;
    float scale_x() const noexcept;
    float scale_y() const noexcept;
    std::uint64_t surface_generation() const noexcept;
    WindowCapabilities capabilities() const noexcept;
    std::uint64_t presented_frames() const noexcept;
    bool text_input_active() const noexcept;

    bool push_event(WindowEvent event) noexcept;
    bool poll_event(WindowEvent& event) noexcept;

    WindowResult
    acquire_surface(SoftwareSurface& surface, std::span<SoftwarePixel> backing) noexcept;
    WindowResult present() noexcept;

    WindowResult create_vulkan_surface(void* instance, void*& surface) noexcept;

    WindowResult start_text_input() noexcept;
    WindowResult stop_text_input() noexcept;
    WindowResult set_text_input_area(TextInputArea area) noexcept;
    WindowResult clear_text_input_area() noexcept;

    WindowResult set_clipboard_text(const char* text) noexcept;
    WindowResult get_clipboard_text(std::span<char> output, std::size_t& length) noexcept;

    void set_suspended(bool suspended) noexcept;

  private:
    static bool is_valid_script(const WindowEvent& event) noexcept;

    WindowResult creation_result_{WindowResult::invalid_state};
    WindowId window_id_{kInvalidWindowId};
    float logical_width_{};
    float logical_height_{};
    float scale_x_{1.0F};
    float scale_y_{1.0F};
    std::uint64_t surface_generation_{};
    std::uint64_t presented_frames_{};
    std::size_t acquired_width_{};
    std::size_t acquired_height_{};
    bool frame_acquired_{};
    bool suspended_{};
    bool text_input_active_{};
    TextInputArea input_area_{};
    std::array<char, kClipboardCapacity> clipboard_{};
    std::size_t clipboard_length_{};
    std::array<WindowEvent, kEventCapacity> queue_{};
    std::size_t queue_head_{};
    std::size_t queue_count_{};
};

static_assert(WindowBackend<NullWindow>);
static_assert(SoftwareSurfacePresenter<NullWindow>);
static_assert(VulkanSurfaceProvider<NullWindow>);

class NullNotices {
  public:
    NullNotices() noexcept = default;

    NullNotices(const NullNotices&) = delete;
    NullNotices& operator=(const NullNotices&) = delete;
    NullNotices(NullNotices&&) = delete;
    NullNotices& operator=(NullNotices&&) = delete;

    NoticePermission notice_permission() const noexcept;
    NoticeAsk notice_request_permission() noexcept;
    ServiceResult notice_show(std::uint64_t id, const char* title, const char* body) noexcept;
    NoticeRead notice_read(std::uint64_t id) const noexcept;
    ServiceResult notice_dismiss(std::uint64_t id) noexcept;
    bool notice_visible(std::uint64_t id) const noexcept;
    std::size_t notice_count() const noexcept;
    bool notice_poll_click(std::uint64_t& id) noexcept;
};

static_assert(NoticeBackend<NullNotices>);

}

#endif
