#include "veyra/files/enumeration/directory_watcher.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <string_view>
#include <thread>

namespace {

using namespace std::chrono_literals;

void require(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

class TemporaryDirectory {
  public:
    TemporaryDirectory() {
        static auto sequence = std::atomic<std::uint64_t>{};
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        path_ = std::filesystem::temp_directory_path() /
            ("veyra-directory-watcher-" + std::to_string(stamp) + "-" +
             std::to_string(sequence.fetch_add(1, std::memory_order_relaxed)));
        std::error_code error;
        if (!std::filesystem::create_directory(path_, error)) {
            throw std::filesystem::filesystem_error("create temporary directory", path_, error);
        }
    }

    ~TemporaryDirectory() {
        std::error_code error;
        std::filesystem::remove_all(path_, error);
    }

    const std::filesystem::path& path() const noexcept {
        return path_;
    }

  private:
    std::filesystem::path path_;
};

void check_changes_are_coalesced() {
    auto fixture = TemporaryDirectory{};
    auto error = std::error_code{};
    auto watcher = veyra::DirectoryWatcher(fixture.path(), error);
    require(!error, "watcher opens an existing directory");

    auto writer = std::jthread([directory = fixture.path()] {
        std::this_thread::sleep_for(10ms);
        const auto path = directory / "entry.txt";
        {
            auto file = std::ofstream(path);
            file << "first";
        }
        std::this_thread::sleep_for(5ms);
        {
            auto file = std::ofstream(path, std::ios::app);
            file << "second";
        }
        std::filesystem::remove(path);
    });

    const auto result = watcher.wait_for_change({}, 2s, error);
    writer.join();
    require(result == veyra::DirectoryWatchResult::changed, "filesystem changes wake the watcher");
    require(!error, "a normal change has no watcher error");
    require(
        watcher.wait_for_change({}, 20ms, error) == veyra::DirectoryWatchResult::timeout,
        "one change burst produces one notification"
    );
}

void check_timeout_and_stop() {
    auto fixture = TemporaryDirectory{};
    auto error = std::error_code{};
    auto watcher = veyra::DirectoryWatcher(fixture.path(), error);
    require(!error, "watcher opens before waiting");
    require(
        watcher.wait_for_change({}, 5ms, error) == veyra::DirectoryWatchResult::timeout,
        "wait returns when its timeout expires"
    );

    auto stop_source = std::stop_source{};
    auto stopper = std::jthread([&stop_source] {
        std::this_thread::sleep_for(10ms);
        stop_source.request_stop();
    });
    require(
        watcher.wait_for_change(stop_source.get_token(), 2s, error) ==
            veyra::DirectoryWatchResult::stopped,
        "stop requests interrupt a blocked wait"
    );
    stopper.join();
    require(
        watcher.wait_for_change({}, 5ms, error) == veyra::DirectoryWatchResult::timeout,
        "a completed stop does not affect the next wait"
    );
}

void check_invalid_path() {
    auto error = std::error_code{};
    auto watcher = veyra::DirectoryWatcher("relative", error);
    require(static_cast<bool>(error), "watcher rejects relative directories");
    require(
        watcher.wait_for_change({}, 1ms, error) == veyra::DirectoryWatchResult::error,
        "invalid watcher setup stays in the error state"
    );

    auto fixture = TemporaryDirectory{};
    auto missing_watcher = veyra::DirectoryWatcher(fixture.path() / "missing", error);
    require(static_cast<bool>(error), "watcher reports an unavailable directory");
    require(
        missing_watcher.wait_for_change({}, 1ms, error) == veyra::DirectoryWatchResult::error,
        "an unavailable directory cannot be polled"
    );

    const auto file_path = fixture.path() / "file.txt";
    std::ofstream(file_path).put('x');
    auto file_watcher = veyra::DirectoryWatcher(file_path, error);
    require(static_cast<bool>(error), "watcher accepts directories but not files");
}

}

int main() {
    try {
        check_changes_are_coalesced();
        check_timeout_and_stop();
        check_invalid_path();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
