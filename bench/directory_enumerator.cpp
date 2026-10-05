#include "veyra/files/directory_enumerator.hpp"

#include <algorithm>
#include <atomic>
#include <charconv>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;

struct Options {
    std::size_t entries{10'000};
    std::size_t warmup{1};
    std::size_t queries{100};
};

struct TemporaryDirectory {
    std::filesystem::path path;

    explicit TemporaryDirectory(std::filesystem::path directory) : path(std::move(directory)) {
    }

    TemporaryDirectory(TemporaryDirectory&& other) noexcept : path(std::move(other.path)) {
        other.path.clear();
    }

    TemporaryDirectory(const TemporaryDirectory&) = delete;
    TemporaryDirectory& operator=(const TemporaryDirectory&) = delete;

    ~TemporaryDirectory() {
        std::error_code error;
        std::filesystem::remove_all(path, error);
    }
};

std::size_t parse_count(
    std::string_view value,
    std::string_view option,
    std::size_t maximum,
    bool allow_zero = false
) {
    auto count = std::size_t{};
    const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), count);
    if (error != std::errc{} || end != value.data() + value.size() || (!allow_zero && count == 0) ||
        count > maximum) {
        throw std::invalid_argument("invalid value for " + std::string(option));
    }
    return count;
}

Options parse_options(int argc, char** argv) {
    auto options = Options{};
    for (auto index = 1; index < argc; ++index) {
        const auto option = std::string_view(argv[index]);
        if (index + 1 >= argc) {
            throw std::invalid_argument("missing value for " + std::string(option));
        }

        const auto value = std::string_view(argv[++index]);
        if (option == "--entries") {
            options.entries = parse_count(value, option, 1'000'000);
        } else if (option == "--warmup") {
            options.warmup = parse_count(value, option, 1'000, true);
        } else if (option == "--queries") {
            options.queries = parse_count(value, option, 1'000);
        } else {
            throw std::invalid_argument("unknown option " + std::string(option));
        }
    }
    return options;
}

TemporaryDirectory make_fixture(std::size_t entry_count) {
    static auto sequence = std::atomic<std::uint64_t>{};
    const auto stamp = Clock::now().time_since_epoch().count();
    auto root = std::filesystem::path{};
    for (;;) {
        root = std::filesystem::temp_directory_path() /
            ("veyra-directory-enumerator-bench-" + std::to_string(stamp) + "-" +
             std::to_string(sequence.fetch_add(1, std::memory_order_relaxed)));
        std::error_code error;
        if (std::filesystem::create_directory(root, error)) {
            break;
        }
        if (error && error != std::errc::file_exists) {
            throw std::filesystem::filesystem_error("create temporary directory", root, error);
        }
    }
    auto directory = TemporaryDirectory(std::move(root));
    for (std::size_t index = 0; index < entry_count; ++index) {
        std::ofstream file(directory.path / ("entry-" + std::to_string(index) + ".dat"));
        if (!file) {
            throw std::runtime_error("could not create benchmark entry");
        }
    }
    return directory;
}

double percentile(std::span<const double> sorted_times, std::size_t percent) {
    const auto last = sorted_times.size() - 1;
    const auto index = last / 100 * percent + last % 100 * percent / 100;
    return sorted_times[index];
}

std::uint64_t scan(
    veyra::DirectoryEnumerator& enumerator,
    const std::filesystem::path& directory,
    std::size_t expected_entries
) {
    const auto generation = enumerator.start(directory);
    auto entries = std::size_t{0};
    auto checksum = std::uint64_t{14695981039346656037ULL};
    auto complete = false;
    while (!complete) {
        const auto batch = enumerator.poll();
        if (!batch) {
            std::this_thread::sleep_for(std::chrono::microseconds(50));
            continue;
        }
        if (batch->generation != generation) {
            throw std::runtime_error("enumerator returned a stale generation");
        }
        if (batch->event == veyra::DirectoryEnumerationEvent::error) {
            throw std::runtime_error(batch->error.message());
        }
        if (batch->event == veyra::DirectoryEnumerationEvent::complete) {
            complete = true;
            continue;
        }

        for (const auto& entry : batch->entries) {
            if (entry.metadata_error) {
                throw std::runtime_error(entry.metadata_error.message());
            }
            ++entries;
            for (const auto byte : entry.name) {
                checksum ^= static_cast<unsigned char>(byte);
                checksum *= 1099511628211ULL;
            }
        }
    }
    if (entries != expected_entries) {
        throw std::runtime_error("enumerator returned an unexpected entry count");
    }
    return checksum;
}

int run(const Options& options) {
    const auto setup_start = Clock::now();
    auto fixture = make_fixture(options.entries);
    const auto setup_end = Clock::now();
    const auto setup_ms =
        std::chrono::duration<double, std::milli>(setup_end - setup_start).count();

    auto enumerator = veyra::DirectoryEnumerator{};
    std::uint64_t checksum = 0;
    for (std::size_t index = 0; index < options.warmup; ++index) {
        checksum += scan(enumerator, fixture.path, options.entries);
    }

    auto times = std::vector<double>(options.queries);
    for (auto& elapsed : times) {
        const auto begin = Clock::now();
        checksum += scan(enumerator, fixture.path, options.entries);
        const auto end = Clock::now();
        elapsed = std::chrono::duration<double, std::micro>(end - begin).count();
    }
    std::sort(times.begin(), times.end());

    std::cout << "scenario,platform,compiler,build_config,entries,setup_ms,warmup_queries,"
                 "measured_queries,p50_us,p95_us,p99_us,max_us,checksum\n";
    std::cout << "directory_enumeration," << VEYRA_BUILD_PLATFORM << ',' << VEYRA_BUILD_COMPILER
              << ',' << VEYRA_BUILD_CONFIG << ',' << options.entries << ',' << setup_ms << ','
              << options.warmup << ',' << options.queries << ',' << percentile(times, 50) << ','
              << percentile(times, 95) << ',' << percentile(times, 99) << ',' << times.back() << ','
              << checksum << '\n';
    return 0;
}

}

int main(int argc, char** argv) {
    try {
        const auto options = parse_options(argc, argv);
        return run(options);
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
