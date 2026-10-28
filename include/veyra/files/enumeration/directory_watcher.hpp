#ifndef VEYRA_FILES_DIRECTORY_WATCHER_HPP
#define VEYRA_FILES_DIRECTORY_WATCHER_HPP

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <stop_token>
#include <system_error>

namespace veyra {

enum class DirectoryWatchResult : std::uint8_t {
    changed,
    timeout,
    stopped,
    error,
};

class DirectoryWatcher {
  public:
    DirectoryWatcher(const std::filesystem::path& directory, std::error_code& error) noexcept;
    ~DirectoryWatcher();

    DirectoryWatcher(const DirectoryWatcher&) = delete;
    DirectoryWatcher& operator=(const DirectoryWatcher&) = delete;
    DirectoryWatcher(DirectoryWatcher&&) = delete;
    DirectoryWatcher& operator=(DirectoryWatcher&&) = delete;

    DirectoryWatchResult wait_for_change(
        const std::stop_token& stop_token,
        std::chrono::milliseconds timeout,
        std::error_code& error
    ) noexcept;

  private:
    void close() noexcept;

    std::error_code error_;
#ifdef _WIN32
    std::intptr_t directory_handle_{-1};
    std::intptr_t completion_event_{-1};
    std::intptr_t cancellation_event_{-1};
#elif defined(__linux__)
    int notification_descriptor_{-1};
    int cancellation_descriptor_{-1};
    int watch_descriptor_{-1};
#endif
};

}

namespace veyra::files {
using ::veyra::DirectoryWatchResult;
using ::veyra::DirectoryWatcher;
}

namespace veyra::files::enumeration {
using ::veyra::DirectoryWatchResult;
using ::veyra::DirectoryWatcher;
}

#endif
