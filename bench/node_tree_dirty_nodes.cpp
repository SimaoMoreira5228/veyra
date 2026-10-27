#include "veyra/core/tree/node_tree.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;

constexpr auto node_count = std::size_t{100'000};
constexpr std::array
    dirty_counts{std::size_t{1}, std::size_t{128}, std::size_t{10'000}, node_count};
constexpr auto warmup_iterations = std::size_t{20};
constexpr auto measured_iterations = std::size_t{100};

struct TimingSummary {
    double p50;
    double p95;
    double p99;
    double maximum;
};

struct ComparisonTiming {
    TimingSummary scan;
    TimingSummary index;
};

double percentile(const std::vector<double>& sorted, std::size_t percent) {
    const auto last = sorted.size() - 1;
    const auto index = last / 100 * percent + last % 100 * percent / 100;
    return sorted[index];
}

TimingSummary summarize(std::vector<double>& timings) {
    std::sort(timings.begin(), timings.end());
    return {
        .p50 = percentile(timings, 50),
        .p95 = percentile(timings, 95),
        .p99 = percentile(timings, 99),
        .maximum = timings.back(),
    };
}

std::size_t
count_dirty_by_scan(const veyra::NodeTree& tree, const std::vector<veyra::NodeId>& nodes) noexcept {
    auto count = std::size_t{0};
    for (const auto node : nodes) {
        count += tree.is_dirty(node, veyra::DirtyCategory::paint);
    }
    return count;
}

std::size_t count_dirty_by_index(const veyra::NodeTree& tree) noexcept {
    auto count = std::size_t{0};
    for (const auto node : tree.dirty_nodes()) {
        count += tree.is_dirty(node, veyra::DirtyCategory::paint);
    }
    return count;
}

template <typename CountScan, typename CountIndex>
ComparisonTiming measure(
    CountScan&& count_scan,
    CountIndex&& count_index,
    std::size_t expected_count,
    std::size_t& scan_checksum,
    std::size_t& index_checksum
) {
    std::vector<double> scan_timings;
    std::vector<double> index_timings;
    scan_timings.reserve(measured_iterations);
    index_timings.reserve(measured_iterations);

    for (std::size_t iteration = 0; iteration < warmup_iterations; ++iteration) {
        if (count_scan() != expected_count || count_index() != expected_count) {
            throw std::runtime_error("dirty-node benchmark warmup produced the wrong count");
        }
    }

    const auto record = [expected_count](auto&& count_dirty, auto& timings, auto& checksum) {
        const auto begin = Clock::now();
        const auto count = count_dirty();
        const auto end = Clock::now();
        if (count != expected_count) {
            throw std::runtime_error("dirty-node benchmark produced the wrong count");
        }

        timings.push_back(std::chrono::duration<double, std::nano>(end - begin).count());
        checksum += count;
    };

    for (std::size_t iteration = 0; iteration < measured_iterations; ++iteration) {
        if (iteration % 2 == 0) {
            record(count_scan, scan_timings, scan_checksum);
            record(count_index, index_timings, index_checksum);
            continue;
        }

        record(count_index, index_timings, index_checksum);
        record(count_scan, scan_timings, scan_checksum);
    }

    return {
        .scan = summarize(scan_timings),
        .index = summarize(index_timings),
    };
}

}

int main() {
    try {
        veyra::NodeTree tree;
        tree.reserve(node_count);
        std::vector<veyra::NodeId> nodes;
        nodes.reserve(node_count);

        for (std::size_t index = 0; index < node_count; ++index) {
            const auto node = tree.create();
            if (node.is_null() || !tree.clear_dirty(node, veyra::DirtyCategory::structure)) {
                throw std::runtime_error("dirty-node benchmark setup failed");
            }
            nodes.push_back(node);
        }

        std::cout << "scenario,platform,compiler,build_config,nodes,dirty_nodes,iterations,"
                     "scan_p50_ns,scan_p95_ns,scan_p99_ns,scan_max_ns,index_p50_ns,"
                     "index_p95_ns,index_p99_ns,index_max_ns,scan_checksum,index_checksum\n";

        auto marked_count = std::size_t{0};
        for (const auto dirty_count : dirty_counts) {
            for (; marked_count < dirty_count; ++marked_count) {
                if (!tree.mark_dirty(nodes[marked_count], veyra::DirtyCategory::paint)) {
                    throw std::runtime_error("dirty-node benchmark could not mark a node");
                }
            }

            if (tree.dirty_node_count() != dirty_count) {
                throw std::runtime_error("dirty-node benchmark index size is inconsistent");
            }

            auto scan_checksum = std::size_t{0};
            auto index_checksum = std::size_t{0};
            const auto timing = measure(
                [&tree, &nodes] {
                    return count_dirty_by_scan(tree, nodes);
                },
                [&tree] {
                    return count_dirty_by_index(tree);
                },
                dirty_count,
                scan_checksum,
                index_checksum
            );
            const auto expected_checksum = dirty_count * measured_iterations;
            if (scan_checksum != expected_checksum || index_checksum != expected_checksum) {
                throw std::runtime_error("dirty-node benchmark results do not agree");
            }

            std::cout << "dirty_nodes_scan_compare," << VEYRA_BUILD_PLATFORM << ','
                      << VEYRA_BUILD_COMPILER << ',' << VEYRA_BUILD_CONFIG << ',' << node_count
                      << ',' << dirty_count << ',' << measured_iterations << ',' << std::fixed
                      << std::setprecision(3) << timing.scan.p50 << ',' << timing.scan.p95 << ','
                      << timing.scan.p99 << ',' << timing.scan.maximum << ',' << timing.index.p50
                      << ',' << timing.index.p95 << ',' << timing.index.p99 << ','
                      << timing.index.maximum << ',' << scan_checksum << ',' << index_checksum
                      << '\n';
        }
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 2;
    }
    return 0;
}
