#include "veyra/inspector/trace_recorder.hpp"

#include <charconv>
#include <string_view>
#include <system_error>

namespace veyra {

namespace {

constexpr std::size_t kKindCount = 7;

constexpr std::string_view kKindLabels[kKindCount] = {
    "frame_begin",
    "frame_end",
    "layout",
    "damage",
    "invalidation",
    "cache",
    "focus",
};

constexpr std::string_view kReportHead = "trace v1 events=";
constexpr std::string_view kReportDropped = " dropped=";
constexpr std::string_view kReportFrameStats = "frame_time_ms ";
constexpr std::string_view kReportNone = "none";

struct ReportWriter {
    char* at;
    char* end;

    void put(char value) noexcept {
        if (at == end) {
            return;
        }

        *at = value;
        ++at;
    }

    void put_text(std::string_view text) noexcept {
        for (const char value : text) {
            put(value);
        }
    }

    void put_number(std::uint64_t value) noexcept {
        char digits[20]{};

        const auto result = std::to_chars(digits, digits + sizeof(digits), value);

        if (result.ec != std::errc{}) {
            return;
        }

        put_text({digits, static_cast<std::size_t>(result.ptr - digits)});
    }
};

std::size_t decimal_length(std::uint64_t value) noexcept {
    std::size_t length = 1;

    while (value >= 10) {
        value /= 10;
        ++length;
    }

    return length;
}

}

TraceRecorder::TraceRecorder(std::span<TraceEvent> storage) noexcept : storage_(storage) {
}

bool TraceRecorder::valid() const noexcept {
    return !storage_.empty();
}

bool TraceRecorder::enabled() const noexcept {
    return enabled_;
}

void TraceRecorder::set_enabled(bool enabled) noexcept {
    enabled_ = enabled;
}

void TraceRecorder::clear() noexcept {
    size_ = 0;
    dropped_ = 0;
}

std::size_t TraceRecorder::capacity() const noexcept {
    return storage_.size();
}

std::size_t TraceRecorder::size() const noexcept {
    return size_;
}

std::size_t TraceRecorder::dropped() const noexcept {
    return dropped_;
}

std::size_t TraceRecorder::count(TraceEventKind kind) const noexcept {
    const auto index = static_cast<std::size_t>(kind);

    if (index >= kKindCount) {
        return 0;
    }

    std::size_t total{};

    for (std::size_t position = 0; position < size_; ++position) {
        if (storage_[position].kind == kind) {
            ++total;
        }
    }

    return total;
}

std::span<const TraceEvent> TraceRecorder::events() const noexcept {
    if (!valid()) {
        return {};
    }

    return {storage_.data(), size_};
}

TraceRecordResult
TraceRecorder::record_frame_begin(std::uint64_t time_ms, std::uint64_t frame) noexcept {
    TraceEvent event{};
    event.kind = TraceEventKind::frame_begin;
    event.time_ms = time_ms;
    event.payload.frame.frame = frame;

    return push(event);
}

TraceRecordResult
TraceRecorder::record_frame_end(std::uint64_t time_ms, std::uint64_t frame) noexcept {
    TraceEvent event{};
    event.kind = TraceEventKind::frame_end;
    event.time_ms = time_ms;
    event.payload.frame.frame = frame;

    return push(event);
}

TraceRecordResult TraceRecorder::record_layout(
    std::uint64_t time_ms,
    std::uint32_t node_count,
    std::uint32_t generation
) noexcept {
    TraceEvent event{};
    event.kind = TraceEventKind::layout;
    event.time_ms = time_ms;
    event.payload.layout.node_count = node_count;
    event.payload.layout.generation = generation;

    return push(event);
}

TraceRecordResult TraceRecorder::record_damage(
    std::uint64_t time_ms,
    std::uint32_t region_count,
    bool full_surface
) noexcept {
    TraceEvent event{};
    event.kind = TraceEventKind::damage;
    event.time_ms = time_ms;
    event.payload.damage.region_count = region_count;
    event.payload.damage.full_surface = full_surface;

    return push(event);
}

TraceRecordResult TraceRecorder::record_invalidation(
    std::uint64_t time_ms,
    TraceInvalidateReason reason,
    NodeId node
) noexcept {
    if (!valid_reason(reason)) {
        return TraceRecordResult::invalid_input;
    }

    TraceEvent event{};
    event.kind = TraceEventKind::invalidation;
    event.time_ms = time_ms;
    event.payload.invalidation.reason = reason;
    event.payload.invalidation.node = node;

    return push(event);
}

TraceRecordResult
TraceRecorder::record_cache(std::uint64_t time_ms, std::uint8_t cache_id, bool hit) noexcept {
    TraceEvent event{};
    event.kind = TraceEventKind::cache;
    event.time_ms = time_ms;
    event.payload.cache.cache_id = cache_id;
    event.payload.cache.hit = hit;

    return push(event);
}

TraceRecordResult
TraceRecorder::record_focus(std::uint64_t time_ms, NodeId from, NodeId to) noexcept {
    TraceEvent event{};
    event.kind = TraceEventKind::focus;
    event.time_ms = time_ms;
    event.payload.focus.from = from;
    event.payload.focus.to = to;

    return push(event);
}

TraceReportResult
TraceRecorder::write_report(std::span<char> output, std::size_t& needed) const noexcept {
    if (!valid()) {
        needed = 0;

        return TraceReportResult::invalid_input;
    }

    std::size_t per_kind[kKindCount]{};

    for (std::size_t position = 0; position < size_; ++position) {
        const auto index = static_cast<std::size_t>(storage_[position].kind);

        if (index < kKindCount) {
            ++per_kind[index];
        }
    }

    bool pending{};
    std::uint64_t pending_frame{};
    std::uint64_t pending_time{};
    std::uint64_t frame_min{};
    std::uint64_t frame_max{};
    std::uint64_t frame_sum{};
    std::uint64_t frame_samples{};

    for (std::size_t position = 0; position < size_; ++position) {
        const TraceEvent& event = storage_[position];

        if (event.kind == TraceEventKind::frame_begin) {
            pending = true;
            pending_frame = event.payload.frame.frame;
            pending_time = event.time_ms;

            continue;
        }

        if (event.kind != TraceEventKind::frame_end) {
            continue;
        }

        if (!pending || event.payload.frame.frame != pending_frame ||
            event.time_ms < pending_time) {
            continue;
        }

        const std::uint64_t duration = event.time_ms - pending_time;
        pending = false;

        if (frame_samples == 0 || duration < frame_min) {
            frame_min = duration;
        }

        if (frame_samples == 0 || duration > frame_max) {
            frame_max = duration;
        }

        frame_sum += duration;
        ++frame_samples;
    }

    std::size_t required = kReportHead.size() + decimal_length(size_);
    required += kReportDropped.size() + decimal_length(dropped_) + 1;

    for (std::size_t index = 0; index < kKindCount; ++index) {
        if (index > 0) {
            required += 1;
        }

        required += kKindLabels[index].size() + 1 + decimal_length(per_kind[index]);
    }

    required += 1 + kReportFrameStats.size();

    if (frame_samples == 0) {
        required += kReportNone.size() + 1;
    } else {
        required += 4 + decimal_length(frame_min) + 5 + decimal_length(frame_max) + 5 +
            decimal_length(frame_sum / frame_samples) + 1;
    }

    needed = required;

    if (output.size() < required) {
        return TraceReportResult::insufficient_output;
    }

    ReportWriter writer{output.data(), output.data() + output.size()};

    writer.put_text(kReportHead);
    writer.put_number(size_);
    writer.put_text(kReportDropped);
    writer.put_number(dropped_);
    writer.put('\n');

    for (std::size_t index = 0; index < kKindCount; ++index) {
        if (index > 0) {
            writer.put(' ');
        }

        writer.put_text(kKindLabels[index]);
        writer.put('=');
        writer.put_number(per_kind[index]);
    }

    writer.put('\n');
    writer.put_text(kReportFrameStats);

    if (frame_samples == 0) {
        writer.put_text(kReportNone);
        writer.put('\n');
    } else {
        writer.put_text("min=");
        writer.put_number(frame_min);
        writer.put_text(" max=");
        writer.put_number(frame_max);
        writer.put_text(" avg=");
        writer.put_number(frame_sum / frame_samples);
        writer.put('\n');
    }

    needed = static_cast<std::size_t>(writer.at - output.data());

    return TraceReportResult::complete;
}

TraceRecordResult TraceRecorder::push(TraceEvent event) noexcept {
    if (!valid()) {
        return TraceRecordResult::invalid_input;
    }

    if (!enabled_) {
        return TraceRecordResult::disabled;
    }

    if (size_ < storage_.size()) {
        storage_[size_] = event;
        ++size_;

        return TraceRecordResult::recorded;
    }

    for (std::size_t position = 1; position < size_; ++position) {
        storage_[position - 1] = storage_[position];
    }

    storage_[size_ - 1] = event;
    ++dropped_;

    return TraceRecordResult::recorded;
}

bool TraceRecorder::valid_reason(TraceInvalidateReason reason) noexcept {
    switch (reason) {
    case TraceInvalidateReason::resize:
    case TraceInvalidateReason::scale:
    case TraceInvalidateReason::theme:
    case TraceInvalidateReason::state_change:
    case TraceInvalidateReason::content:
        return true;
    }

    return false;
}

}
