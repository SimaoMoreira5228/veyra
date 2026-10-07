#include "veyra/files/file_thumbnail_queue.hpp"

#include <utility>

namespace veyra {
namespace {

void clear_thumbnail_request(FileThumbnailRequest& request) noexcept {
    request.path.clear();
    request.key = {};
    request.priority = 0;
    request.generation = 0;
}

}

FileThumbnailQueue::FileThumbnailQueue(std::span<FileThumbnailRequest> storage) noexcept :
    storage_(storage) {
}

std::uint64_t FileThumbnailQueue::reset() noexcept {
    std::lock_guard lock(mutex_);
    ++generation_;
    if (generation_ == 0) {
        generation_ = 1;
    }
    clear();
    work_available_.notify_all();
    return generation_;
}

FileThumbnailQueueResult FileThumbnailQueue::submit(
    FileThumbnailKey key,
    std::filesystem::path path,
    std::uint32_t priority
) {
    if (!key.file_id.valid()) {
        return FileThumbnailQueueResult::invalid_file_id;
    }
    if (!path.is_absolute()) {
        return FileThumbnailQueueResult::invalid_path;
    }

    std::lock_guard lock(mutex_);
    for (std::size_t index = 0; index < count_; ++index) {
        if (storage_[index].key.file_id == key.file_id) {
            storage_[index] = {
                .key = key,
                .path = std::move(path),
                .priority = priority,
                .generation = generation_,
            };
            work_available_.notify_one();
            return FileThumbnailQueueResult::updated;
        }
    }

    if (count_ < storage_.size()) {
        storage_[count_] = {
            .key = key,
            .path = std::move(path),
            .priority = priority,
            .generation = generation_,
        };
        ++count_;
        work_available_.notify_one();
        return FileThumbnailQueueResult::queued;
    }

    if (storage_.empty()) {
        return FileThumbnailQueueResult::queue_full;
    }

    auto worst_priority_index = std::size_t{0};
    for (std::size_t index = 1; index < count_; ++index) {
        if (storage_[index].priority > storage_[worst_priority_index].priority) {
            worst_priority_index = index;
        }
    }
    if (priority >= storage_[worst_priority_index].priority) {
        return FileThumbnailQueueResult::queue_full;
    }

    storage_[worst_priority_index] = {
        .key = key,
        .path = std::move(path),
        .priority = priority,
        .generation = generation_,
    };
    work_available_.notify_one();
    return FileThumbnailQueueResult::replaced_lower_priority;
}

bool FileThumbnailQueue::cancel(FileId file_id) noexcept {
    std::lock_guard lock(mutex_);
    auto index = std::size_t{0};
    while (index < count_ && storage_[index].key.file_id != file_id) {
        ++index;
    }
    if (index == count_) {
        return false;
    }

    --count_;
    if (index != count_) {
        storage_[index] = std::move(storage_[count_]);
    }
    clear_thumbnail_request(storage_[count_]);
    return true;
}

std::optional<FileThumbnailRequest>
FileThumbnailQueue::wait_next(const std::stop_token& stop_token) {
    std::unique_lock lock(mutex_);
    const auto has_work = work_available_.wait(lock, stop_token, [this] {
        return count_ != 0;
    });
    if (!has_work || stop_token.stop_requested()) {
        return std::nullopt;
    }

    const auto next_index = best_priority_index();
    auto request = std::optional<FileThumbnailRequest>{std::move(storage_[next_index])};
    --count_;
    if (next_index != count_) {
        storage_[next_index] = std::move(storage_[count_]);
    }
    clear_thumbnail_request(storage_[count_]);
    return request;
}

bool FileThumbnailQueue::is_current(std::uint64_t generation) const noexcept {
    std::lock_guard lock(mutex_);
    return generation_ == generation;
}

std::size_t FileThumbnailQueue::pending() const noexcept {
    std::lock_guard lock(mutex_);
    return count_;
}

void FileThumbnailQueue::clear() noexcept {
    for (std::size_t index = 0; index < count_; ++index) {
        clear_thumbnail_request(storage_[index]);
    }
    count_ = 0;
}

std::size_t FileThumbnailQueue::best_priority_index() const noexcept {
    auto result = std::size_t{0};
    for (std::size_t index = 1; index < count_; ++index) {
        if (storage_[index].priority < storage_[result].priority) {
            result = index;
        }
    }
    return result;
}

}
