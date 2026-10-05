#ifndef VEYRA_FILES_DIRECTORY_ENUMERATOR_HPP
#define VEYRA_FILES_DIRECTORY_ENUMERATOR_HPP

#include "veyra/files/file_id.hpp"

#include <array>
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <optional>
#include <stop_token>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

namespace veyra {

struct DirectoryEntry {
    FileId id;
    std::u8string name;
    std::uint64_t size_bytes{};
    std::int64_t modified_nanoseconds{};
    bool is_directory{};
    std::error_code metadata_error;
};

enum class DirectoryEnumerationEvent : std::uint8_t {
    entries,
    complete,
    error,
};

struct DirectoryEnumerationBatch {
    std::uint64_t generation{};
    DirectoryEnumerationEvent event{DirectoryEnumerationEvent::entries};
    std::vector<DirectoryEntry> entries;
    std::error_code error;
};

class DirectoryEnumerator {
  public:
    static constexpr std::size_t entries_per_batch = 64;

    DirectoryEnumerator();
    ~DirectoryEnumerator();

    DirectoryEnumerator(const DirectoryEnumerator&) = delete;
    DirectoryEnumerator& operator=(const DirectoryEnumerator&) = delete;
    DirectoryEnumerator(DirectoryEnumerator&&) = delete;
    DirectoryEnumerator& operator=(DirectoryEnumerator&&) = delete;

    std::uint64_t start(std::filesystem::path directory);
    std::uint64_t cancel() noexcept;
    bool poll(DirectoryEnumerationBatch& batch);

  private:
    struct Request {
        std::uint64_t generation{};
        std::filesystem::path directory;
    };

    static constexpr std::size_t queued_batch_capacity = 4;

    std::uint64_t advance_generation() noexcept;
    void clear_queue() noexcept;
    void run(const std::stop_token& stop_token) noexcept;
    void enumerate(const Request& request, const std::stop_token& stop_token) noexcept;
    bool publish(DirectoryEnumerationBatch& batch, const std::stop_token& stop_token) noexcept;

    std::atomic<std::uint64_t> generation_{};
    std::mutex mutex_;
    std::condition_variable_any work_available_;
    std::condition_variable_any queue_space_available_;
    std::optional<Request> pending_request_;
    std::array<DirectoryEnumerationBatch, queued_batch_capacity> queue_;
    std::size_t queue_head_{};
    std::size_t queue_size_{};
    DirectoryEnumerationBatch working_batch_;
    std::jthread worker_;
};

}

#endif
