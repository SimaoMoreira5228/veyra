#ifndef VEYRA_PLATFORM_WINDOW_HPP
#define VEYRA_PLATFORM_WINDOW_HPP

#include "veyra/core/input/shortcut_map.hpp"
#include "veyra/foundation/geometry.hpp"
#include "veyra/foundation/image.hpp"

#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <span>
#include <type_traits>

namespace veyra {

using WindowId = std::uint64_t;

inline constexpr WindowId kInvalidWindowId = 0;
inline constexpr std::uint32_t kMousePointerId = 0;

inline constexpr std::size_t kWindowTextCommitCapacity = 128;
inline constexpr std::size_t kWindowTextPreeditCapacity = 64;

enum class WindowResult : std::uint8_t {
    ready,
    suspended,
    invalid_input,
    invalid_state,
    unsupported_size,
    text_input_failed,
    clipboard_failed,
    insufficient_capacity,
    presentation_failed,
    creation_failed,
    unsupported,
};

enum class WindowTextureFormat : std::uint32_t {
    rgba8_unorm = 1U << 0U,
    rgba8_srgb = 1U << 1U,
    alpha8_coverage = 1U << 2U,
};

constexpr std::uint32_t texture_format_flag(WindowTextureFormat format) noexcept {
    return static_cast<std::uint32_t>(format);
}

constexpr bool has_texture_format(std::uint32_t mask, WindowTextureFormat format) noexcept {
    return (mask & static_cast<std::uint32_t>(format)) != 0;
}

enum class WindowClipSupport : std::uint8_t {
    none,
    rect_only,
    stencil,
    mask,
};

enum class WindowPresentMode : std::uint32_t {
    fifo = 1U << 0U,
    mailbox = 1U << 1U,
    immediate = 1U << 2U,
};

constexpr std::uint32_t present_mode_flag(WindowPresentMode mode) noexcept {
    return static_cast<std::uint32_t>(mode);
}

constexpr bool has_present_mode(std::uint32_t mask, WindowPresentMode mode) noexcept {
    return (mask & static_cast<std::uint32_t>(mode)) != 0;
}

enum class WindowColorSpace : std::uint32_t {
    srgb = 1U << 0U,
};

constexpr std::uint32_t color_space_flag(WindowColorSpace space) noexcept {
    return static_cast<std::uint32_t>(space);
}

constexpr bool has_color_space(std::uint32_t mask, WindowColorSpace space) noexcept {
    return (mask & static_cast<std::uint32_t>(space)) != 0;
}

struct WindowCapabilities {
    bool clipboard{};
    bool text_input{};
    bool text_preedit{};
    bool text_input_area{};
    std::uint32_t max_texture_width{};
    std::uint32_t max_texture_height{};
    std::uint32_t texture_formats{};
    WindowClipSupport clip{WindowClipSupport::none};
    std::uint32_t present_modes{};
    bool present_timestamp{};
    bool external_image_import{};
    bool external_image_export{};
    std::uint32_t color_spaces{};
    bool srgb_transfer_approximate{};
    bool device_loss_recovery{};
    bool external_render_target{};
};

static_assert(std::is_trivially_copyable_v<WindowCapabilities>);

enum class PointerButton : std::uint8_t {
    primary = 1U << 0U,
    middle = 1U << 1U,
    secondary = 1U << 2U,
    back = 1U << 3U,
    forward = 1U << 4U,
};

constexpr std::uint32_t operator|(PointerButton left, PointerButton right) noexcept {
    return static_cast<std::uint32_t>(left) | static_cast<std::uint32_t>(right);
}

constexpr std::uint32_t operator|(std::uint32_t left, PointerButton right) noexcept {
    return left | static_cast<std::uint32_t>(right);
}

constexpr std::uint32_t button_flag(PointerButton button) noexcept {
    return static_cast<std::uint32_t>(button);
}

constexpr bool has_button(std::uint32_t mask, PointerButton button) noexcept {
    return (mask & static_cast<std::uint32_t>(button)) != 0;
}

enum class WindowEventKind : std::uint8_t {
    close_request,
    resize,
    scale_change,
    pointer_down,
    pointer_up,
    pointer_move,
    pointer_cancel,
    wheel,
    key_down,
    key_up,
    text_commit,
    text_editing,
    focus_gained,
    focus_lost,
    gamepad_axis,
    gamepad_button,
    gamepad_added,
    gamepad_removed,
};

struct WindowResize {
    float width{};
    float height{};
};

struct WindowScale {
    float x{};
    float y{};
};

struct WindowPointer {
    LayoutPoint position{};
    std::uint32_t pointer{kMousePointerId};
    std::uint32_t buttons{};
    std::uint32_t modifiers{};
    std::uint8_t click_count{};
};

struct WindowWheel {
    float delta_x{};
    float delta_y{};
    std::uint32_t modifiers{};
};

struct WindowKey {
    std::uint32_t code{};
    std::uint32_t modifiers{};
    bool repeat{};
};

// NOTE: text payloads inline their bytes, so an event owns its text for as long
// as the event object lives. Backends copy platform text in at the boundary and
// never hand out spans into memory the caller does not own.

struct WindowTextCommit {
    std::array<char, kWindowTextCommitCapacity> text{};
    std::uint32_t length{};
    bool truncated{};
};

struct WindowTextEditing {
    std::array<char, kWindowTextPreeditCapacity> text{};
    std::uint32_t length{};
    std::int32_t cursor{-1};
    std::int32_t selection_length{-1};
    bool truncated{};
};

struct WindowGamepadAxis {
    std::uint32_t device{};
    std::uint32_t axis{};
    float value{};
};

struct WindowGamepadButton {
    std::uint32_t device{};
    std::uint32_t button{};
    bool pressed{};
};

struct WindowGamepadDevice {
    std::uint32_t device{};
};

static_assert(std::is_trivially_copyable_v<WindowGamepadAxis>);
static_assert(std::is_trivially_copyable_v<WindowGamepadButton>);
static_assert(std::is_trivially_copyable_v<WindowGamepadDevice>);

union WindowEventData {
    WindowResize resize;
    WindowScale scale;
    WindowPointer pointer;
    WindowWheel wheel;
    WindowKey key;
    WindowTextCommit commit;
    WindowTextEditing editing;
    WindowGamepadAxis gamepad_axis;
    WindowGamepadButton gamepad_button;
    WindowGamepadDevice gamepad_device;

    WindowEventData() noexcept : resize{} {
    }
};

struct WindowEvent {
    WindowEventKind kind{WindowEventKind::close_request};
    WindowId window{kInvalidWindowId};
    WindowEventData data{};
};

struct TextInputArea {
    int x{};
    int y{};
    int width{};
    int height{};
    int cursor{};
};

template <typename Backend>
concept WindowBackend = requires(
    Backend& backend,
    const Backend& observer,
    WindowEvent& event,
    std::span<char> text,
    std::size_t& length,
    TextInputArea area,
    const char* clipboard
) {
    { observer.creation_result() } noexcept -> std::same_as<WindowResult>;
    { observer.window_id() } noexcept -> std::same_as<WindowId>;
    { observer.logical_width() } noexcept -> std::same_as<float>;
    { observer.logical_height() } noexcept -> std::same_as<float>;
    { observer.scale_x() } noexcept -> std::same_as<float>;
    { observer.scale_y() } noexcept -> std::same_as<float>;
    { observer.surface_generation() } noexcept -> std::same_as<std::uint64_t>;
    { observer.capabilities() } noexcept -> std::same_as<WindowCapabilities>;
    { backend.poll_event(event) } noexcept -> std::same_as<bool>;
    { observer.gamepad_count() } noexcept -> std::same_as<std::uint32_t>;
    { backend.start_text_input() } noexcept -> std::same_as<WindowResult>;
    { backend.stop_text_input() } noexcept -> std::same_as<WindowResult>;
    { backend.set_text_input_area(area) } noexcept -> std::same_as<WindowResult>;
    { backend.clear_text_input_area() } noexcept -> std::same_as<WindowResult>;
    { backend.set_clipboard_text(clipboard) } noexcept -> std::same_as<WindowResult>;
    { backend.get_clipboard_text(text, length) } noexcept -> std::same_as<WindowResult>;
};

template <typename Presenter>
concept SoftwareSurfacePresenter =
    requires(Presenter& presenter, SoftwareSurface& surface, std::span<SoftwarePixel> pixels) {
        { presenter.acquire_surface(surface, pixels) } noexcept -> std::same_as<WindowResult>;
        { presenter.present() } noexcept -> std::same_as<WindowResult>;
    };

}

namespace veyra::platform {
using ::veyra::PointerButton;
using ::veyra::TextInputArea;
using ::veyra::WindowCapabilities;
using ::veyra::WindowClipSupport;
using ::veyra::WindowColorSpace;
using ::veyra::WindowEvent;
using ::veyra::WindowEventData;
using ::veyra::WindowEventKind;
using ::veyra::WindowGamepadAxis;
using ::veyra::WindowGamepadButton;
using ::veyra::WindowGamepadDevice;
using ::veyra::WindowId;
using ::veyra::WindowKey;
using ::veyra::WindowPointer;
using ::veyra::WindowPresentMode;
using ::veyra::WindowResize;
using ::veyra::WindowResult;
using ::veyra::WindowScale;
using ::veyra::WindowTextCommit;
using ::veyra::WindowTextEditing;
using ::veyra::WindowTextureFormat;
using ::veyra::WindowWheel;
using ::veyra::button_flag;
using ::veyra::color_space_flag;
using ::veyra::has_button;
using ::veyra::has_color_space;
using ::veyra::has_present_mode;
using ::veyra::has_texture_format;
using ::veyra::kInvalidWindowId;
using ::veyra::kMousePointerId;
using ::veyra::kWindowTextCommitCapacity;
using ::veyra::kWindowTextPreeditCapacity;
using ::veyra::present_mode_flag;
using ::veyra::texture_format_flag;
}

namespace veyra::platform::core {
using ::veyra::PointerButton;
using ::veyra::TextInputArea;
using ::veyra::WindowCapabilities;
using ::veyra::WindowClipSupport;
using ::veyra::WindowColorSpace;
using ::veyra::WindowEvent;
using ::veyra::WindowEventData;
using ::veyra::WindowEventKind;
using ::veyra::WindowGamepadAxis;
using ::veyra::WindowGamepadButton;
using ::veyra::WindowGamepadDevice;
using ::veyra::WindowId;
using ::veyra::WindowKey;
using ::veyra::WindowPointer;
using ::veyra::WindowPresentMode;
using ::veyra::WindowResize;
using ::veyra::WindowResult;
using ::veyra::WindowScale;
using ::veyra::WindowTextCommit;
using ::veyra::WindowTextEditing;
using ::veyra::WindowTextureFormat;
using ::veyra::WindowWheel;
using ::veyra::button_flag;
using ::veyra::color_space_flag;
using ::veyra::has_button;
using ::veyra::has_color_space;
using ::veyra::has_present_mode;
using ::veyra::has_texture_format;
using ::veyra::kInvalidWindowId;
using ::veyra::kMousePointerId;
using ::veyra::kWindowTextCommitCapacity;
using ::veyra::kWindowTextPreeditCapacity;
using ::veyra::present_mode_flag;
using ::veyra::texture_format_flag;
}

#endif
