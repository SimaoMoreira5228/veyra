#ifndef VEYRA_BEHAVIORS_TOAST_HPP
#define VEYRA_BEHAVIORS_TOAST_HPP

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace veyra {

enum class ToastKind : std::uint8_t {
    info,
    warning,
    error,
};

enum class ToastResult : std::uint8_t {
    ignored,
    pushed,
    replaced,
    dismissed,
    overflow,
    rejected,
    complete,
    insufficient_output,
};

struct ToastAnnouncement {
    std::uint64_t id{};
    ToastKind kind{ToastKind::info};
    bool assertive{};
};

struct ToastPush {
    ToastResult result{ToastResult::ignored};
    ToastAnnouncement announcement{};
};

struct ToastDismiss {
    ToastResult result{ToastResult::ignored};
    std::uint64_t id{};
    ToastKind kind{ToastKind::info};
};

struct ToastReset {
    std::size_t dismissed{};
};

class ToastQueue {
  public:
    static constexpr std::size_t kCapacity = 6;

    ToastQueue() noexcept;

    bool empty() const noexcept;
    std::size_t size() const noexcept;
    std::uint64_t now_ms() const noexcept;
    bool contains(std::uint64_t id) const noexcept;
    std::optional<std::uint32_t> time_until_next_expiry() const noexcept;

    ToastPush
    push(std::uint64_t id, ToastKind kind, std::uint32_t ttl_ms, std::string_view body) noexcept;
    ToastDismiss dismiss(std::uint64_t id) noexcept;
    ToastReset dismiss_all() noexcept;
    ToastResult advance(
        std::uint64_t now_ms,
        std::span<std::uint64_t> expired_out,
        std::size_t& expired_count
    ) noexcept;

  private:
    struct ToastEntry {
        std::uint64_t id{};
        ToastKind kind{ToastKind::info};
        std::uint64_t expiry_ms{};
        bool sticky{};
    };

    static constexpr std::size_t kNotFound = static_cast<std::size_t>(-1);

    static bool is_assertive(ToastKind kind) noexcept;
    std::size_t index_of(std::uint64_t id) const noexcept;

    std::array<ToastEntry, kCapacity> entries_{};
    std::size_t size_{};
    std::uint64_t now_ms_{};
};

}

namespace veyra::behaviors {
using ::veyra::ToastAnnouncement;
using ::veyra::ToastDismiss;
using ::veyra::ToastKind;
using ::veyra::ToastPush;
using ::veyra::ToastQueue;
using ::veyra::ToastReset;
using ::veyra::ToastResult;
}

namespace veyra::behaviors::overlay {
using ::veyra::ToastAnnouncement;
using ::veyra::ToastDismiss;
using ::veyra::ToastKind;
using ::veyra::ToastPush;
using ::veyra::ToastQueue;
using ::veyra::ToastReset;
using ::veyra::ToastResult;
}

#endif
