#include "veyra/behaviors/data/log_feed.hpp"
#include "veyra/behaviors/control/scroll.hpp"
#include "veyra/chat/rows/row_height_cache.hpp"
#include "veyra/layout/scroll/virtual_list.hpp"
#include "veyra/widgets/data/log_feed.hpp"

#include <array>
#include <charconv>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;

struct Options {
    std::size_t messages{10'000};
    std::size_t frames{60};
    std::size_t warmup{5};
    std::size_t arrivals{32};
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
    Options options;
    for (int index = 1; index < argc; ++index) {
        const std::string_view argument(argv[index]);
        if (index + 1 >= argc) {
            throw std::invalid_argument("missing value for " + std::string(argument));
        }
        const auto value = parse_size(argv[++index]);
        if (argument == "--messages") {
            options.messages = value;
        } else if (argument == "--frames") {
            options.frames = value;
        } else if (argument == "--warmup") {
            options.warmup = value;
        } else if (argument == "--arrivals") {
            options.arrivals = value;
        } else {
            throw std::invalid_argument("unknown option: " + std::string(argument));
        }
    }
    if (options.messages == 0 || options.frames == 0 || options.arrivals == 0) {
        throw std::invalid_argument("messages, frames, and arrivals must be positive");
    }
    return options;
}

float burst_height(std::uint64_t message_id, std::uint32_t folds) {
    const auto band = static_cast<float>(message_id % 5) * 4.0F;
    const auto badge = folds > 1 ? 12.0F : 0.0F;
    return 16.0F + band + badge;
}

int run(const Options& options) {
    auto cache_slots = std::vector<veyra::ChatRowHeightCacheSlot>(16384);
    auto heights = veyra::ChatRowHeightCache{cache_slots};
    if (!heights.valid()) {
        throw std::runtime_error("chat view bench workspace is invalid");
    }
    auto feed = veyra::LogFeed{};
    auto ids = std::vector<std::uint64_t>{};
    auto extents = std::vector<float>{};
    auto fenwick = std::vector<double>{};
    ids.reserve(options.messages + options.frames * options.arrivals);
    extents.reserve(options.messages + options.frames * options.arrivals);
    fenwick.reserve(options.messages + options.frames * options.arrivals + 1);
    auto list = veyra::VirtualListIndex{};
    auto scroller = veyra::ScrollController{};
    const auto theme = veyra::Theme{};
    auto states = std::array<veyra::LogFeedRowState, 64>{};
    auto badges = std::array<veyra::LayoutRect, 64>{};
    auto commands = std::array<veyra::DisplayCommand, 256>{};
    constexpr auto kViewport = 240.0;

    auto id = std::uint64_t{1};
    auto now_ms = std::uint64_t{};
    for (std::size_t index = 0; index < options.messages; ++index) {
        const auto kind = index % 2 == 0 ? veyra::LogFeedKind::info : veyra::LogFeedKind::warning;
        const auto pushed = feed.push(id, kind, now_ms);
        if (pushed.result != veyra::LogFeedResult::appended) {
            throw std::runtime_error("seeded history appends row by row");
        }
        const auto height = burst_height(id, 1);
        if (heights.insert({id, 640.0F, 1}, height) != veyra::ChatRowHeightCacheResult::inserted) {
            throw std::runtime_error("seeded height is cached");
        }
        ids.push_back(id);
        extents.push_back(height);
        ++id;
        now_ms += 2000;
    }

    auto checksum = std::uint64_t{};
    auto total_pushed = std::uint64_t{};
    auto step_frame = [&]() {
        for (std::size_t arrival = 0; arrival < options.arrivals; ++arrival) {
            const auto kind =
                arrival % 2 == 0 ? veyra::LogFeedKind::info : veyra::LogFeedKind::warning;
            const auto pushed = feed.push(id, kind, now_ms);
            if (pushed.result != veyra::LogFeedResult::appended) {
                throw std::runtime_error("burst arrivals append row by row");
            }
            const auto height = burst_height(id, 1);
            heights.insert({id, 640.0F, 1}, height);
            ids.push_back(id);
            extents.push_back(height);
            ++id;
            now_ms += 2000;
            ++total_pushed;
        }
        fenwick.assign(extents.size() + 1, 0.0);
        if (!list.reset(extents, fenwick)) {
            throw std::runtime_error("burst rebuilds the variable-height index");
        }
        if (!scroller.configure(list.total_extent(), kViewport)) {
            throw std::runtime_error("burst reconfigures the scroller");
        }
        if (scroller.set_offset(scroller.maximum_offset()) == veyra::ScrollResult::ignored) {
            throw std::runtime_error("burst pins to the tail");
        }
        auto range = veyra::VirtualListRange{};
        if (!list.visible_range(scroller.offset(), kViewport, 2, range)) {
            throw std::runtime_error("burst resolves its tail window");
        }
        const auto visible = range.last_item - range.first_item;
        if (visible > states.size()) {
            throw std::runtime_error("tail window exceeds caller spans");
        }
        auto base_offset = 0.0;
        if (!list.offset_of(range.first_item, base_offset)) {
            throw std::runtime_error("tail slice base is unavailable");
        }
        for (auto cursor = range.first_item; cursor < range.last_item; ++cursor) {
            const auto slot = cursor - range.first_item;
            states[slot].kind =
                cursor % 2 == 0 ? veyra::LogFeedKind::info : veyra::LogFeedKind::warning;
            badges[slot] = {};
        }
        const veyra::VirtualListRowsGeometry geometry{
            .list_bounds = {.x = 0.0F, .y = 0.0F, .width = 320.0F, .height = 240.0F},
            .row_height = 24.0F,
        };
        const auto output = veyra::emit_log_feed_rows(
            theme,
            std::span<const veyra::LogFeedRowState>{states.data(), visible},
            std::span<const veyra::LayoutRect>{badges.data(), visible},
            geometry,
            commands
        );
        if (output.result != veyra::LogFeedEmitResult::complete) {
            throw std::runtime_error("tail slice emits");
        }
        checksum += output.placed + range.first_item + range.last_item;
    };

    for (std::size_t frame = 0; frame < options.warmup; ++frame) {
        step_frame();
    }
    const auto measured_pushed_before = total_pushed;
    const auto begin = Clock::now();
    for (std::size_t frame = 0; frame < options.frames; ++frame) {
        step_frame();
    }
    const auto elapsed = std::chrono::duration<double>(Clock::now() - begin).count();
    const auto measured_pushed = total_pushed - measured_pushed_before;
    std::cout << "scenario,platform,compiler,build_config,messages,frames,arrivals_per_frame,"
                 "elapsed_seconds,messages_per_second,checksum\n"
              << "chat_view_burst_push_window_emit," << VEYRA_BUILD_PLATFORM << ','
              << VEYRA_BUILD_COMPILER << ',' << VEYRA_BUILD_CONFIG << ',' << options.messages << ','
              << options.frames << ',' << options.arrivals << ',' << elapsed << ','
              << static_cast<double>(measured_pushed) / elapsed << ',' << checksum << '\n';
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
