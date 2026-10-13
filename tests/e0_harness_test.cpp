#include "display_list_equivalence.hpp"
#include "e0_fixed_scene.hpp"

#include "veyra/inspector/trace_recorder.hpp"
#include "veyra/render/scroll_shift.hpp"
#include "veyra/render/software_renderer.hpp"

#include <array>
#include <atomic>
#include <bit>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <new>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#if defined(__linux__)
#include <sys/resource.h>
#endif

namespace {

std::atomic<std::size_t> g_allocation_count{0};
std::atomic<bool> g_counting{false};

// NOTE: every allocation form counts while armed; deletes never count.
void note_allocation() noexcept {
    if (g_counting.load(std::memory_order_relaxed)) {
        g_allocation_count.fetch_add(1, std::memory_order_relaxed);
    }
}

void* allocate_unaligned(std::size_t size) {
    note_allocation();

    if (size == 0) {
        size = 1;
    }

    void* result = std::malloc(size);

    if (result == nullptr) {
        throw std::bad_alloc{};
    }

    return result;
}

// NOTE: manual align-up stashes the raw pointer one word below the result.
void* allocate_aligned(std::size_t size, std::size_t alignment) {
    note_allocation();

    if (alignment < alignof(void*)) {
        alignment = alignof(void*);
    }

    if (size == 0) {
        size = 1;
    }

    const auto header = sizeof(void*);

    if (size > std::numeric_limits<std::size_t>::max() - alignment - header) {
        throw std::bad_alloc{};
    }

    void* raw = std::malloc(size + alignment + header);

    if (raw == nullptr) {
        throw std::bad_alloc{};
    }

    const auto base = reinterpret_cast<std::uintptr_t>(raw) + header;
    const auto aligned = (base + alignment - 1U) & ~(alignment - 1U);
    auto* slot = reinterpret_cast<void**>(aligned - header);
    *slot = raw;

    return reinterpret_cast<void*>(aligned);
}

void release_unaligned(void* ptr) noexcept {
    std::free(ptr);
}

void release_aligned(void* ptr) noexcept {
    if (ptr == nullptr) {
        return;
    }

    const auto slot = reinterpret_cast<void**>(static_cast<char*>(ptr) - sizeof(void*));

    std::free(*slot);
}

// NOTE: arm around the steady repeat only; setup and teardown stay outside.
class Measure {
  public:
    Measure() noexcept {
        g_allocation_count.store(0, std::memory_order_relaxed);
        g_counting.store(true, std::memory_order_relaxed);
    }

    ~Measure() noexcept {
        g_counting.store(false, std::memory_order_relaxed);
    }

    Measure(const Measure&) = delete;
    Measure& operator=(const Measure&) = delete;

    std::size_t count() const noexcept {
        return g_allocation_count.load(std::memory_order_relaxed);
    }
};

void require(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

constexpr std::uint64_t kE0GoldenFrame0 = 11463938727468498053ULL;
constexpr std::uint64_t kE0GoldenFrame1 = 2775304537190214027ULL;

std::uint64_t hash_pixels(std::span<const veyra::SoftwarePixel> pixels) noexcept {
    auto hash = std::uint64_t{14695981039346656037ULL};

    for (const auto& pixel : pixels) {
        hash = veyra::e0_mix_hash(hash, pixel.red);
        hash = veyra::e0_mix_hash(hash, pixel.green);
        hash = veyra::e0_mix_hash(hash, pixel.blue);
        hash = veyra::e0_mix_hash(hash, pixel.alpha);
    }

    return hash;
}

struct E0Workspace {
    std::array<veyra::SoftwareClipRect, 2> clips{};
    std::array<veyra::LayoutPoint, 2> translations{};
    std::array<double, 16> intersections{};
    std::array<std::int8_t, 8> windings{};
};

veyra::SoftwareRenderWorkspace workspace_view(E0Workspace& rooms) noexcept {
    return veyra::SoftwareRenderWorkspace{
        .clips = rooms.clips,
        .translations = rooms.translations,
        .path_intersections = rooms.intersections,
        .path_windings = rooms.windings,
    };
}

struct E0FrameBuffers {
    std::array<veyra::SoftwarePixel, veyra::kE0Width * veyra::kE0Height> framebuffer{};
    std::array<veyra::SoftwarePixel, veyra::kE0Width * veyra::kE0Height> reference{};
    std::array<veyra::SoftwarePixel, veyra::kE0Width * veyra::kE0Height> presented{};
};

struct E0PhaseTimings {
    std::uint64_t build_ns{};
    std::uint64_t layout_ns{};
    std::uint64_t paint_ns{};
    std::uint64_t cache_ns{};
    std::uint64_t present_ns{};
};

struct E0Outcome {
    std::uint64_t command_hash[veyra::kE0FrameCount]{};
    std::size_t command_count[veyra::kE0FrameCount]{};
    std::uint64_t pixel_hash[veyra::kE0FrameCount]{};
    std::uint64_t present_checksum{};
    bool plan_has_blit{};
    std::size_t strip_count{};
    std::size_t trace_events{};
    std::size_t trace_dropped{};
    std::size_t cache_hits{};
    std::size_t cache_misses{};
    std::uint32_t damage_regions{};
    bool damage_full{};
    E0PhaseTimings timings{};
};

using E0Clock = std::chrono::steady_clock;

std::uint64_t elapsed_ns(E0Clock::time_point begin, E0Clock::time_point end) noexcept {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(end - begin).count()
    );
}

void record_frame_trace(
    veyra::TraceRecorder& recorder,
    std::size_t frame,
    std::uint32_t node_count,
    std::uint32_t damage_regions,
    bool full_surface,
    veyra::ScrollShiftPlan plan,
    std::span<const veyra::DisplayCommand> commands
) {
    const auto time = veyra::e0_frame_time_ms(frame);

    auto check = [&recorder](veyra::TraceRecordResult result, std::string_view message) {
        require(result == veyra::TraceRecordResult::recorded, message);
    };

    check(
        recorder.record_frame_begin(time, static_cast<std::uint64_t>(frame)),
        "trace opens a frame"
    );
    check(
        recorder.record_layout(time + 1, node_count, static_cast<std::uint32_t>(frame)),
        "trace records layout"
    );
    check(recorder.record_damage(time + 2, damage_regions, full_surface), "trace records damage");
    check(
        recorder
            .record_invalidation(time + 3, veyra::TraceInvalidateReason::content, veyra::NodeId{}),
        "trace records invalidation"
    );

    for (std::uint8_t row = 0; row < commands.size() && row < 16; ++row) {
        const auto retained = frame > 0 && plan.has_blit && row > 0;

        check(recorder.record_cache(time + 4, row, retained), "trace records a row cache");
    }

    check(
        recorder.record_frame_end(time + 5, static_cast<std::uint64_t>(frame)),
        "trace closes a frame"
    );
}

void count_caches(const veyra::TraceRecorder& recorder, E0Outcome& outcome) noexcept {
    for (const auto& event : recorder.events()) {
        if (event.kind != veyra::TraceEventKind::cache) {
            continue;
        }

        if (event.payload.cache.hit) {
            ++outcome.cache_hits;
        } else {
            ++outcome.cache_misses;
        }

        continue;
    }
}

E0Outcome run_pipeline(
    veyra::E0SceneStorage& first,
    veyra::E0SceneStorage& second,
    E0Workspace& rooms,
    E0FrameBuffers& buffers,
    std::span<veyra::TraceEvent, 64> trace_storage
) {
    auto outcome = E0Outcome{};
    auto recorder = veyra::TraceRecorder(trace_storage);

    recorder.set_enabled(true);

    auto plan = veyra::ScrollShiftPlan{};
    const auto shift_result = veyra::resolve_scroll_shift(
        {
            .width = veyra::kE0Width,
            .height = veyra::kE0Height,
            .delta_x = 0,
            .delta_y = static_cast<int>(veyra::kE0ScrollStep),
        },
        plan
    );

    require(shift_result == veyra::ScrollShiftResult::complete, "scroll shift resolves");
    require(plan.has_blit, "scroll keeps a blit");
    require(plan.strip_count >= 1, "scroll exposes a strip");

    outcome.plan_has_blit = plan.has_blit;
    outcome.strip_count = plan.strip_count;

    auto built = std::array<std::span<const veyra::DisplayCommand>, veyra::kE0FrameCount>{};

    {
        const auto begin = E0Clock::now();

        built[0] = veyra::e0_build_scene(veyra::kE0Seed, 0, first);
        built[1] = veyra::e0_build_scene(veyra::kE0Seed, 1, second);

        outcome.timings.build_ns = elapsed_ns(begin, E0Clock::now());
    }

    for (std::size_t frame = 0; frame < veyra::kE0FrameCount; ++frame) {
        outcome.command_count[frame] = built[frame].size();
        outcome.command_hash[frame] = veyra::e0_hash_commands(built[frame]);
    }

    {
        const auto begin = E0Clock::now();

        record_frame_trace(
            recorder,
            0,
            static_cast<std::uint32_t>(built[0].size()),
            1,
            true,
            plan,
            built[0]
        );
        record_frame_trace(
            recorder,
            1,
            static_cast<std::uint32_t>(built[1].size()),
            static_cast<std::uint32_t>(plan.strip_count),
            false,
            plan,
            built[1]
        );

        outcome.timings.layout_ns = elapsed_ns(begin, E0Clock::now());
    }

    {
        const auto begin = E0Clock::now();
        const auto workspace = workspace_view(rooms);
        const auto surface = veyra::SoftwareSurface{
            .width = veyra::kE0Width,
            .height = veyra::kE0Height,
            .stride = veyra::kE0Width,
            .pixels = buffers.framebuffer,
        };

        require(
            veyra::render_software(built[0], surface, workspace) ==
                veyra::SoftwareRenderResult::complete,
            "frame zero renders"
        );

        outcome.pixel_hash[0] = hash_pixels(buffers.framebuffer);

        const auto reference_surface = veyra::SoftwareSurface{
            .width = veyra::kE0Width,
            .height = veyra::kE0Height,
            .stride = veyra::kE0Width,
            .pixels = buffers.reference,
        };

        require(
            veyra::render_software(built[1], reference_surface, workspace) ==
                veyra::SoftwareRenderResult::complete,
            "frame one renders its reference"
        );

        outcome.pixel_hash[1] = hash_pixels(buffers.reference);

        require(
            veyra::shift_surface_pixels(
                buffers.framebuffer,
                veyra::kE0Width,
                veyra::kE0Width,
                veyra::kE0Height,
                plan.source,
                plan.destination
            ),
            "scroll blits surviving pixels"
        );

        auto region_storage = std::array<veyra::LayoutRect, 2>{};

        for (std::size_t index = 0; index < plan.strip_count; ++index) {
            region_storage[index] = veyra::LayoutRect{
                .x = static_cast<float>(plan.strips[index].left),
                .y = static_cast<float>(plan.strips[index].top),
                .width = static_cast<float>(plan.strips[index].width),
                .height = static_cast<float>(plan.strips[index].height),
            };
        }

        const auto regions =
            std::span<const veyra::LayoutRect>(region_storage.data(), plan.strip_count);
        const auto strip_surface = veyra::SoftwareSurface{
            .width = veyra::kE0Width,
            .height = veyra::kE0Height,
            .stride = veyra::kE0Width,
            .pixels = buffers.framebuffer,
        };

        require(
            veyra::render_software(built[1], strip_surface, workspace, regions) ==
                veyra::SoftwareRenderResult::complete,
            "strips repaint the exposed region"
        );

        outcome.timings.paint_ns = elapsed_ns(begin, E0Clock::now());
    }

    {
        const auto begin = E0Clock::now();

        count_caches(recorder, outcome);

        outcome.trace_events = recorder.size();
        outcome.trace_dropped = recorder.dropped();
        outcome.timings.cache_ns = elapsed_ns(begin, E0Clock::now());
    }

    for (const auto& event : recorder.events()) {
        if (event.kind != veyra::TraceEventKind::damage) {
            continue;
        }

        outcome.damage_regions = event.payload.damage.region_count;
        outcome.damage_full = event.payload.damage.full_surface;
    }

    {
        const auto begin = E0Clock::now();

        for (std::size_t index = 0; index < buffers.presented.size(); ++index) {
            buffers.presented[index] = buffers.framebuffer[index];
        }

        outcome.present_checksum = hash_pixels(buffers.presented);
        outcome.timings.present_ns = elapsed_ns(begin, E0Clock::now());
    }

    return outcome;
}

constexpr std::size_t kE0ReportSize = 2048;

struct E0Report {
    std::array<char, kE0ReportSize> bytes{};
    std::size_t length{};
};

void append_hex(E0Report& report, std::uint64_t value) {
    constexpr std::string_view kDigits = "0123456789abcdef";

    require(report.length + 16 <= report.bytes.size(), "report keeps its bounds");

    for (int shift = 60; shift >= 0; shift -= 4) {
        report.bytes[report.length] = kDigits[static_cast<std::size_t>((value >> shift) & 0xFULL)];

        ++report.length;
    }
}

void append_separator(E0Report& report) {
    require(report.length + 1 <= report.bytes.size(), "report separates fields");

    report.bytes[report.length] = '|';

    ++report.length;
}

E0Report serialize_report(const E0Outcome& outcome) {
    const std::uint64_t fields[] = {
        veyra::kE0Seed,
        outcome.command_count[0],
        outcome.command_hash[0],
        outcome.pixel_hash[0],
        outcome.command_count[1],
        outcome.command_hash[1],
        outcome.pixel_hash[1],
        outcome.plan_has_blit ? 1U : 0U,
        outcome.strip_count,
        outcome.trace_events,
        outcome.trace_dropped,
        outcome.cache_hits,
        outcome.cache_misses,
        outcome.damage_regions,
        outcome.damage_full ? 1U : 0U,
        outcome.present_checksum,
    };

    auto report = E0Report{};

    for (const auto field : fields) {
        append_hex(report, field);
        append_separator(report);
    }

    return report;
}

bool reports_equal(const E0Report& left, const E0Report& right) noexcept {
    if (left.length != right.length) {
        return false;
    }

    for (std::size_t index = 0; index < left.length; ++index) {
        if (left.bytes[index] != right.bytes[index]) {
            return false;
        }
    }

    return true;
}

std::uint64_t read_rss_bytes() noexcept {
#if defined(__linux__)
    auto usage = rusage{};

    if (getrusage(RUSAGE_SELF, &usage) != 0) {
        return 0;
    }

    return static_cast<std::uint64_t>(usage.ru_maxrss) * 1024U;
#else
    // NOTE: RSS has no portable query; other platforms report zero on purpose.
    return 0;
#endif
}

void check_determinism_gate() {
    auto first_a = veyra::E0SceneStorage{};
    auto second_a = veyra::E0SceneStorage{};
    auto rooms_a = E0Workspace{};
    auto buffers_a = E0FrameBuffers{};
    auto trace_a = std::array<veyra::TraceEvent, 64>{};

    const auto outcome_a = run_pipeline(first_a, second_a, rooms_a, buffers_a, trace_a);
    const auto report_a = serialize_report(outcome_a);

    auto first_b = veyra::E0SceneStorage{};
    auto second_b = veyra::E0SceneStorage{};
    auto rooms_b = E0Workspace{};
    auto buffers_b = E0FrameBuffers{};
    auto trace_b = std::array<veyra::TraceEvent, 64>{};

    const auto outcome_b = run_pipeline(first_b, second_b, rooms_b, buffers_b, trace_b);
    const auto report_b = serialize_report(outcome_b);

    require(
        reports_equal(report_a, report_b),
        "determinism gate: same seed reruns byte-identical reports"
    );

    std::cout << "e0 snapshot frame0=" << outcome_a.pixel_hash[0]
              << " frame1=" << outcome_a.pixel_hash[1] << '\n';
    std::cout << "e0 build_ns=" << outcome_a.timings.build_ns
              << " layout_ns=" << outcome_a.timings.layout_ns
              << " paint_ns=" << outcome_a.timings.paint_ns
              << " cache_ns=" << outcome_a.timings.cache_ns
              << " present_ns=" << outcome_a.timings.present_ns << '\n';
    std::cout << "e0 rss_bytes=" << read_rss_bytes() << '\n';
}

void check_allocation_gate() {
    auto first = veyra::E0SceneStorage{};
    auto second = veyra::E0SceneStorage{};
    auto rooms = E0Workspace{};
    auto buffers = E0FrameBuffers{};
    auto trace_storage = std::array<veyra::TraceEvent, 64>{};

    run_pipeline(first, second, rooms, buffers, trace_storage);

    auto allocations = std::size_t{0};

    {
        auto measure = Measure{};

        for (auto repeat = 0; repeat < 60; ++repeat) {
            const auto outcome = run_pipeline(first, second, rooms, buffers, trace_storage);

            require(
                outcome.trace_dropped == 0,
                "allocation gate: steady frames drop no trace events"
            );
        }

        allocations = measure.count();
    }

    require(allocations == 0, "allocation gate: steady frames allocate nothing");
}

void check_equivalence_gate() {
    auto first = veyra::E0SceneStorage{};
    auto second = veyra::E0SceneStorage{};
    auto rooms = E0Workspace{};
    auto buffers = E0FrameBuffers{};
    auto trace_storage = std::array<veyra::TraceEvent, 64>{};

    const auto outcome = run_pipeline(first, second, rooms, buffers, trace_storage);

    auto rebuilt = veyra::E0SceneStorage{};
    const auto frame_zero = veyra::e0_build_scene(veyra::kE0Seed, 0, first);
    const auto frame_zero_again = veyra::e0_build_scene(veyra::kE0Seed, 0, rebuilt);
    const auto frame_one = veyra::e0_build_scene(veyra::kE0Seed, 1, second);

    auto copy_storage = std::array<veyra::DisplayCommand, veyra::kE0MaxCommands>{};

    for (std::size_t index = 0; index < frame_zero.size(); ++index) {
        copy_storage[index] = frame_zero[index];
    }

    const auto copied =
        std::span<const veyra::DisplayCommand>(copy_storage.data(), frame_zero.size());

    auto first_different = std::size_t{0};

    require(
        veyra::equivalent_display_lists(frame_zero, copied, first_different),
        "equivalence gate: rebuilt lists match across every command type"
    );
    require(
        !veyra::equivalent_display_lists(frame_zero, frame_one, first_different),
        "equivalence gate: scrolled frame differs from the full frame"
    );

    auto direct = std::array<veyra::SoftwarePixel, veyra::kE0Width * veyra::kE0Height>{};
    const auto workspace = workspace_view(rooms);
    const auto direct_surface = veyra::SoftwareSurface{
        .width = veyra::kE0Width,
        .height = veyra::kE0Height,
        .stride = veyra::kE0Width,
        .pixels = direct,
    };

    require(
        veyra::render_software(frame_one, direct_surface, workspace) ==
            veyra::SoftwareRenderResult::complete,
        "equivalence gate: full redraw renders"
    );

    require(
        hash_pixels(direct) == outcome.pixel_hash[1],
        "equivalence gate: blit plus strips equal a full redraw"
    );
    require(
        veyra::e0_hash_commands(frame_zero) == veyra::e0_hash_commands(frame_zero_again),
        "equivalence gate: same seed rebuilds identical commands"
    );
}

void check_snapshot_gate() {
    auto first = veyra::E0SceneStorage{};
    auto second = veyra::E0SceneStorage{};
    auto rooms = E0Workspace{};
    auto buffers = E0FrameBuffers{};
    auto trace_storage = std::array<veyra::TraceEvent, 64>{};

    const auto outcome = run_pipeline(first, second, rooms, buffers, trace_storage);

    require(
        outcome.pixel_hash[0] == kE0GoldenFrame0,
        "snapshot gate: frame zero pixels match the golden hash"
    );
    require(
        outcome.pixel_hash[1] == kE0GoldenFrame1,
        "snapshot gate: frame one pixels match the golden hash"
    );
}

}

void* operator new(std::size_t size) {
    return allocate_unaligned(size);
}

void* operator new[](std::size_t size) {
    return allocate_unaligned(size);
}

void operator delete(void* ptr) noexcept {
    release_unaligned(ptr);
}

void operator delete[](void* ptr) noexcept {
    release_unaligned(ptr);
}

void operator delete(void* ptr, std::size_t) noexcept {
    release_unaligned(ptr);
}

void operator delete[](void* ptr, std::size_t) noexcept {
    release_unaligned(ptr);
}

void* operator new(std::size_t size, std::align_val_t alignment) {
    return allocate_aligned(size, static_cast<std::size_t>(alignment));
}

void* operator new[](std::size_t size, std::align_val_t alignment) {
    return allocate_aligned(size, static_cast<std::size_t>(alignment));
}

void operator delete(void* ptr, std::align_val_t) noexcept {
    release_aligned(ptr);
}

void operator delete[](void* ptr, std::align_val_t) noexcept {
    release_aligned(ptr);
}

void operator delete(void* ptr, std::size_t, std::align_val_t) noexcept {
    release_aligned(ptr);
}

void operator delete[](void* ptr, std::size_t, std::align_val_t) noexcept {
    release_aligned(ptr);
}

void* operator new(std::size_t size, const std::nothrow_t&) noexcept {
    note_allocation();

    if (size == 0) {
        size = 1;
    }

    return std::malloc(size);
}

void* operator new[](std::size_t size, const std::nothrow_t&) noexcept {
    note_allocation();

    if (size == 0) {
        size = 1;
    }

    return std::malloc(size);
}

void operator delete(void* ptr, const std::nothrow_t&) noexcept {
    release_unaligned(ptr);
}

void operator delete[](void* ptr, const std::nothrow_t&) noexcept {
    release_unaligned(ptr);
}

int main() {
    try {
        check_determinism_gate();
        check_allocation_gate();
        check_equivalence_gate();
        check_snapshot_gate();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
