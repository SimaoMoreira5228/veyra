#include "veyra/files/directory_enumerator.hpp"

#include <chrono>
#include <new>
#include <utility>

namespace veyra {
namespace {

DirectoryEnumerationBatch
event(std::uint64_t generation, DirectoryEnumerationEvent kind, std::error_code error = {}) {
    return {
        .generation = generation,
        .event = kind,
        .entries = {},
        .error = error,
    };
}

}

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
        batch.reset();
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

std::optional<DirectoryEnumerationBatch> DirectoryEnumerator::poll() {
    std::lock_guard lock(mutex_);
    if (queue_size_ == 0) {
        return std::nullopt;
    }

    auto batch = std::move(queue_[queue_head_]);
    queue_[queue_head_].reset();
    queue_head_ = (queue_head_ + 1) % queued_batch_capacity;
    --queue_size_;
    queue_space_available_.notify_one();
    return batch;
}

bool DirectoryEnumerator::publish(
    DirectoryEnumerationBatch batch,
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
    queue_[index].emplace(std::move(batch));
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
        }

        enumerate(request, stop_token);
    }
}

void DirectoryEnumerator::enumerate(
    const Request& request,
    const std::stop_token& stop_token
) noexcept {
    try {
        if (!request.directory.is_absolute()) {
            publish(
                event(
                    request.generation,
                    DirectoryEnumerationEvent::error,
                    std::make_error_code(std::errc::invalid_argument)
                ),
                stop_token
            );
            return;
        }

        std::error_code error;
        auto iterator = std::filesystem::directory_iterator(request.directory, error);
        if (error) {
            publish(event(request.generation, DirectoryEnumerationEvent::error, error), stop_token);
            return;
        }

        const auto end = std::filesystem::directory_iterator{};
        auto entries = std::vector<DirectoryEntry>{};
        entries.reserve(entries_per_batch);
        while (iterator != end) {
            if (stop_token.stop_requested() ||
                generation_.load(std::memory_order_acquire) != request.generation) {
                return;
            }

            const auto& item = *iterator;
            auto entry_error = std::error_code{};
            auto entry = DirectoryEntry{
                .name = item.path().filename().u8string(),
            };
            entry.is_directory = item.is_directory(entry_error);
            if (entry_error) {
                entry.metadata_error = entry_error;
            } else {
                const auto is_regular_file = item.is_regular_file(entry_error);
                if (entry_error) {
                    entry.metadata_error = entry_error;
                } else if (is_regular_file) {
                    entry.size_bytes = item.file_size(entry_error);
                    if (entry_error) {
                        entry.metadata_error = entry_error;
                    }
                }

                if (!entry_error) {
                    const auto modified = item.last_write_time(entry_error);
                    if (entry_error) {
                        entry.metadata_error = entry_error;
                    } else {
                        entry.modified_nanoseconds =
                            std::chrono::duration_cast<std::chrono::nanoseconds>(
                                std::chrono::file_clock::to_sys(modified).time_since_epoch()
                            )
                                .count();
                    }
                }
            }
            entries.push_back(std::move(entry));

            if (entries.size() == entries_per_batch) {
                if (!publish(
                        {
                            .generation = request.generation,
                            .event = DirectoryEnumerationEvent::entries,
                            .entries = std::move(entries),
                        },
                        stop_token
                    )) {
                    return;
                }
                entries = {};
                entries.reserve(entries_per_batch);
            }

            if (stop_token.stop_requested() ||
                generation_.load(std::memory_order_acquire) != request.generation) {
                return;
            }

            iterator.increment(error);
            if (error) {
                if (!entries.empty() &&
                    !publish(
                        {
                            .generation = request.generation,
                            .event = DirectoryEnumerationEvent::entries,
                            .entries = std::move(entries),
                        },
                        stop_token
                    )) {
                    return;
                }
                publish(
                    event(request.generation, DirectoryEnumerationEvent::error, error),
                    stop_token
                );
                return;
            }
        }

        if (!entries.empty() &&
            !publish(
                {
                    .generation = request.generation,
                    .event = DirectoryEnumerationEvent::entries,
                    .entries = std::move(entries),
                },
                stop_token
            )) {
            return;
        }

        publish(event(request.generation, DirectoryEnumerationEvent::complete), stop_token);
    } catch (const std::filesystem::filesystem_error& error) {
        publish(
            event(request.generation, DirectoryEnumerationEvent::error, error.code()),
            stop_token
        );
    } catch (const std::bad_alloc&) {
        publish(
            event(
                request.generation,
                DirectoryEnumerationEvent::error,
                std::make_error_code(std::errc::not_enough_memory)
            ),
            stop_token
        );
    } catch (...) {
        publish(
            event(
                request.generation,
                DirectoryEnumerationEvent::error,
                std::make_error_code(std::errc::io_error)
            ),
            stop_token
        );
    }
}

}
