#include "veyra/files/directory_enumerator.hpp"
#include "file_metadata.hpp"

#include <new>
#include <utility>

namespace veyra {

DirectoryEnumerator::DirectoryEnumerator() :
    worker_([this](const std::stop_token& token) {
        run(token);
    }) {
}

DirectoryEnumerator::~DirectoryEnumerator() {
    worker_.request_stop();
    work_available_.notify_all();
    queue_space_available_.notify_all();
}

std::uint64_t DirectoryEnumerator::advance_generation() noexcept {
    auto next = generation_.load(std::memory_order_relaxed) + 1;
    if (next == 0) {
        next = 1;
    }
    generation_.store(next, std::memory_order_release);
    return next;
}

void DirectoryEnumerator::clear_queue() noexcept {
    for (auto& batch : queue_) {
        batch.generation = 0;
        batch.event = DirectoryEnumerationEvent::entries;
        batch.entries.clear();
        batch.error.clear();
    }
    queue_head_ = 0;
    queue_size_ = 0;
}

std::uint64_t DirectoryEnumerator::start(std::filesystem::path directory) {
    std::lock_guard lock(mutex_);
    const auto generation = advance_generation();
    pending_request_.emplace(
        Request{
            .generation = generation,
            .directory = std::move(directory),
        }
    );
    clear_queue();
    queue_space_available_.notify_all();
    work_available_.notify_one();
    return generation;
}

std::uint64_t DirectoryEnumerator::cancel() noexcept {
    std::lock_guard lock(mutex_);
    const auto generation = advance_generation();
    pending_request_.reset();
    clear_queue();
    queue_space_available_.notify_all();
    return generation;
}

bool DirectoryEnumerator::poll(DirectoryEnumerationBatch& output) {
    std::lock_guard lock(mutex_);
    if (queue_size_ == 0) {
        return false;
    }

    auto& batch = queue_[queue_head_];
    output.generation = batch.generation;
    output.event = batch.event;
    output.error = batch.error;
    output.entries.swap(batch.entries);
    batch.entries.clear();
    batch.generation = 0;
    batch.event = DirectoryEnumerationEvent::entries;
    batch.error.clear();
    queue_head_ = (queue_head_ + 1) % queued_batch_capacity;
    --queue_size_;
    queue_space_available_.notify_one();
    return true;
}

bool DirectoryEnumerator::publish(
    DirectoryEnumerationBatch& batch,
    const std::stop_token& stop_token
) noexcept {
    std::unique_lock lock(mutex_);
    const auto has_space = queue_space_available_.wait(lock, stop_token, [this, &batch] {
        return queue_size_ < queued_batch_capacity ||
            generation_.load(std::memory_order_acquire) != batch.generation;
    });
    if (!has_space || stop_token.stop_requested() ||
        generation_.load(std::memory_order_acquire) != batch.generation) {
        return false;
    }

    const auto index = (queue_head_ + queue_size_) % queued_batch_capacity;
    auto& slot = queue_[index];
    slot.generation = batch.generation;
    slot.event = batch.event;
    slot.error = batch.error;
    slot.entries.swap(batch.entries);
    ++queue_size_;
    return true;
}

void DirectoryEnumerator::run(const std::stop_token& stop_token) noexcept {
    while (!stop_token.stop_requested()) {
        auto request = Request{};
        {
            std::unique_lock lock(mutex_);
            const auto has_request = work_available_.wait(lock, stop_token, [this] {
                return pending_request_.has_value();
            });
            if (!has_request || stop_token.stop_requested()) {
                return;
            }
            if (!pending_request_) {
                continue;
            }
            request = std::move(*pending_request_);
            pending_request_.reset();

            auto best_buffer_index = std::size_t{0};
            for (std::size_t index = 1; index < queued_batch_capacity; ++index) {
                if (queue_[index].entries.capacity() >
                    queue_[best_buffer_index].entries.capacity()) {
                    best_buffer_index = index;
                }
            }
            if (queue_[best_buffer_index].entries.capacity() > working_batch_.entries.capacity()) {
                working_batch_.entries.swap(queue_[best_buffer_index].entries);
            }
        }

        enumerate(request, stop_token);
    }
}

void DirectoryEnumerator::enumerate(
    const Request& request,
    const std::stop_token& stop_token
) noexcept {
    auto& batch = working_batch_;
    batch.generation = request.generation;
    batch.event = DirectoryEnumerationEvent::entries;
    batch.entries.clear();
    batch.error.clear();
    try {
        if (!request.directory.is_absolute()) {
            batch.event = DirectoryEnumerationEvent::error;
            batch.error = std::make_error_code(std::errc::invalid_argument);
            publish(batch, stop_token);
            return;
        }

        std::error_code error;
        auto iterator = std::filesystem::directory_iterator(request.directory, error);
        if (error) {
            batch.event = DirectoryEnumerationEvent::error;
            batch.error = error;
            publish(batch, stop_token);
            return;
        }

        const auto end = std::filesystem::directory_iterator{};
        batch.entries.reserve(entries_per_batch);
        while (iterator != end) {
            if (stop_token.stop_requested() ||
                generation_.load(std::memory_order_acquire) != request.generation) {
                return;
            }

            const auto& item = *iterator;
            const auto metadata = read_file_metadata(item.path());
            auto entry = DirectoryEntry{
                .id = metadata.id,
                .name = item.path().filename().u8string(),
                .size_bytes = metadata.size_bytes,
                .modified_nanoseconds = metadata.modified_nanoseconds,
                .is_directory = metadata.is_directory,
                .metadata_error = metadata.error,
            };
            batch.entries.push_back(std::move(entry));

            if (batch.entries.size() == entries_per_batch) {
                batch.event = DirectoryEnumerationEvent::entries;
                if (!publish(batch, stop_token)) {
                    return;
                }
                batch.entries.clear();
            }

            if (stop_token.stop_requested() ||
                generation_.load(std::memory_order_acquire) != request.generation) {
                return;
            }

            iterator.increment(error);
            if (error) {
                if (!batch.entries.empty()) {
                    batch.event = DirectoryEnumerationEvent::entries;
                    if (!publish(batch, stop_token)) {
                        return;
                    }
                }
                batch.event = DirectoryEnumerationEvent::error;
                batch.error = error;
                batch.entries.clear();
                publish(batch, stop_token);
                return;
            }
        }

        if (!batch.entries.empty()) {
            batch.event = DirectoryEnumerationEvent::entries;
            if (!publish(batch, stop_token)) {
                return;
            }
        }

        batch.event = DirectoryEnumerationEvent::complete;
        batch.error.clear();
        batch.entries.clear();
        publish(batch, stop_token);
    } catch (const std::filesystem::filesystem_error& error) {
        batch.event = DirectoryEnumerationEvent::error;
        batch.entries.clear();
        batch.error = error.code();
        publish(batch, stop_token);
    } catch (const std::bad_alloc&) {
        batch.event = DirectoryEnumerationEvent::error;
        batch.entries.clear();
        batch.error = std::make_error_code(std::errc::not_enough_memory);
        publish(batch, stop_token);
    } catch (...) {
        batch.event = DirectoryEnumerationEvent::error;
        batch.entries.clear();
        batch.error = std::make_error_code(std::errc::io_error);
        publish(batch, stop_token);
    }
}

}
