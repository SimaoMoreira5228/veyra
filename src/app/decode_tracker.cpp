#include "veyra/app/decode_tracker.hpp"

namespace veyra::app {

bool DecodeTracker::begin(WindowId window, std::uint64_t request) noexcept {
    if (window == kInvalidWindowId || request == 0 || find(request) != nullptr) {
        return false;
    }

    for (auto& entry : entries_) {
        if (entry.active && entry.state != DecodeState::discarded) {
            continue;
        }
        entry = {
            .window = window,
            .request = request,
            .state = DecodeState::pending,
            .active = true,
        };
        return true;
    }
    return false;
}

bool DecodeTracker::complete(std::uint64_t request) noexcept {
    auto* entry = find(request);
    if (entry == nullptr || entry->state != DecodeState::pending) {
        return false;
    }

    entry->state = DecodeState::ready;
    return true;
}

bool DecodeTracker::take(std::uint64_t request) noexcept {
    auto* entry = find(request);
    if (entry == nullptr) {
        return false;
    }

    const auto ready = entry->state == DecodeState::ready;
    *entry = {};
    return ready;
}

bool DecodeTracker::discarded(std::uint64_t request) const noexcept {
    const auto* entry = find(request);
    return entry != nullptr && entry->state == DecodeState::discarded;
}

void DecodeTracker::discard_window(WindowId window) noexcept {
    for (auto& entry : entries_) {
        if (entry.active && entry.window == window) {
            entry.state = DecodeState::discarded;
        }
    }
}

std::size_t DecodeTracker::pending() const noexcept {
    auto count = std::size_t{};
    for (const auto& entry : entries_) {
        if (entry.active && entry.state == DecodeState::pending) {
            ++count;
        }
    }
    return count;
}

DecodeEntry* DecodeTracker::find(std::uint64_t request) noexcept {
    if (request == 0) {
        return nullptr;
    }
    for (auto& entry : entries_) {
        if (entry.active && entry.request == request) {
            return &entry;
        }
    }
    return nullptr;
}

const DecodeEntry* DecodeTracker::find(std::uint64_t request) const noexcept {
    if (request == 0) {
        return nullptr;
    }
    for (const auto& entry : entries_) {
        if (entry.active && entry.request == request) {
            return &entry;
        }
    }
    return nullptr;
}

}
