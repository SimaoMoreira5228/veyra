#include "veyra/inspector/trace/trace_recorder.hpp"

#include <array>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

constexpr std::string_view kGolden = "trace v1 events=8 dropped=0\n"
                                     "frame_begin=1 frame_end=1 layout=1 damage=1 invalidation=1 "
                                     "cache=2 focus=1 route=0 provenance=0\n"
                                     "frame_time_ms min=7 max=7 avg=7\n";

constexpr std::string_view kGoldenStats = "trace v1 events=4 dropped=0\n"
                                          "frame_begin=2 frame_end=2 layout=0 damage=0 "
                                          "invalidation=0 cache=0 focus=0 route=0 provenance=0\n"
                                          "frame_time_ms min=4 max=7 avg=5\n";

constexpr std::string_view kGoldenBackward = "trace v1 events=2 dropped=0\n"
                                             "frame_begin=1 frame_end=1 layout=0 damage=0 "
                                             "invalidation=0 cache=0 focus=0 route=0 provenance=0\n"
                                             "frame_time_ms none\n";

void require(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

void require_recorded(veyra::TraceRecordResult result, std::string_view message) {
    require(result == veyra::TraceRecordResult::recorded, message);
}

void fill_golden(veyra::TraceRecorder& recorder) {
    require(recorder.valid(), "trace workspace is valid");
    require(!recorder.enabled(), "trace recorder starts disabled");

    recorder.set_enabled(true);
    require(recorder.enabled(), "trace recorder enables");

    require_recorded(recorder.record_frame_begin(10, 1), "frame begin records");
    require_recorded(recorder.record_layout(11, 4, 2), "layout records");
    require_recorded(recorder.record_damage(12, 2, false), "damage records");

    const veyra::NodeId changed{.index = 2, .generation = 1};
    require_recorded(
        recorder.record_invalidation(13, veyra::TraceInvalidateReason::content, changed),
        "invalidation records"
    );
    require_recorded(recorder.record_cache(14, 3, true), "cache hit records");
    require_recorded(recorder.record_cache(15, 3, false), "cache miss records");

    const veyra::NodeId from{.index = 1, .generation = 1};
    const veyra::NodeId to{.index = 2, .generation = 1};
    require_recorded(recorder.record_focus(16, from, to), "focus move records");
    require_recorded(recorder.record_frame_end(17, 1), "frame end records");
}

void check_record_and_counts() {
    std::array<veyra::TraceEvent, 16> storage{};
    veyra::TraceRecorder recorder(storage);
    fill_golden(recorder);

    require(recorder.size() == 8, "fixed sequence stores eight events");
    require(recorder.dropped() == 0, "roomy buffer drops nothing");
    require(recorder.capacity() == 16, "capacity reports caller storage");
    require(recorder.count(veyra::TraceEventKind::cache) == 2, "cache kind counts both outcomes");
    require(recorder.count(veyra::TraceEventKind::focus) == 1, "focus kind counts the move");
    require(recorder.count(veyra::TraceEventKind::layout) == 1, "layout kind counts the pass");
    require(recorder.events().size() == 8, "event span exposes stored order");
    require(
        recorder.events()[0].kind == veyra::TraceEventKind::frame_begin &&
            recorder.events()[7].kind == veyra::TraceEventKind::frame_end,
        "stored events keep record order"
    );
    require(
        recorder.events()[4].payload.cache.cache_id == 3 && recorder.events()[4].payload.cache.hit,
        "cache payload keeps id and hit flag"
    );
    require(
        recorder.events()[6].payload.focus.from.index == 1 &&
            recorder.events()[6].payload.focus.to.index == 2,
        "focus payload keeps from and to ids"
    );
}

void check_report_golden() {
    std::array<veyra::TraceEvent, 16> storage{};
    veyra::TraceRecorder recorder(storage);
    fill_golden(recorder);

    std::array<char, 256> output{};
    std::size_t needed{};

    require(
        recorder.write_report(output, needed) == veyra::TraceReportResult::complete,
        "small report completes"
    );
    require(needed == kGolden.size(), "needed matches golden length");
    require(std::string_view(output.data(), needed) == kGolden, "report matches golden output");
}

void check_frame_stats_and_backward_time() {
    std::array<veyra::TraceEvent, 8> storage{};
    veyra::TraceRecorder recorder(storage);
    recorder.set_enabled(true);

    require_recorded(recorder.record_frame_begin(10, 1), "first frame begin records");
    require_recorded(recorder.record_frame_end(17, 1), "first frame end records");
    require_recorded(recorder.record_frame_begin(20, 2), "second frame begin records");
    require_recorded(recorder.record_frame_end(24, 2), "second frame end records");

    std::array<char, 256> output{};
    std::size_t needed{};
    require(
        recorder.write_report(output, needed) == veyra::TraceReportResult::complete,
        "stats report completes"
    );
    require(std::string_view(output.data(), needed) == kGoldenStats, "min max avg match samples");

    std::array<veyra::TraceEvent, 4> backward_storage{};
    veyra::TraceRecorder backward(backward_storage);
    backward.set_enabled(true);

    require_recorded(backward.record_frame_begin(20, 1), "backward begin records");
    require_recorded(backward.record_frame_end(10, 1), "backward end records");

    std::array<char, 256> backward_output{};
    std::size_t backward_needed{};
    require(
        backward.write_report(backward_output, backward_needed) ==
            veyra::TraceReportResult::complete,
        "backward report completes"
    );
    require(
        std::string_view(backward_output.data(), backward_needed) == kGoldenBackward,
        "backward frame pair yields no sample"
    );
}

void check_overflow_keeps_tail() {
    std::array<veyra::TraceEvent, 3> storage{};
    veyra::TraceRecorder recorder(storage);
    recorder.set_enabled(true);

    for (std::uint64_t frame = 1; frame <= 5; ++frame) {
        require_recorded(recorder.record_frame_begin(frame, frame), "overflow begin records");
    }

    require(recorder.size() == 3, "overflow keeps buffer full");
    require(recorder.dropped() == 2, "overflow counts both evicted events");
    require(
        recorder.events()[0].payload.frame.frame == 3 &&
            recorder.events()[2].payload.frame.frame == 5,
        "overflow keeps the newest tail in order"
    );
    require(recorder.count(veyra::TraceEventKind::frame_begin) == 3, "counts cover stored tail");
}

void check_truncation_and_retry() {
    std::array<veyra::TraceEvent, 16> storage{};
    veyra::TraceRecorder recorder(storage);
    fill_golden(recorder);

    std::array<char, 10> small{};
    std::size_t needed{};

    require(
        recorder.write_report(small, needed) == veyra::TraceReportResult::insufficient_output,
        "tiny buffer reports insufficiency"
    );
    require(needed == kGolden.size(), "insufficient write still reports needed size");

    std::array<char, kGolden.size()> exact{};
    std::size_t retry_needed{};

    require(
        recorder.write_report(exact, retry_needed) == veyra::TraceReportResult::complete,
        "exact-size retry completes"
    );
    require(retry_needed == kGolden.size(), "retry needed matches golden length");
    require(
        std::string_view(exact.data(), retry_needed) == kGolden,
        "retry output matches golden output"
    );
}

void check_disabled_and_invalid() {
    std::array<veyra::TraceEvent, 4> storage{};
    veyra::TraceRecorder recorder(storage);

    require(
        recorder.record_frame_begin(1, 1) == veyra::TraceRecordResult::disabled,
        "disabled recorder stores nothing"
    );
    require(recorder.size() == 0 && recorder.dropped() == 0, "disabled records leave no trace");

    recorder.set_enabled(true);
    require_recorded(recorder.record_frame_begin(1, 1), "enabled recorder stores");
    require(recorder.size() == 1, "one enabled record stores one event");

    recorder.set_enabled(false);
    require(
        recorder.record_frame_end(2, 1) == veyra::TraceRecordResult::disabled,
        "re-disabled recorder stores nothing"
    );
    require(recorder.size() == 1, "re-disable keeps earlier events");

    const std::span<veyra::TraceEvent> no_storage;
    veyra::TraceRecorder invalid(no_storage);
    require(!invalid.valid(), "empty workspace is rejected");
    require(
        invalid.record_frame_begin(1, 1) == veyra::TraceRecordResult::invalid_input,
        "invalid recorder rejects records"
    );
    require(
        invalid.record_invalidation(1, veyra::TraceInvalidateReason::content, veyra::NodeId{}) ==
            veyra::TraceRecordResult::invalid_input,
        "invalid recorder rejects invalidations"
    );

    std::array<char, 32> output{};
    std::size_t needed{999};
    require(
        invalid.write_report(output, needed) == veyra::TraceReportResult::invalid_input,
        "invalid recorder rejects reports"
    );
    require(needed == 0, "invalid report clears needed");

    require(
        recorder.record_invalidation(
            3,
            static_cast<veyra::TraceInvalidateReason>(9),
            veyra::NodeId{}
        ) == veyra::TraceRecordResult::invalid_input,
        "unknown reason is rejected"
    );
    require(recorder.size() == 1, "rejected reason stores nothing");
}

void check_route_and_provenance() {
    std::array<veyra::TraceEvent, 4> storage{};
    veyra::TraceRecorder recorder(storage);
    recorder.set_enabled(true);

    require_recorded(recorder.record_route(10, 6, 0, 42, 640, 480), "route event records");
    require_recorded(recorder.record_provenance(11, 0, 2, 2, 9), "provenance event records");
    require(recorder.count(veyra::TraceEventKind::route) == 1, "route kind counts");
    require(recorder.count(veyra::TraceEventKind::provenance) == 1, "provenance kind counts");

    const auto events = recorder.events();
    require(events[0].payload.route.event_kind == 6, "route keeps the event kind");
    require(events[0].payload.route.route_result == 0, "route keeps the result");
    require(events[0].payload.route.window == 42, "route keeps the window");
    require(events[0].payload.route.data1 == 640, "route keeps the first payload word");
    require(events[0].payload.route.data2 == 480, "route keeps the second payload word");
    require(events[1].payload.provenance.token_kind == 0, "provenance keeps the token kind");
    require(events[1].payload.provenance.token == 2, "provenance keeps the token");
    require(events[1].payload.provenance.layer == 2, "provenance keeps the layer");
    require(events[1].payload.provenance.line == 9, "provenance keeps the line");
}

}

int main() {
    try {
        check_record_and_counts();
        check_report_golden();
        check_frame_stats_and_backward_time();
        check_overflow_keeps_tail();
        check_truncation_and_retry();
        check_disabled_and_invalid();
        check_route_and_provenance();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
