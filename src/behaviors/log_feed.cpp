#include "veyra/behaviors/log_feed.hpp"

#include <limits>

namespace veyra {

bool LogFeed::empty() const noexcept {
    return size_ == 0;
}

std::size_t LogFeed::size() const noexcept {
    return size_;
}

std::uint64_t LogFeed::generation() const noexcept {
    return generation_;
}

std::uint64_t LogFeed::dedup_window_ms() const noexcept {
    return dedup_window_ms_;
}

bool LogFeed::set_dedup_window_ms(std::uint64_t window_ms) noexcept {
    if (window_ms == dedup_window_ms_) {
        return false;
    }

    dedup_window_ms_ = window_ms;

    return true;
}

bool LogFeed::record_at(std::size_t index, LogFeedRecord& output) const noexcept {
    if (index >= size_) {
        return false;
    }

    output = records_[index];

    return true;
}

bool LogFeed::index_of(std::uint64_t id, std::size_t& index) const noexcept {
    if (id == 0) {
        return false;
    }

    for (auto cursor = std::size_t{}; cursor < size_; ++cursor) {
        if (records_[cursor].id == id) {
            index = cursor;

            return true;
        }
    }

    return false;
}

bool LogFeed::collapses_with(LogFeedKind kind, std::uint64_t now_ms) const noexcept {
    if (size_ == 0) {
        return false;
    }

    const auto& tail = records_[size_ - 1];

    if (tail.kind != kind) {
        return false;
    }

    if (now_ms < tail.last_ms) {
        return false;
    }

    return now_ms - tail.last_ms <= dedup_window_ms_;
}

LogFeedPush LogFeed::push(std::uint64_t id, LogFeedKind kind, std::uint64_t now_ms) noexcept {
    if (id == 0) {
        return {};
    }

    if (collapses_with(kind, now_ms)) {
        auto& tail = records_[size_ - 1];
        tail.id = id;

        if (tail.count < std::numeric_limits<std::uint32_t>::max()) {
            ++tail.count;
        }

        tail.last_ms = now_ms;
        ++generation_;

        return {
            .result = LogFeedResult::collapsed,
            .id = id,
            .count = tail.count,
        };
    }

    auto evicted = false;

    if (size_ == kCapacity) {
        for (auto cursor = std::size_t{1}; cursor < kCapacity; ++cursor) {
            records_[cursor - 1] = records_[cursor];
        }

        --size_;
        evicted = true;
    }

    records_[size_] = {
        .id = id,
        .kind = kind,
        .count = 1,
        .last_ms = now_ms,
    };
    ++size_;
    ++generation_;

    return {
        .result = LogFeedResult::appended,
        .id = id,
        .count = 1,
        .evicted_oldest = evicted,
    };
}

bool LogFeed::clear() noexcept {
    if (size_ == 0) {
        return false;
    }

    size_ = 0;
    ++generation_;

    return true;
}

}
