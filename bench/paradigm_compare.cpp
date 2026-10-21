#include "veyra/chat/row_height_cache.hpp"
#include "veyra/engine.hpp"
#include "veyra/files/file_list.hpp"
#include "veyra/layout/virtual_list.hpp"
#include "veyra/text/text_editor_buffer.hpp"

#include "../tests/allocation_counter.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <memory>
#include <optional>
#include <random>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;

struct Options {
    std::uint32_t seed{42};
    std::size_t frames{200};
    std::size_t warmup{20};
    std::size_t chat_rows{2048};
    std::size_t arrivals{8};
    std::size_t file_entries{4096};
    std::size_t editor_bytes{4096};
    std::size_t editor_ops{64};
    std::size_t visible{60};
};

struct EngineResult {
    std::vector<double> adapter;
    std::vector<double> engine;
    std::uint64_t checksum{};
    std::uint64_t equiv{0xCBF29CE484222325ULL};
    std::size_t allocs{};
};

struct ChatFixture {
    std::vector<float> heights;
    std::vector<std::uint32_t> rows;
    std::vector<float> arrival_heights;
};

struct FileFixture {
    std::vector<std::u8string> names;
    std::vector<veyra::FileListEntry> entries;
};

struct EditorFixture {
    std::vector<char> initial;
    std::vector<std::uint8_t> script;
};

std::size_t parse_size(const char* value) {
    const std::string_view text(value);
    std::size_t parsed = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), parsed);
    if (error != std::errc{} || end != text.data() + text.size()) {
        throw std::invalid_argument("expected a non-negative integer: " + std::string(text));
    }
    return parsed;
}

Options parse_options(int argc, char** argv) {
    auto options = Options{};
    for (auto index = 1; index < argc; ++index) {
        const std::string_view argument(argv[index]);
        if (index + 1 >= argc) {
            throw std::invalid_argument("missing value for " + std::string(argument));
        }

        const auto value = parse_size(argv[++index]);
        if (argument == "--seed") {
            if (value > UINT32_MAX) {
                throw std::invalid_argument("seed must fit in a 32-bit unsigned integer");
            }
            options.seed = static_cast<std::uint32_t>(value);
        } else if (argument == "--frames") {
            options.frames = value;
        } else if (argument == "--warmup") {
            options.warmup = value;
        } else if (argument == "--chat-rows") {
            options.chat_rows = value;
        } else if (argument == "--arrivals") {
            options.arrivals = value;
        } else if (argument == "--file-entries") {
            options.file_entries = value;
        } else if (argument == "--editor-bytes") {
            options.editor_bytes = value;
        } else if (argument == "--editor-ops") {
            options.editor_ops = value;
        } else if (argument == "--visible") {
            options.visible = value;
        } else {
            throw std::invalid_argument("unknown option: " + std::string(argument));
        }
    }

    if (options.frames == 0 || options.frames > 10'000 || options.warmup > 10'000) {
        throw std::invalid_argument("frame counts must be positive and within limits");
    }
    if (options.chat_rows == 0 || options.chat_rows > 100'000 || options.arrivals == 0 ||
        options.arrivals > 256) {
        throw std::invalid_argument("chat sizes must be positive and within limits");
    }
    if (options.file_entries == 0 || options.file_entries > 100'000) {
        throw std::invalid_argument("file entries must be positive and within limits");
    }
    if (options.editor_bytes == 0 || options.editor_bytes > 1'000'000 || options.editor_ops == 0 ||
        options.editor_ops > 1024) {
        throw std::invalid_argument("editor sizes must be positive and within limits");
    }
    if (options.visible == 0 || options.visible > 4096) {
        throw std::invalid_argument("visible window must be positive and within limits");
    }
    if (options.chat_rows > 0 && (options.warmup + options.frames) > SIZE_MAX / options.arrivals) {
        throw std::invalid_argument("chat arrival script is too large");
    }
    if ((options.warmup + options.frames) > SIZE_MAX / options.editor_ops) {
        throw std::invalid_argument("editor op script is too large");
    }
    return options;
}

double percentile(const std::vector<double>& sorted_times, std::size_t percent) {
    const auto last = sorted_times.size() - 1;
    const auto index = last / 100 * percent + last % 100 * percent / 100;
    return sorted_times[index];
}

std::uint64_t mix_state(std::uint64_t state, std::uint64_t value) noexcept {
    state ^= value + 0x9E3779B97F4A7C15ULL + (state << 6) + (state >> 2);
    return state;
}

void print_row(
    std::string_view workload,
    std::string_view engine,
    const Options& options,
    std::size_t items,
    EngineResult& result
) {
    std::cout << workload << ',' << engine << ',' << VEYRA_BUILD_PLATFORM << ','
              << VEYRA_BUILD_COMPILER << ',' << VEYRA_BUILD_CONFIG << ',' << items << ','
              << options.warmup << ',' << options.frames << ',' << percentile(result.adapter, 50)
              << ',' << percentile(result.adapter, 95) << ',' << percentile(result.adapter, 99)
              << ',' << result.adapter.back() << ',' << percentile(result.engine, 50) << ','
              << percentile(result.engine, 95) << ',' << percentile(result.engine, 99) << ','
              << result.engine.back() << ',' << result.checksum << ',' << result.allocs << ','
              << result.equiv << '\n';
}

ChatFixture build_chat_fixture(const Options& options) {
    auto fixture = ChatFixture{};
    fixture.heights.resize(options.chat_rows);
    for (std::size_t index = 0; index < fixture.heights.size(); ++index) {
        fixture.heights[index] = 20.0F + static_cast<float>(index % 5) * 4.0F;
    }

    const auto tail = std::max<std::size_t>(1, std::min(options.chat_rows, options.visible * 4));
    const auto total = (options.warmup + options.frames) * options.arrivals;
    fixture.rows.resize(total);
    fixture.arrival_heights.resize(total);
    auto generator = std::mt19937(options.seed);
    for (std::size_t index = 0; index < total; ++index) {
        fixture.rows[index] =
            static_cast<std::uint32_t>(options.chat_rows - 1 - generator() % tail);
        fixture.arrival_heights[index] = 20.0F + static_cast<float>(generator() % 5) * 4.0F;
    }
    return fixture;
}

FileFixture build_file_fixture(const Options& options) {
    auto fixture = FileFixture{};
    fixture.names.reserve(options.file_entries);
    for (std::size_t index = 0; index < options.file_entries; ++index) {
        auto digits = std::array<char, 32>{};
        const auto [end, error] =
            std::to_chars(digits.data(), digits.data() + digits.size(), index);
        if (error != std::errc{}) {
            throw std::runtime_error("could not format file entry name");
        }

        auto name = std::u8string{};
        name.reserve(9 + static_cast<std::size_t>(end - digits.data()));
        name.append(u8"file-");
        for (auto digit = digits.data(); digit != end; ++digit) {
            name.push_back(static_cast<char8_t>(*digit));
        }
        name.append(u8".dat");
        fixture.names.push_back(std::move(name));
    }

    fixture.entries.reserve(options.file_entries);
    for (std::size_t index = 0; index < options.file_entries; ++index) {
        fixture.entries.push_back({
            .id = static_cast<std::uint64_t>(index + 1),
            .name = fixture.names[index],
            .size_bytes = static_cast<std::uint64_t>(index * 17),
            .modified_nanoseconds = static_cast<std::int64_t>(options.file_entries - index),
            .is_directory = index % 16 == 0,
        });
    }
    std::reverse(fixture.entries.begin(), fixture.entries.end());
    return fixture;
}

EditorFixture build_editor_fixture(const Options& options) {
    auto fixture = EditorFixture{};
    fixture.initial.assign(options.editor_bytes, 'a');
    for (std::size_t index = 80; index < fixture.initial.size(); index += 80) {
        fixture.initial[index] = '\n';
    }

    fixture.script.resize((options.warmup + options.frames) * options.editor_ops);
    auto generator = std::mt19937(options.seed + 1);
    for (auto& kind : fixture.script) {
        kind = static_cast<std::uint8_t>(generator() % 100);
    }
    return fixture;
}

EngineResult
measure_chat(const Options& options, const ChatFixture& fixture, veyra::Engine& engine) {
    auto result = EngineResult{};
    result.adapter.reserve(options.frames);
    result.engine.reserve(options.frames);

    auto slots = std::vector<veyra::ChatRowHeightCacheSlot>(options.chat_rows * 2);
    auto extents = std::vector<float>(options.chat_rows);
    auto fenwick = std::vector<double>(options.chat_rows + 1);
    auto updates = std::vector<veyra::Update>(options.arrivals);
    auto rects = std::vector<veyra::Rect>(options.visible);
    for (std::size_t index = 0; index < extents.size(); ++index) {
        extents[index] = fixture.heights[index];
    }

    engine.reset(options.chat_rows);

    auto cache = veyra::ChatRowHeightCache(slots);
    if (!cache.valid()) {
        throw std::runtime_error("chat cache setup failed");
    }

    auto list = veyra::VirtualListIndex{};
    if (!list.reset(extents, fenwick)) {
        throw std::runtime_error("virtual list setup failed");
    }

    const auto first =
        options.chat_rows > options.visible ? options.chat_rows - options.visible : 0;
    auto rect_sum = std::uint64_t{};
    auto probe = std::optional<Measure>{};

    for (std::size_t frame = 0; frame < options.warmup + options.frames; ++frame) {
        if (frame == options.warmup) {
            probe.emplace();
        }

        const auto measuring = frame >= options.warmup;
        const auto base = frame * options.arrivals;
        const auto adapter_begin = Clock::now();
        for (std::size_t slot = 0; slot < options.arrivals; ++slot) {
            const auto row = fixture.rows[base + slot];
            const auto height = fixture.arrival_heights[base + slot];
            const veyra::ChatRowHeightKey key{
                .message_id = static_cast<std::uint64_t>(row) + 1,
                .available_width = 480.0F + static_cast<float>(row % 3) * 80.0F,
                .style_generation = 0,
            };
            const auto inserted = cache.insert(key, height);
            if (inserted != veyra::ChatRowHeightCacheResult::inserted &&
                inserted != veyra::ChatRowHeightCacheResult::updated) {
                throw std::runtime_error("chat cache insert failed");
            }

            auto delta = 0.0;
            if (!list.set_extent(row, height, delta)) {
                throw std::runtime_error("virtual list extent failed");
            }

            updates[slot] = {
                row,
                static_cast<std::uint32_t>(height * 16.0F),
            };
        }

        auto range = veyra::VirtualListRange{};
        const auto total = list.total_extent();
        const auto scroll = total > 1200.0 ? total - 1200.0 : 0.0;
        if (!list.visible_range(scroll, 1200.0, 2, range)) {
            throw std::runtime_error("virtual range failed");
        }

        const auto adapter_end = Clock::now();
        engine.apply(updates);
        const auto stats = engine.frame(first, options.visible, rects);
        const auto engine_end = Clock::now();
        if (stats.emitted > rects.size()) {
            throw std::runtime_error("engine emitted beyond capacity");
        }
        if (!measuring) {
            continue;
        }

        result.adapter.push_back(
            std::chrono::duration<double, std::nano>(adapter_end - adapter_begin).count()
        );
        result.engine.push_back(
            std::chrono::duration<double, std::nano>(engine_end - adapter_end).count()
        );
        for (std::size_t index = 0; index < stats.emitted; ++index) {
            rect_sum += rects[index].color;
        }
        result.equiv = mix_state(result.equiv, range.first_item);
        result.equiv = mix_state(result.equiv, range.last_item);
        result.equiv = mix_state(result.equiv, rect_sum);
    }

    if (!probe.has_value()) {
        throw std::runtime_error("allocation probe missed the measured window");
    }
    result.allocs = probe->count();
    result.checksum = engine.checksum();
    return result;
}

EngineResult
measure_files(const Options& options, const FileFixture& fixture, veyra::Engine& engine) {
    auto result = EngineResult{};
    result.adapter.reserve(options.frames);
    result.engine.reserve(options.frames);

    constexpr auto update_cap = std::size_t{256};
    auto output = std::vector<std::size_t>(options.file_entries);
    auto updates = std::vector<veyra::Update>(update_cap);
    auto rects = std::vector<veyra::Rect>(options.visible);
    engine.reset(options.file_entries);

    const std::array<veyra::FileListSort, 3> sorts{
        veyra::FileListSort{.key = veyra::FileListSortKey::name},
        veyra::FileListSort{
            .key = veyra::FileListSortKey::modified_time,
            .direction = veyra::FileListSortDirection::descending,
        },
        veyra::FileListSort{
            .key = veyra::FileListSortKey::size,
            .direction = veyra::FileListSortDirection::descending,
            .directories_first = false,
        },
    };
    const std::array<std::u8string_view, 4> filters{u8"", u8"file-1", u8"file-9", u8".dat"};

    auto probe = std::optional<Measure>{};

    for (std::size_t frame = 0; frame < options.warmup + options.frames; ++frame) {
        if (frame == options.warmup) {
            probe.emplace();
        }

        const auto measuring = frame >= options.warmup;
        const auto adapter_begin = Clock::now();
        auto output_count = std::size_t{0};
        const auto ordered = veyra::build_file_list_order(
            fixture.entries,
            filters[frame % filters.size()],
            sorts[frame % sorts.size()],
            output,
            output_count
        );
        if (ordered != veyra::FileListResult::complete) {
            throw std::runtime_error("file list order failed");
        }

        const auto mapped = std::min(output_count, updates.size());
        for (std::size_t rank = 0; rank < mapped; ++rank) {
            const auto& entry = fixture.entries[output[rank]];
            updates[rank] = {
                static_cast<std::uint32_t>(entry.id - 1),
                static_cast<std::uint32_t>(rank + 1),
            };
        }

        const auto adapter_end = Clock::now();
        engine.apply(std::span<const veyra::Update>(updates.data(), mapped));
        const auto stats = engine.frame(0, options.visible, rects);
        const auto engine_end = Clock::now();
        if (stats.emitted > rects.size()) {
            throw std::runtime_error("engine emitted beyond capacity");
        }
        if (!measuring) {
            continue;
        }

        result.adapter.push_back(
            std::chrono::duration<double, std::nano>(adapter_end - adapter_begin).count()
        );
        result.engine.push_back(
            std::chrono::duration<double, std::nano>(engine_end - adapter_end).count()
        );
        result.equiv = mix_state(result.equiv, output_count);
        for (std::size_t sample = 0; sample < std::min(output_count, std::size_t{8}); ++sample) {
            result.equiv = mix_state(result.equiv, output[sample]);
        }
        for (std::size_t index = 0; index < stats.emitted; ++index) {
            result.equiv = mix_state(result.equiv, rects[index].color);
        }
    }

    if (!probe.has_value()) {
        throw std::runtime_error("allocation probe missed the measured window");
    }
    result.allocs = probe->count();
    result.checksum = engine.checksum();
    return result;
}

EngineResult
measure_editor(const Options& options, const EditorFixture& fixture, veyra::Engine& engine) {
    auto result = EngineResult{};
    result.adapter.reserve(options.frames);
    result.engine.reserve(options.frames);

    constexpr auto update_count = std::size_t{8};
    constexpr auto engine_items = std::size_t{256};
    auto storage = std::vector<char>(options.editor_bytes + fixture.script.size() + 64);
    auto history = std::vector<char>(65536);
    auto snap = std::vector<char>(storage.size());
    auto updates = std::vector<veyra::Update>(update_count);
    auto rects = std::vector<veyra::Rect>(options.visible);

    auto editor = veyra::TextEditorBuffer(
        std::span<char>(storage),
        std::string_view(fixture.initial.data(), fixture.initial.size()),
        std::span<char>(history)
    );
    if (editor.initialization_result() != veyra::TextEditResult::complete) {
        throw std::runtime_error("editor setup failed");
    }
    if (editor.set_cursor(options.editor_bytes / 2) != veyra::TextEditResult::complete) {
        throw std::runtime_error("editor cursor setup failed");
    }
    engine.reset(engine_items);

    auto probe = std::optional<Measure>{};

    for (std::size_t frame = 0; frame < options.warmup + options.frames; ++frame) {
        if (frame == options.warmup) {
            probe.emplace();
        }

        const auto measuring = frame >= options.warmup;
        const auto base = frame * options.editor_ops;
        const auto adapter_begin = Clock::now();
        for (std::size_t slot = 0; slot < options.editor_ops; ++slot) {
            const auto glyph = static_cast<char>('a' + static_cast<int>((base + slot) % 26));
            const auto piece = std::string_view(&glyph, 1);
            const auto kind = fixture.script[base + slot];
            auto outcome = veyra::TextEditResult::complete;
            if (kind < 70) {
                outcome = editor.insert(piece);
            } else if (kind < 85) {
                outcome = editor.erase_backward();
            } else if (kind < 95) {
                if (editor.can_undo()) {
                    outcome = editor.undo();
                } else {
                    outcome = editor.insert(piece);
                }
            } else if (editor.can_redo()) {
                outcome = editor.redo();
            } else {
                outcome = editor.insert(piece);
            }
            if (outcome != veyra::TextEditResult::complete) {
                outcome = editor.insert(piece);
            }
            if (outcome != veyra::TextEditResult::complete) {
                throw std::runtime_error("editor op failed");
            }
        }

        auto length = std::size_t{0};
        if (editor.copy_text(snap, length) != veyra::TextEditResult::complete) {
            throw std::runtime_error("editor snapshot failed");
        }
        auto hash = 0xCBF29CE484222325ULL;
        for (std::size_t index = 0; index < length; ++index) {
            hash ^= static_cast<std::uint64_t>(static_cast<unsigned char>(snap[index]));
            hash *= 0x100000001B3ULL;
        }
        for (std::size_t part = 0; part < updates.size(); ++part) {
            updates[part] = {
                static_cast<std::uint32_t>(part),
                static_cast<std::uint32_t>(hash + part * 0x9E3779B9ULL),
            };
        }

        const auto adapter_end = Clock::now();
        engine.apply(updates);
        const auto stats = engine.frame(0, options.visible, rects);
        const auto engine_end = Clock::now();
        if (stats.emitted > rects.size()) {
            throw std::runtime_error("engine emitted beyond capacity");
        }
        if (!measuring) {
            continue;
        }

        result.adapter.push_back(
            std::chrono::duration<double, std::nano>(adapter_end - adapter_begin).count()
        );
        result.engine.push_back(
            std::chrono::duration<double, std::nano>(engine_end - adapter_end).count()
        );
        result.equiv = mix_state(result.equiv, editor.size());
        result.equiv = mix_state(result.equiv, editor.cursor());
        result.equiv = mix_state(result.equiv, hash);
        for (std::size_t index = 0; index < stats.emitted; ++index) {
            result.equiv = mix_state(result.equiv, rects[index].color);
        }
    }

    if (!probe.has_value()) {
        throw std::runtime_error("allocation probe missed the measured window");
    }
    result.allocs = probe->count();
    result.checksum = engine.checksum();
    return result;
}

template <typename Fixture>
using MeasureFn = EngineResult (*)(const Options&, const Fixture&, veyra::Engine&);

template <typename Fixture>
void run_workload(
    std::string_view workload,
    const Options& options,
    const Fixture& fixture,
    std::size_t items,
    MeasureFn<Fixture> measure
) {
    const auto engines = veyra::available_engines();
    if (engines.empty()) {
        throw std::runtime_error("no paradigm engines registered");
    }

    auto names = std::vector<std::string>{};
    auto results = std::vector<EngineResult>{};
    names.reserve(engines.size());
    results.reserve(engines.size());
    for (const auto name : engines) {
        auto engine = veyra::create_engine(name);
        names.emplace_back(name);
        results.push_back(measure(options, fixture, *engine));
    }

    for (std::size_t index = 1; index < results.size(); ++index) {
        if (results[index].checksum != results[0].checksum ||
            results[index].equiv != results[0].equiv) {
            throw std::runtime_error("paradigm divergence in " + std::string(workload));
        }
    }

    for (std::size_t index = 0; index < results.size(); ++index) {
        std::sort(results[index].adapter.begin(), results[index].adapter.end());
        std::sort(results[index].engine.begin(), results[index].engine.end());
        print_row(workload, names[index], options, items, results[index]);
    }
}

int run(const Options& options) {
    const auto chat = build_chat_fixture(options);
    const auto files = build_file_fixture(options);
    const auto editor = build_editor_fixture(options);

    std::cout << "workload,engine,platform,compiler,build_config,items,warmup_frames,"
                 "measured_frames,adapter_p50_ns,adapter_p95_ns,adapter_p99_ns,adapter_max_ns,"
                 "engine_p50_ns,engine_p95_ns,engine_p99_ns,engine_max_ns,checksum,allocs,equiv\n";
    run_workload("chat_burst", options, chat, options.chat_rows, measure_chat);
    run_workload("file_sort_filter", options, files, options.file_entries, measure_files);
    run_workload("editor_typing", options, editor, 256, measure_editor);
    return 0;
}

}

int main(int argc, char** argv) {
    try {
        return run(parse_options(argc, argv));
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 2;
    }
}
