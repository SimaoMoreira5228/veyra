#include "veyra/platform/headless/null_window.hpp"

#include <cmath>
#include <cstdint>
#include <limits>

namespace veyra {

NullWindow::NullWindow(const char* title, int width, int height, WindowId id) noexcept {
    if (title == nullptr || width <= 0 || height <= 0) {
        creation_result_ = WindowResult::invalid_input;
        return;
    }

    if (id == kInvalidWindowId) {
        creation_result_ = WindowResult::invalid_input;
        return;
    }

    window_id_ = id;
    logical_width_ = static_cast<float>(width);
    logical_height_ = static_cast<float>(height);
    creation_result_ = WindowResult::ready;
}

WindowResult NullWindow::creation_result() const noexcept {
    return creation_result_;
}

WindowId NullWindow::window_id() const noexcept {
    return window_id_;
}

float NullWindow::logical_width() const noexcept {
    return logical_width_;
}

float NullWindow::logical_height() const noexcept {
    return logical_height_;
}

float NullWindow::scale_x() const noexcept {
    return scale_x_;
}

float NullWindow::scale_y() const noexcept {
    return scale_y_;
}

std::uint64_t NullWindow::surface_generation() const noexcept {
    return surface_generation_;
}

WindowCapabilities NullWindow::capabilities() const noexcept {
    return {};
}

std::uint64_t NullWindow::presented_frames() const noexcept {
    return presented_frames_;
}

std::uint32_t NullWindow::gamepad_count() const noexcept {
    return 0;
}

bool NullWindow::text_input_active() const noexcept {
    return text_input_active_;
}

WindowState NullWindow::window_state() const noexcept {
    return window_state_;
}

bool NullWindow::push_event(WindowEvent event) noexcept {
    if (queue_count_ == kEventCapacity) {
        return false;
    }

    if (!is_valid_script(event)) {
        return false;
    }

    event.window = window_id_;

    const std::size_t slot = (queue_head_ + queue_count_) % kEventCapacity;

    queue_[slot] = event;
    ++queue_count_;

    return true;
}

bool NullWindow::poll_event(WindowEvent& event) noexcept {
    if (queue_count_ == 0) {
        return false;
    }

    event = queue_[queue_head_];
    queue_head_ = (queue_head_ + 1) % kEventCapacity;
    --queue_count_;

    if (event.kind == WindowEventKind::resize) {
        logical_width_ = event.data.resize.width;
        logical_height_ = event.data.resize.height;
    }

    if (event.kind == WindowEventKind::scale_change) {
        scale_x_ = event.data.scale.x;
        scale_y_ = event.data.scale.y;
    }

    if (event.kind == WindowEventKind::state_change) {
        window_state_ = event.data.state;
    }

    return true;
}

WindowResult
NullWindow::acquire_surface(SoftwareSurface& surface, std::span<SoftwarePixel> backing) noexcept {
    if (frame_acquired_) {
        return WindowResult::invalid_state;
    }

    surface = {};

    if (creation_result_ != WindowResult::ready) {
        return creation_result_;
    }

    if (suspended_) {
        return WindowResult::suspended;
    }

    const double scaled_width = static_cast<double>(logical_width_) * scale_x_;
    const double scaled_height = static_cast<double>(logical_height_) * scale_y_;
    const double scaled_area = scaled_width * scaled_height;
    const double longest_long = static_cast<double>(std::numeric_limits<long>::max());

    if (!std::isfinite(scaled_area) || scaled_area < 1.0) {
        return WindowResult::unsupported_size;
    }

    if (scaled_width > longest_long || scaled_height > longest_long) {
        return WindowResult::unsupported_size;
    }

    const auto pixel_width = static_cast<std::size_t>(std::lround(scaled_width));
    const auto pixel_height = static_cast<std::size_t>(std::lround(scaled_height));

    if (pixel_width == 0 || pixel_height == 0) {
        return WindowResult::unsupported_size;
    }

    if (pixel_width > std::numeric_limits<std::size_t>::max() / pixel_height) {
        return WindowResult::unsupported_size;
    }

    const std::size_t pixel_count = pixel_width * pixel_height;

    if (pixel_count > backing.size()) {
        return WindowResult::insufficient_capacity;
    }

    if (pixel_width != acquired_width_ || pixel_height != acquired_height_) {
        ++surface_generation_;
        acquired_width_ = pixel_width;
        acquired_height_ = pixel_height;
    }

    surface = {
        .width = pixel_width,
        .height = pixel_height,
        .stride = pixel_width,
        .pixels = backing.subspan(0, pixel_count),
    };
    frame_acquired_ = true;

    return WindowResult::ready;
}

WindowResult NullWindow::present() noexcept {
    if (!frame_acquired_) {
        return WindowResult::invalid_state;
    }

    frame_acquired_ = false;
    ++presented_frames_;

    return WindowResult::ready;
}

WindowResult NullWindow::set_state(WindowState state) noexcept {
    if (creation_result_ != WindowResult::ready) {
        return creation_result_;
    }
    if (!valid_window_state(state)) {
        return WindowResult::invalid_input;
    }

    window_state_ = state;
    return WindowResult::ready;
}

WindowResult NullWindow::start_text_input() noexcept {
    if (creation_result_ != WindowResult::ready) {
        return creation_result_;
    }

    text_input_active_ = true;

    return WindowResult::ready;
}

WindowResult NullWindow::stop_text_input() noexcept {
    if (creation_result_ != WindowResult::ready) {
        return creation_result_;
    }

    text_input_active_ = false;

    return WindowResult::ready;
}

WindowResult NullWindow::set_text_input_area(TextInputArea area) noexcept {
    if (area.width <= 0 || area.height <= 0 || area.cursor < 0 ||
        static_cast<std::int64_t>(area.x) + area.width > std::numeric_limits<int>::max() ||
        static_cast<std::int64_t>(area.y) + area.height > std::numeric_limits<int>::max() ||
        static_cast<std::int64_t>(area.x) + area.cursor > std::numeric_limits<int>::max()) {
        return WindowResult::invalid_input;
    }

    if (creation_result_ != WindowResult::ready) {
        return creation_result_;
    }

    input_area_ = area;

    return WindowResult::ready;
}

WindowResult NullWindow::clear_text_input_area() noexcept {
    if (creation_result_ != WindowResult::ready) {
        return creation_result_;
    }

    input_area_ = {};

    return WindowResult::ready;
}

WindowResult NullWindow::set_clipboard_text(const char* text) noexcept {
    if (text == nullptr) {
        return WindowResult::invalid_input;
    }

    if (creation_result_ != WindowResult::ready) {
        return creation_result_;
    }

    std::size_t length = 0;

    while (text[length] != '\0') {
        ++length;

        if (length > kClipboardCapacity) {
            return WindowResult::clipboard_failed;
        }
    }

    for (std::size_t index = 0; index < length; ++index) {
        clipboard_[index] = text[index];
    }

    clipboard_length_ = length;

    return WindowResult::ready;
}

WindowResult NullWindow::get_clipboard_text(std::span<char> output, std::size_t& length) noexcept {
    if (creation_result_ != WindowResult::ready) {
        return creation_result_;
    }

    length = clipboard_length_;

    if (clipboard_length_ > output.size()) {
        return WindowResult::insufficient_capacity;
    }

    for (std::size_t index = 0; index < clipboard_length_; ++index) {
        output[index] = clipboard_[index];
    }

    return WindowResult::ready;
}

void NullWindow::set_suspended(bool suspended) noexcept {
    suspended_ = suspended;
}

bool NullWindow::is_valid_script(const WindowEvent& event) noexcept {
    if (event.kind == WindowEventKind::resize) {
        const float width = event.data.resize.width;
        const float height = event.data.resize.height;

        return width > 0.0F && height > 0.0F && std::isfinite(width) && std::isfinite(height);
    }

    if (event.kind == WindowEventKind::scale_change) {
        const float x = event.data.scale.x;
        const float y = event.data.scale.y;

        return x > 0.0F && y > 0.0F && std::isfinite(x) && std::isfinite(y);
    }

    if (event.kind == WindowEventKind::text_commit) {
        return event.data.commit.length <= kWindowTextCommitCapacity;
    }

    if (event.kind == WindowEventKind::text_editing) {
        return event.data.editing.length <= kWindowTextPreeditCapacity;
    }

    if (event.kind == WindowEventKind::state_change) {
        return valid_window_state(event.data.state);
    }

    return true;
}

}

namespace veyra {

NoticePermission NullNotices::notice_permission() const noexcept {
    return NoticePermission::granted;
}

NoticeAsk NullNotices::notice_request_permission() noexcept {
    return {.result = ServiceResult::ok, .permission = NoticePermission::granted};
}

ServiceResult
NullNotices::notice_show(std::uint64_t id, const char* title, const char* body) noexcept {
    if (id == 0 || title == nullptr || body == nullptr) {
        return ServiceResult::invalid_input;
    }

    return ServiceResult::ok;
}

NoticeRead NullNotices::notice_read(std::uint64_t id) const noexcept {
    static_cast<void>(id);

    return {.result = ServiceResult::not_found, .announcement = {}};
}

ServiceResult NullNotices::notice_dismiss(std::uint64_t id) noexcept {
    static_cast<void>(id);

    return ServiceResult::not_found;
}

bool NullNotices::notice_visible(std::uint64_t id) const noexcept {
    static_cast<void>(id);

    return false;
}

std::size_t NullNotices::notice_count() const noexcept {
    return 0;
}

bool NullNotices::notice_poll_click(std::uint64_t& id) noexcept {
    static_cast<void>(id);

    return false;
}

}
