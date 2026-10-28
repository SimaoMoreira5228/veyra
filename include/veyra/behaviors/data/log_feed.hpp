#ifndef VEYRA_BEHAVIORS_LOG_FEED_HPP
#define VEYRA_BEHAVIORS_LOG_FEED_HPP

#include <array>
#include <cstddef>
#include <cstdint>

namespace veyra {

enum class LogFeedKind : std::uint8_t {
    info,
    warning,
    error,
};

enum class LogFeedResult : std::uint8_t {
    ignored,
    appended,
    collapsed,
};

struct LogFeedRecord {
    std::uint64_t id{};
    LogFeedKind kind{LogFeedKind::info};
    std::uint32_t count{1};
    std::uint64_t last_ms{};
};

struct LogFeedPush {
    LogFeedResult result{LogFeedResult::ignored};
    std::uint64_t id{};
    std::uint32_t count{1};
    bool evicted_oldest{};
};

class LogFeed {
  public:
    static constexpr std::size_t kCapacity = 256;
    static constexpr std::uint64_t kDefaultDedupWindowMs = 1000;

    LogFeed() noexcept = default;

    bool empty() const noexcept;
    std::size_t size() const noexcept;
    std::uint64_t generation() const noexcept;
    std::uint64_t dedup_window_ms() const noexcept;
    bool set_dedup_window_ms(std::uint64_t window_ms) noexcept;
    bool record_at(std::size_t index, LogFeedRecord& output) const noexcept;
    bool index_of(std::uint64_t id, std::size_t& index) const noexcept;
    LogFeedPush push(std::uint64_t id, LogFeedKind kind, std::uint64_t now_ms) noexcept;
    bool clear() noexcept;

  private:
    bool collapses_with(LogFeedKind kind, std::uint64_t now_ms) const noexcept;

    std::array<LogFeedRecord, kCapacity> records_{};
    std::size_t size_{};
    std::uint64_t generation_{};
    std::uint64_t dedup_window_ms_{kDefaultDedupWindowMs};
};

}

namespace veyra::behaviors {
using ::veyra::LogFeed;
using ::veyra::LogFeedKind;
using ::veyra::LogFeedPush;
using ::veyra::LogFeedRecord;
using ::veyra::LogFeedResult;
}

namespace veyra::behaviors::data {
using ::veyra::LogFeed;
using ::veyra::LogFeedKind;
using ::veyra::LogFeedPush;
using ::veyra::LogFeedRecord;
using ::veyra::LogFeedResult;
}

#endif
