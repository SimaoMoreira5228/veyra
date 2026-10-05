#include "veyra/files/file_thumbnail_queue.hpp"

#include <array>
#include <iostream>
#include <optional>
#include <semaphore>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>

namespace {

void require(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

std::filesystem::path image_path(std::string_view name) {
    return std::filesystem::temp_directory_path() / name;
}

void check_priority_and_capacity() {
    auto storage = std::array<veyra::FileThumbnailRequest, 3>{};
    auto queue = veyra::FileThumbnailQueue(storage);
    const auto first_generation = queue.reset();
    require(
        queue.submit({.file_id = 1}, image_path("first.png"), 10) ==
            veyra::FileThumbnailQueueResult::queued,
        "first request enters the queue"
    );
    require(
        queue.submit({.file_id = 2}, image_path("second.png"), 4) ==
            veyra::FileThumbnailQueueResult::queued,
        "second request enters the queue"
    );
    require(
        queue.submit({.file_id = 3}, image_path("third.png"), 1) ==
            veyra::FileThumbnailQueueResult::queued,
        "third request fills the queue"
    );
    require(
        queue.submit({.file_id = 4}, image_path("background.png"), 20) ==
            veyra::FileThumbnailQueueResult::queue_full,
        "a lower-priority request cannot displace visible work"
    );
    require(
        queue.submit({.file_id = 4}, image_path("visible.png"), 0) ==
            veyra::FileThumbnailQueueResult::replaced_lower_priority,
        "a higher-priority request displaces the least visible work"
    );

    const auto first = queue.wait_next({});
    const auto second = queue.wait_next({});
    const auto third = queue.wait_next({});
    if (!first || !second || !third) {
        throw std::runtime_error("all retained requests can be taken");
    }
    require(first->key.file_id == 4, "lowest numeric priority is handled first");
    require(second->key.file_id == 3, "next visible request follows");
    require(third->key.file_id == 2, "displaced request is not returned");
    require(first->generation == first_generation, "requests carry their view generation");
    require(queue.pending() == 0, "taking work frees queue slots");
}

void check_updates_and_generation_changes() {
    auto storage = std::array<veyra::FileThumbnailRequest, 2>{};
    auto queue = veyra::FileThumbnailQueue(storage);
    const auto first_key = veyra::FileThumbnailKey{
        .file_id = 7,
        .size_bytes = 12,
        .modified_nanoseconds = 14,
    };
    auto updated_key = first_key;
    ++updated_key.size_bytes;
    ++updated_key.modified_nanoseconds;
    require(
        queue.submit(first_key, image_path("old.png"), 15) ==
            veyra::FileThumbnailQueueResult::queued,
        "first version enters the queue"
    );
    require(
        queue.submit(updated_key, image_path("new.png"), 2) ==
            veyra::FileThumbnailQueueResult::updated,
        "new metadata replaces pending work for the same file"
    );
    require(queue.pending() == 1, "updated file does not occupy two slots");

    const auto request = queue.wait_next({});
    if (!request) {
        throw std::runtime_error("updated request can be taken");
    }
    require(request->key == updated_key, "worker receives current file metadata");
    require(request->path == image_path("new.png"), "worker receives the current path");

    const auto next_generation = queue.reset();
    require(next_generation != request->generation, "reset advances the generation");
    require(!queue.is_current(request->generation), "completed old work can be discarded");
    require(queue.pending() == 0, "reset cancels queued requests");
}

void check_worker_wait_and_stop() {
    auto storage = std::array<veyra::FileThumbnailRequest, 1>{};
    auto queue = veyra::FileThumbnailQueue(storage);
    auto worker_started = std::binary_semaphore{0};
    auto result = std::optional<veyra::FileThumbnailRequest>{};
    auto worker = std::jthread([&](const std::stop_token& token) {
        worker_started.release();
        result = queue.wait_next(token);
    });

    worker_started.acquire();
    queue.submit({.file_id = 9}, image_path("worker.png"), 0);
    worker.join();
    require(result && result->key.file_id == 9, "waiting worker wakes for new work");

    auto stopped_worker = std::jthread([&](const std::stop_token& token) {
        result = queue.wait_next(token);
    });
    stopped_worker.request_stop();
    stopped_worker.join();
    require(!result, "stopped worker leaves without a request");
}

void check_invalid_paths_and_empty_storage() {
    auto storage = std::array<veyra::FileThumbnailRequest, 1>{};
    auto queue = veyra::FileThumbnailQueue(storage);
    require(
        queue.submit({.file_id = 1}, "relative.png", 0) ==
            veyra::FileThumbnailQueueResult::invalid_path,
        "queue rejects relative paths"
    );

    auto empty_queue = veyra::FileThumbnailQueue(std::span<veyra::FileThumbnailRequest>{});
    require(
        empty_queue.submit({.file_id = 1}, image_path("empty.png"), 0) ==
            veyra::FileThumbnailQueueResult::queue_full,
        "empty storage rejects requests"
    );
}

}

int main() {
    try {
        check_priority_and_capacity();
        check_updates_and_generation_changes();
        check_worker_wait_and_stop();
        check_invalid_paths_and_empty_storage();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
