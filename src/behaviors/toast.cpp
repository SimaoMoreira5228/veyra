#include "veyra/behaviors/toast.hpp"

#include <limits>

namespace veyra {

ToastQueue::ToastQueue() noexcept = default;

bool ToastQueue::empty() const noexcept {
    return size_ == 0;
}

std::size_t ToastQueue::size() const noexcept {
    return size_;
}

std::uint64_t ToastQueue::now_ms() const noexcept {
    return now_ms_;
}

bool ToastQueue::contains(std::uint64_t id) const noexcept {
    return index_of(id) != kNotFound;
}

std::optional<std::uint32_t> ToastQueue::time_until_next_expiry() const noexcept {
    bool found{};

    std::uint64_t nearest{};

    for (std::size_t i = 0; i < size_; ++i) {
        if (entries_[i].sticky) {
            continue;
        }

        std::uint64_t delta{};

        if (entries_[i].expiry_ms > now_ms_) {
            delta = entries_[i].expiry_ms - now_ms_;
        }

        if (!found || delta < nearest) {
            nearest = delta;
            found = true;
        }
    }

    if (!found) {
        return std::nullopt;
    }

    if (nearest > std::numeric_limits<std::uint32_t>::max()) {
        return std::numeric_limits<std::uint32_t>::max();
    }

    return static_cast<std::uint32_t>(nearest);
}

ToastPush ToastQueue::push(
    std::uint64_t id,
    ToastKind kind,
    std::uint32_t ttl_ms,
    std::string_view body
) noexcept {
    if (id == 0) {
        return {};
    }

    if (body.empty()) {
        return {};
    }

    const bool sticky = ttl_ms == 0;

    std::uint64_t expiry{};

    if (!sticky) {
        constexpr auto kMax = std::numeric_limits<std::uint64_t>::max();

        if (ttl_ms > kMax - now_ms_) {
            expiry = kMax;
        } else {
            expiry = now_ms_ + ttl_ms;
        }
    }

    const ToastAnnouncement announcement{
        .id = id,
        .kind = kind,
        .assertive = is_assertive(kind),
    };

    const auto existing = index_of(id);

    if (existing != kNotFound) {
        entries_[existing].kind = kind;
        entries_[existing].expiry_ms = expiry;
        entries_[existing].sticky = sticky;

        return {
            .result = ToastResult::replaced,
            .announcement = announcement,
        };
    }

    if (size_ == kCapacity) {
        return {.result = ToastResult::overflow};
    }

    entries_[size_] = {
        .id = id,
        .kind = kind,
        .expiry_ms = expiry,
        .sticky = sticky,
    };
    ++size_;

    return {
        .result = ToastResult::pushed,
        .announcement = announcement,
    };
}

ToastDismiss ToastQueue::dismiss(std::uint64_t id) noexcept {
    if (id == 0) {
        return {};
    }

    const auto index = index_of(id);

    if (index == kNotFound) {
        return {};
    }

    const auto kind = entries_[index].kind;

    for (std::size_t i = index + 1; i < size_; ++i) {
        entries_[i - 1] = entries_[i];
    }
    --size_;

    return {
        .result = ToastResult::dismissed,
        .id = id,
        .kind = kind,
    };
}

ToastReset ToastQueue::dismiss_all() noexcept {
    const auto dismissed = size_;
    size_ = 0;

    return {.dismissed = dismissed};
}

ToastResult ToastQueue::advance(
    std::uint64_t now_ms,
    std::span<std::uint64_t> expired_out,
    std::size_t& expired_count
) noexcept {
    if (now_ms < now_ms_) {
        expired_count = 0;

        return ToastResult::rejected;
    }

    std::size_t due{};

    for (std::size_t i = 0; i < size_; ++i) {
        if (!entries_[i].sticky && entries_[i].expiry_ms <= now_ms) {
            ++due;
        }
    }

    if (expired_out.size() < due) {
        expired_count = due;

        return ToastResult::insufficient_output;
    }

    std::size_t kept{};

    std::size_t written{};

    for (std::size_t i = 0; i < size_; ++i) {
        if (!entries_[i].sticky && entries_[i].expiry_ms <= now_ms) {
            expired_out[written] = entries_[i].id;
            ++written;
        } else {
            entries_[kept] = entries_[i];
            ++kept;
        }
    }

    size_ = kept;
    now_ms_ = now_ms;
    expired_count = written;

    return ToastResult::complete;
}

bool ToastQueue::is_assertive(ToastKind kind) noexcept {
    return kind == ToastKind::error;
}

std::size_t ToastQueue::index_of(std::uint64_t id) const noexcept {
    for (std::size_t i = 0; i < size_; ++i) {
        if (entries_[i].id == id) {
            return i;
        }
    }

    return kNotFound;
}

}
