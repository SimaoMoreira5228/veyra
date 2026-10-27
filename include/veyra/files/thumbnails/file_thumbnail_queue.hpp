#ifndef VEYRA_FILES_FILE_THUMBNAIL_QUEUE_HPP
#define VEYRA_FILES_FILE_THUMBNAIL_QUEUE_HPP

#include "veyra/files/thumbnails/file_thumbnail_cache.hpp"

#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <optional>
#include <span>
#include <stop_token>

namespace veyra {

struct FileThumbnailRequest {
    FileThumbnailKey key;
    std::filesystem::path path;
    std::uint32_t priority{};
    std::uint64_t generation{};
};

enum class FileThumbnailQueueResult : std::uint8_t {
    queued,
    updated,
    replaced_lower_priority,
    invalid_file_id,
    invalid_path,
    queue_full,
};

class FileThumbnailQueue {
  public:
    explicit FileThumbnailQueue(std::span<FileThumbnailRequest> storage) noexcept;

    FileThumbnailQueue(const FileThumbnailQueue&) = delete;
    FileThumbnailQueue& operator=(const FileThumbnailQueue&) = delete;
    FileThumbnailQueue(FileThumbnailQueue&&) = delete;
    FileThumbnailQueue& operator=(FileThumbnailQueue&&) = delete;

    std::uint64_t reset() noexcept;
    FileThumbnailQueueResult
    submit(FileThumbnailKey key, std::filesystem::path path, std::uint32_t priority);
    bool cancel(FileId file_id) noexcept;
    std::optional<FileThumbnailRequest> wait_next(const std::stop_token& stop_token);
    bool is_current(std::uint64_t generation) const noexcept;
    std::size_t pending() const noexcept;

  private:
    void clear() noexcept;
    std::size_t best_priority_index() const noexcept;

    std::span<FileThumbnailRequest> storage_;
    mutable std::mutex mutex_;
    std::condition_variable_any work_available_;
    std::size_t count_{};
    std::uint64_t generation_{1};
};

}

#endif
