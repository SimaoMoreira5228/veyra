#ifndef VEYRA_INSPECTOR_TRACE_RECORDER_HPP
#define VEYRA_INSPECTOR_TRACE_RECORDER_HPP

#include "veyra/core/node_id_pool.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

namespace veyra {

enum class TraceEventKind : std::uint8_t {
    frame_begin,
    frame_end,
    layout,
    damage,
    invalidation,
    cache,
    focus,
};

enum class TraceInvalidateReason : std::uint8_t {
    resize,
    scale,
    theme,
    state_change,
    content,
};

struct TraceFramePayload {
    std::uint64_t frame{};
};

struct TraceLayoutPayload {
    std::uint32_t node_count{};
    std::uint32_t generation{};
};

struct TraceDamagePayload {
    std::uint32_t region_count{};
    bool full_surface{};
};

struct TraceInvalidationPayload {
    TraceInvalidateReason reason{TraceInvalidateReason::content};
    NodeId node{};
};

struct TraceCachePayload {
    std::uint8_t cache_id{};
    bool hit{};
};

struct TraceFocusPayload {
    NodeId from{};
    NodeId to{};
};

struct TraceEvent {
    TraceEventKind kind{TraceEventKind::frame_begin};
    std::uint64_t time_ms{};

    union Payload {
        TraceFramePayload frame;
        TraceLayoutPayload layout;
        TraceDamagePayload damage;
        TraceInvalidationPayload invalidation;
        TraceCachePayload cache;
        TraceFocusPayload focus;

        constexpr Payload() noexcept : frame{} {
        }
    } payload{};
};

enum class TraceRecordResult : std::uint8_t {
    recorded,
    disabled,
    invalid_input,
};

enum class TraceReportResult : std::uint8_t {
    complete,
    insufficient_output,
    invalid_input,
};

class TraceRecorder {
  public:
    explicit TraceRecorder(std::span<TraceEvent> storage) noexcept;

    TraceRecorder(const TraceRecorder&) = delete;
    TraceRecorder& operator=(const TraceRecorder&) = delete;

    bool valid() const noexcept;
    bool enabled() const noexcept;
    void set_enabled(bool enabled) noexcept;
    void clear() noexcept;
    std::size_t capacity() const noexcept;
    std::size_t size() const noexcept;
    std::size_t dropped() const noexcept;
    std::size_t count(TraceEventKind kind) const noexcept;
    std::span<const TraceEvent> events() const noexcept;

    TraceRecordResult record_frame_begin(std::uint64_t time_ms, std::uint64_t frame) noexcept;
    TraceRecordResult record_frame_end(std::uint64_t time_ms, std::uint64_t frame) noexcept;

    TraceRecordResult record_layout(
        std::uint64_t time_ms,
        std::uint32_t node_count,
        std::uint32_t generation
    ) noexcept;

    TraceRecordResult
    record_damage(std::uint64_t time_ms, std::uint32_t region_count, bool full_surface) noexcept;

    TraceRecordResult
    record_invalidation(std::uint64_t time_ms, TraceInvalidateReason reason, NodeId node) noexcept;

    TraceRecordResult record_cache(std::uint64_t time_ms, std::uint8_t cache_id, bool hit) noexcept;
    TraceRecordResult record_focus(std::uint64_t time_ms, NodeId from, NodeId to) noexcept;

    TraceReportResult write_report(std::span<char> output, std::size_t& needed) const noexcept;

  private:
    std::span<TraceEvent> storage_;
    std::size_t size_{};
    std::size_t dropped_{};
    bool enabled_{};

    TraceRecordResult push(TraceEvent event) noexcept;
    static bool valid_reason(TraceInvalidateReason reason) noexcept;
};

}
#endif
