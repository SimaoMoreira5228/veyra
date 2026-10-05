#include "veyra/files/directory_enumerator.hpp"
#include "veyra/files/directory_history.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace {

void require(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

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

std::vector<veyra::DirectoryEnumerationBatch> collect(
    veyra::DirectoryEnumerator& enumerator,
    std::uint64_t generation,
    bool expect_error = false
) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    auto batches = std::vector<veyra::DirectoryEnumerationBatch>{};
    auto batch = veyra::DirectoryEnumerationBatch{};
    auto complete = false;
    while (std::chrono::steady_clock::now() < deadline && !complete) {
        if (!enumerator.poll(batch)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }

        require(batch.generation == generation, "only the current generation is delivered");
        if (batch.event == veyra::DirectoryEnumerationEvent::error) {
            require(expect_error, batch.error.message());
            complete = true;
        }
        complete = complete || batch.event == veyra::DirectoryEnumerationEvent::complete;
        batches.push_back(batch);
    }
    require(complete, "enumeration completes before the timeout");
    return batches;
}

TemporaryDirectory make_fixture() {
    static auto sequence = std::atomic<std::uint64_t>{};
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    auto root = std::filesystem::path{};
    for (;;) {
        root = std::filesystem::temp_directory_path() /
            ("veyra-directory-enumerator-" + std::to_string(stamp) + "-" +
             std::to_string(sequence.fetch_add(1, std::memory_order_relaxed)));
        std::error_code error;
        if (std::filesystem::create_directory(root, error)) {
            break;
        }
        if (error && error != std::errc::file_exists) {
            throw std::filesystem::filesystem_error("create temporary directory", root, error);
        }
    }
    auto result = TemporaryDirectory(std::move(root));
    std::filesystem::create_directories(result.path / "many");
    std::filesystem::create_directories(result.path / "latest");
    std::filesystem::create_directory(result.path / "many" / "folder");

    for (auto index = 0; index < 260; ++index) {
        std::ofstream file(result.path / "many" / ("entry-" + std::to_string(index) + ".dat"));
        require(file.good(), "fixture entry is created");
    }

    std::ofstream latest(result.path / "latest" / "only.txt");
    latest << "veyra";
    require(latest.good(), "latest fixture entry is created");
    std::ofstream unicode(result.path / "latest" / std::filesystem::path{u8"café.txt"});
    require(unicode.good(), "UTF-8 fixture entry is created");
    return result;
}

void check_streaming_and_supersession() {
    auto fixture = make_fixture();
    auto enumerator = veyra::DirectoryEnumerator{};

    const auto stale_generation = enumerator.start(fixture.path / "many");
    const auto latest_generation = enumerator.start(fixture.path / "latest");
    require(latest_generation > stale_generation, "each request gets a newer generation");

    const auto latest_batches = collect(enumerator, latest_generation);
    require(latest_batches.size() == 2, "a small directory produces entries and completion");
    require(
        latest_batches[0].event == veyra::DirectoryEnumerationEvent::entries,
        "entries arrive first"
    );
    require(latest_batches[0].entries.size() == 2, "the newest directory has two entries");
    const auto has_only = std::ranges::any_of(latest_batches[0].entries, [](const auto& entry) {
        return entry.name == u8"only.txt" && entry.size_bytes == 5;
    });
    const auto has_unicode = std::ranges::any_of(latest_batches[0].entries, [](const auto& entry) {
        return entry.name == u8"café.txt";
    });
    require(has_only, "file names and sizes are reported");
    require(has_unicode, "non-ASCII entry names use UTF-8");
    require(
        std::ranges::all_of(
            latest_batches[0].entries,
            [](const auto& entry) {
                return !entry.metadata_error;
            }
        ),
        "available metadata has no error"
    );
    require(
        latest_batches[1].event == veyra::DirectoryEnumerationEvent::complete,
        "completion is explicit"
    );

    const auto streamed_generation = enumerator.start(fixture.path / "many");
    const auto streamed_batches = collect(enumerator, streamed_generation);
    auto names = std::set<std::u8string>{};
    auto entry_batch_count = std::size_t{0};
    auto has_directory = false;
    for (const auto& batch : streamed_batches) {
        if (batch.event != veyra::DirectoryEnumerationEvent::entries) {
            continue;
        }
        ++entry_batch_count;
        require(
            batch.entries.size() <= veyra::DirectoryEnumerator::entries_per_batch,
            "entry batches stay within the configured bound"
        );
        for (const auto& entry : batch.entries) {
            names.emplace(entry.name);
            has_directory = has_directory || (entry.name == u8"folder" && entry.is_directory);
        }
    }
    require(entry_batch_count == 5, "large directories are split into bounded batches");
    require(names.size() == 261, "streaming returns every file and directory");
    require(has_directory, "directories are included with their type");
}

void check_cancel_and_errors() {
    auto fixture = make_fixture();
    auto enumerator = veyra::DirectoryEnumerator{};

    const auto generation = enumerator.start(fixture.path / "many");
    const auto canceled_generation = enumerator.cancel();
    require(canceled_generation > generation, "cancellation advances the generation");
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    auto batch = veyra::DirectoryEnumerationBatch{};
    batch.generation = 42;
    require(!enumerator.poll(batch), "canceled generations do not publish results");
    require(batch.generation == 42, "an empty poll leaves the output unchanged");

    const auto missing_generation = enumerator.start(fixture.path / "missing");
    const auto batches = collect(enumerator, missing_generation, true);
    require(batches.size() == 1, "a missing directory produces one terminal event");
    require(
        batches[0].event == veyra::DirectoryEnumerationEvent::error,
        "filesystem errors are reported"
    );
    require(static_cast<bool>(batches[0].error), "the error event carries the filesystem error");

    const auto relative_generation = enumerator.start("relative-directory");
    const auto relative_batches = collect(enumerator, relative_generation, true);
    require(relative_batches.size() == 1, "relative paths fail before filesystem access");
    require(
        relative_batches[0].error == std::errc::invalid_argument,
        "enumeration requires an absolute path"
    );
}

void check_directory_history() {
    auto fixture = make_fixture();
    auto storage = std::array<std::filesystem::path, 3>{};
    auto history = veyra::DirectoryHistory(storage);
    require(history.current() == nullptr, "empty history has no current directory");
    require(!history.go_back() && !history.go_forward(), "empty history cannot move");
    require(
        history.navigate("relative") == veyra::DirectoryHistoryResult::invalid_path,
        "history rejects relative paths"
    );

    const auto root = fixture.path;
    const auto many = fixture.path / "many";
    const auto latest = fixture.path / "latest";
    const auto folder = fixture.path / "many" / "folder";
    require(
        history.navigate(root) == veyra::DirectoryHistoryResult::complete,
        "history accepts an absolute path"
    );
    require(
        history.navigate(many) == veyra::DirectoryHistoryResult::complete,
        "history records navigation"
    );
    require(
        history.navigate(latest) == veyra::DirectoryHistoryResult::complete,
        "history records later navigation"
    );
    require(history.go_back() && *history.current() == many, "back returns to the prior path");
    require(
        history.navigate(folder) == veyra::DirectoryHistoryResult::complete,
        "new navigation branches from the current path"
    );
    require(!history.go_forward(), "new navigation drops the old forward path");

    require(history.go_back() && *history.current() == many, "back follows the new branch");
    require(history.go_back() && *history.current() == root, "back reaches the oldest path");
    require(!history.go_back(), "back stops at the oldest retained path");
    require(history.go_forward() && *history.current() == many, "forward advances the path");
    require(
        history.go_forward() && *history.current() == folder,
        "forward reaches the latest path"
    );

    require(
        history.navigate(latest) == veyra::DirectoryHistoryResult::complete,
        "full history accepts a new path"
    );
    require(
        history.go_back() && *history.current() == folder,
        "full history evicts its oldest path"
    );
    require(history.go_back() && *history.current() == many, "retained paths keep their order");
    require(!history.go_back(), "evicted path is no longer reachable");

    history.clear();
    require(history.current() == nullptr, "clear forgets every path");

    auto no_storage = veyra::DirectoryHistory(std::span<std::filesystem::path>{});
    require(
        no_storage.navigate(root) == veyra::DirectoryHistoryResult::insufficient_storage,
        "history reports missing caller storage"
    );

    auto single_path_storage = std::array<std::filesystem::path, 1>{};
    auto single_path_history = veyra::DirectoryHistory(single_path_storage);
    require(
        single_path_history.navigate(root) == veyra::DirectoryHistoryResult::complete,
        "one-slot history accepts its first path"
    );
    require(
        single_path_history.navigate(many) == veyra::DirectoryHistoryResult::complete,
        "one-slot history replaces its oldest path"
    );
    require(
        *single_path_history.current() == many && !single_path_history.go_back(),
        "one-slot history retains only its latest path"
    );
}

}

int main() {
    try {
        check_streaming_and_supersession();
        check_cancel_and_errors();
        check_directory_history();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
