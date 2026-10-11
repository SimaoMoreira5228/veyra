#ifndef VEYRA_PLATFORM_WINDOW_HPP
#define VEYRA_PLATFORM_WINDOW_HPP

#include "veyra/core/shortcut_map.hpp"
#include "veyra/layout/geometry.hpp"
#include "veyra/render/software_renderer.hpp"

#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <span>

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
};

enum class PointerButton : std::uint32_t {
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

union WindowEventData {
    WindowResize resize;
    WindowScale scale;
    WindowPointer pointer;
    WindowWheel wheel;
    WindowKey key;
    WindowTextCommit commit;
    WindowTextEditing editing;

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
    SoftwareSurface& surface,
    std::span<SoftwarePixel> pixels,
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
    { backend.poll_event(event) } noexcept -> std::same_as<bool>;
    { backend.acquire_surface(surface, pixels) } noexcept -> std::same_as<WindowResult>;
    { backend.present() } noexcept -> std::same_as<WindowResult>;
    { backend.start_text_input() } noexcept -> std::same_as<WindowResult>;
    { backend.stop_text_input() } noexcept -> std::same_as<WindowResult>;
    { backend.set_text_input_area(area) } noexcept -> std::same_as<WindowResult>;
    { backend.clear_text_input_area() } noexcept -> std::same_as<WindowResult>;
    { backend.set_clipboard_text(clipboard) } noexcept -> std::same_as<WindowResult>;
    { backend.get_clipboard_text(text, length) } noexcept -> std::same_as<WindowResult>;
};

}

#endif
