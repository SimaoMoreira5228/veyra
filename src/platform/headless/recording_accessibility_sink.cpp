#include "veyra/platform/headless/recording_accessibility_sink.hpp"

#include <algorithm>

namespace veyra {

void RecordingAccessibilitySink::reset() noexcept {
    window_ = kInvalidWindowId;
    begin_count_ = 0;
    end_count_ = 0;
    node_count_ = 0;
    dropped_ = 0;
}

void RecordingAccessibilitySink::begin_window(WindowId window) noexcept {
    window_ = window;
    ++begin_count_;
}

void RecordingAccessibilitySink::node(const AccessibilityNode& record) noexcept {
    if (node_count_ == records_.size()) {
        ++dropped_;
        return;
    }

    auto& stored = records_[node_count_];
    stored.node = record;
    stored.node.name = copy_text(stored.name, record.name);
    stored.node.value = copy_text(stored.value, record.value);
    ++node_count_;
}

void RecordingAccessibilitySink::end_window(WindowId window) noexcept {
    if (window == window_) {
        ++end_count_;
    }
}

WindowId RecordingAccessibilitySink::window() const noexcept {
    return window_;
}

std::size_t RecordingAccessibilitySink::begin_count() const noexcept {
    return begin_count_;
}

std::size_t RecordingAccessibilitySink::end_count() const noexcept {
    return end_count_;
}

std::size_t RecordingAccessibilitySink::node_count() const noexcept {
    return node_count_;
}

std::size_t RecordingAccessibilitySink::dropped() const noexcept {
    return dropped_;
}

const AccessibilityNode* RecordingAccessibilitySink::at(std::size_t index) const noexcept {
    if (index >= node_count_) {
        return nullptr;
    }

    return &records_[index].node;
}

std::string_view RecordingAccessibilitySink::copy_text(
    std::array<char, kTextCapacity>& storage,
    std::string_view text
) noexcept {
    const auto length = std::min(text.size(), storage.size());

    for (std::size_t index = 0; index < length; ++index) {
        storage[index] = text[index];
    }

    return {storage.data(), length};
}

}
