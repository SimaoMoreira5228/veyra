#include "veyra/core/semantics.hpp"
#include "veyra/layout/scroll_axis.hpp"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;

struct Options {
    std::size_t items{1'000'000};
    std::size_t queries{100'000};
    std::size_t warmup{1'000};
};

struct ItemIdLookupContext {
    std::size_t item_count{};
};

bool find_item_index(void* context, std::uint64_t item_id, std::size_t& item_index) noexcept {
    const auto item_count = static_cast<ItemIdLookupContext*>(context)->item_count;
    if (item_id == 0 || item_id > item_count) {
        return false;
    }

    item_index = static_cast<std::size_t>(item_id - 1);
    return true;
}

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
        if (argument == "--items") {
            options.items = value;
        } else if (argument == "--queries") {
            options.queries = value;
        } else if (argument == "--warmup") {
            options.warmup = value;
        } else {
            throw std::invalid_argument("unknown option: " + std::string(argument));
        }
    }

    if (options.items == 0 || options.queries == 0 || options.items > 10'000'000 ||
        options.queries > 10'000'000 || options.warmup > 10'000'000) {
        throw std::invalid_argument(
            "items and queries must be positive and all values within limits"
        );
    }
    return options;
}

double percentile(const std::vector<double>& sorted_times, std::size_t percent) {
    const auto last = sorted_times.size() - 1;
    const auto index = last / 100 * percent + last % 100 * percent / 100;
    return sorted_times[index];
}

double scroll_offset(std::size_t query, double total_extent) {
    constexpr std::uint64_t modulus = 1'000'003;
    const auto sample = (static_cast<std::uint64_t>(query) * 48'271 + 17) % modulus;
    return total_extent * static_cast<double>(sample) / static_cast<double>(modulus);
}

std::size_t item_index_for_query(std::size_t query, std::size_t item_count) {
    constexpr std::uint64_t multiplier = 48'271;
    const auto sample = (static_cast<std::uint64_t>(query) * multiplier + 17) % item_count;
    return static_cast<std::size_t>(sample);
}

int run(const Options& options) {
    std::vector<float> extents(options.items);
    std::vector<std::uint64_t> item_ids(options.items);
    for (std::size_t index = 0; index < extents.size(); ++index) {
        extents[index] = 18.0F + static_cast<float>(index % 13);
        item_ids[index] = static_cast<std::uint64_t>(index) + 1;
    }
    std::vector<double> fenwick(options.items + 1);
    veyra::VirtualListIndex index;
    auto lookup_context = ItemIdLookupContext{options.items};

    const auto build_begin = Clock::now();
    if (!index.reset(extents, fenwick)) {
        throw std::runtime_error("virtual list index initialization failed");
    }
    const auto build_end = Clock::now();

    std::vector<double> timings;
    std::vector<double> semantic_timings;
    std::vector<double> anchor_timings;
    std::vector<double> scroll_chain_timings;
    std::vector<double> reveal_timings;
    timings.reserve(options.queries);
    semantic_timings.reserve(options.queries);
    anchor_timings.reserve(options.queries);
    scroll_chain_timings.reserve(options.queries);
    reveal_timings.reserve(options.queries);
    veyra::VirtualListRange range;
    veyra::VirtualListAnchor anchor;
    double restored_scroll_offset = 0.0;
    veyra::NodeTree tree;
    tree.reserve(1);
    const auto list = tree.create();
    veyra::VirtualSemanticNode semantic_item;
    constexpr veyra::SemanticNodeData semantic_data{
        .role = veyra::SemanticRole::list_item,
        .label = "Entry",
    };
    constexpr veyra::LayoutRect viewport{
        .width = 640.0F,
        .height = 720.0F,
    };

    for (std::size_t query = 0; query < options.warmup; ++query) {
        const auto scroll = scroll_offset(query, index.total_extent());
        if (!index.visible_range(scroll, 720.0, 2, range)) {
            throw std::runtime_error("virtual range query failed during warmup");
        }
        if (!index.capture_anchor(item_ids, scroll, 720.0, 0, anchor) ||
            !index.restore_anchor(
                anchor,
                720.0,
                0,
                find_item_index,
                &lookup_context,
                restored_scroll_offset
            )) {
            throw std::runtime_error("virtual scroll anchor failed during warmup");
        }

        const auto item_index = item_index_for_query(query, options.items);
        double item_offset = 0.0;
        if (!index.offset_of(item_index, item_offset) ||
            veyra::virtual_semantic_item(
                tree,
                list,
                index,
                static_cast<std::uint64_t>(item_index) + 1,
                item_index,
                item_offset,
                viewport,
                semantic_data,
                semantic_item
            ) != veyra::VirtualSemanticResult::complete) {
            throw std::runtime_error("virtual semantic query failed during warmup");
        }
        double reveal_offset = 0.0;
        if (!index.scroll_item_into_view(item_index, 720.0, scroll, reveal_offset)) {
            throw std::runtime_error("virtual item reveal failed during warmup");
        }
    }

    double checksum = 0.0;
    double semantic_checksum = 0.0;
    double anchor_checksum = 0.0;
    double reveal_checksum = 0.0;
    auto child_scroll = veyra::ScrollAxis{};
    auto parent_scroll = veyra::ScrollAxis{};
    if (!child_scroll.set_extents(20'000.0, 720.0) ||
        !parent_scroll.set_extents(100'000.0, 1'080.0) ||
        !child_scroll.set_offset(child_scroll.maximum_offset() - 17.0)) {
        throw std::runtime_error("nested scroll axes could not be initialized");
    }
    auto scroll_chain_checksum = 0.0;
    for (std::size_t query = 0; query < options.warmup; ++query) {
        const auto delta = query % 2 == 0 ? 31.25 : -19.5;
        auto remaining_delta = delta;
        if (!child_scroll.scroll_by(remaining_delta, remaining_delta) ||
            !parent_scroll.scroll_by(remaining_delta, remaining_delta)) {
            throw std::runtime_error("nested scroll chain failed during warmup");
        }
        scroll_chain_checksum += child_scroll.offset() + parent_scroll.offset() + remaining_delta;
    }

    for (std::size_t query = 0; query < options.queries; ++query) {
        const auto scroll = scroll_offset(query + options.warmup, index.total_extent());
        const auto begin = Clock::now();
        const auto valid = index.visible_range(scroll, 720.0, 2, range);
        const auto end = Clock::now();
        if (!valid) {
            throw std::runtime_error("virtual range query failed during measurement");
        }

        timings.push_back(std::chrono::duration<double, std::nano>(end - begin).count());
        checksum += static_cast<double>(range.first_item + range.last_item) + range.first_offset;

        const auto anchor_begin = Clock::now();
        const auto anchor_captured = index.capture_anchor(item_ids, scroll, 720.0, 0, anchor);
        const auto anchor_restored = anchor_captured &&
            index.restore_anchor(
                anchor,
                720.0,
                0,
                find_item_index,
                &lookup_context,
                restored_scroll_offset
            );
        const auto anchor_end = Clock::now();
        if (!anchor_restored) {
            throw std::runtime_error("virtual scroll anchor failed during measurement");
        }

        anchor_timings.push_back(
            std::chrono::duration<double, std::nano>(anchor_end - anchor_begin).count()
        );
        anchor_checksum += static_cast<double>(anchor.item_index) +
            static_cast<double>(anchor.item_id) + anchor.item_offset + restored_scroll_offset +
            static_cast<double>(anchor.pinned_to_end);

        const auto item_index = item_index_for_query(query + options.warmup, options.items);
        double item_offset = 0.0;
        if (!index.offset_of(item_index, item_offset)) {
            throw std::runtime_error("virtual item offset query failed during measurement");
        }

        const auto semantic_begin = Clock::now();
        const auto semantic_result = veyra::virtual_semantic_item(
            tree,
            list,
            index,
            static_cast<std::uint64_t>(item_index) + 1,
            item_index,
            item_offset,
            viewport,
            semantic_data,
            semantic_item
        );
        const auto semantic_end = Clock::now();
        if (semantic_result != veyra::VirtualSemanticResult::complete) {
            throw std::runtime_error("virtual semantic query failed during measurement");
        }

        semantic_timings.push_back(
            std::chrono::duration<double, std::nano>(semantic_end - semantic_begin).count()
        );
        semantic_checksum += static_cast<double>(semantic_item.index) + semantic_item.bounds.y;

        double reveal_offset = 0.0;
        const auto reveal_begin = Clock::now();
        const auto revealed = index.scroll_item_into_view(item_index, 720.0, scroll, reveal_offset);
        const auto reveal_end = Clock::now();
        if (!revealed) {
            throw std::runtime_error("virtual item reveal failed during measurement");
        }
        reveal_timings.push_back(
            std::chrono::duration<double, std::nano>(reveal_end - reveal_begin).count()
        );
        reveal_checksum += reveal_offset;

        const auto delta = (query + options.warmup) % 2 == 0 ? 31.25 : -19.5;
        auto remaining_delta = delta;
        const auto scroll_begin = Clock::now();
        const auto child_scrolled = child_scroll.scroll_by(remaining_delta, remaining_delta);
        const auto parent_scrolled =
            child_scrolled && parent_scroll.scroll_by(remaining_delta, remaining_delta);
        const auto scroll_end = Clock::now();
        if (!parent_scrolled) {
            throw std::runtime_error("nested scroll chain failed during measurement");
        }
        scroll_chain_timings.push_back(
            std::chrono::duration<double, std::nano>(scroll_end - scroll_begin).count()
        );
        scroll_chain_checksum += child_scroll.offset() + parent_scroll.offset() + remaining_delta;
    }

    std::sort(timings.begin(), timings.end());
    std::sort(semantic_timings.begin(), semantic_timings.end());
    std::sort(anchor_timings.begin(), anchor_timings.end());
    std::sort(scroll_chain_timings.begin(), scroll_chain_timings.end());
    std::sort(reveal_timings.begin(), reveal_timings.end());
    const auto build_ns = std::chrono::duration<double, std::nano>(build_end - build_begin).count();
    std::cout << "scenario,platform,compiler,build_config,items,warmup_queries,measured_queries,"
                 "build_ns,p50_ns,p95_ns,p99_ns,max_ns,total_extent,checksum\n"
              << "variable_height_random_access," << VEYRA_BUILD_PLATFORM << ','
              << VEYRA_BUILD_COMPILER << ',' << VEYRA_BUILD_CONFIG << ',' << options.items << ','
              << options.warmup << ',' << options.queries << ',' << build_ns << ','
              << percentile(timings, 50) << ',' << percentile(timings, 95) << ','
              << percentile(timings, 99) << ',' << timings.back() << ',' << index.total_extent()
              << ',' << checksum << '\n'
              << "virtual_item_semantics_lookup," << VEYRA_BUILD_PLATFORM << ','
              << VEYRA_BUILD_COMPILER << ',' << VEYRA_BUILD_CONFIG << ',' << options.items << ','
              << options.warmup << ',' << options.queries << ',' << build_ns << ','
              << percentile(semantic_timings, 50) << ',' << percentile(semantic_timings, 95) << ','
              << percentile(semantic_timings, 99) << ',' << semantic_timings.back() << ','
              << index.total_extent() << ',' << semantic_checksum << '\n'
              << "virtual_scroll_anchor_capture_restore," << VEYRA_BUILD_PLATFORM << ','
              << VEYRA_BUILD_COMPILER << ',' << VEYRA_BUILD_CONFIG << ',' << options.items << ','
              << options.warmup << ',' << options.queries << ',' << build_ns << ','
              << percentile(anchor_timings, 50) << ',' << percentile(anchor_timings, 95) << ','
              << percentile(anchor_timings, 99) << ',' << anchor_timings.back() << ','
              << index.total_extent() << ',' << anchor_checksum << '\n';
    std::cout << "nested_scroll_delta_chaining," << VEYRA_BUILD_PLATFORM << ','
              << VEYRA_BUILD_COMPILER << ',' << VEYRA_BUILD_CONFIG << ",2," << options.warmup << ','
              << options.queries << ",0," << percentile(scroll_chain_timings, 50) << ','
              << percentile(scroll_chain_timings, 95) << ',' << percentile(scroll_chain_timings, 99)
              << ',' << scroll_chain_timings.back() << ',' << child_scroll.content_extent() << ','
              << scroll_chain_checksum << '\n';
    std::cout << "virtual_item_scroll_into_view," << VEYRA_BUILD_PLATFORM << ','
              << VEYRA_BUILD_COMPILER << ',' << VEYRA_BUILD_CONFIG << ',' << options.items << ','
              << options.warmup << ',' << options.queries << ',' << build_ns << ','
              << percentile(reveal_timings, 50) << ',' << percentile(reveal_timings, 95) << ','
              << percentile(reveal_timings, 99) << ',' << reveal_timings.back() << ','
              << index.total_extent() << ',' << reveal_checksum << '\n';
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
