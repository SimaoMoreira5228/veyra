#include "veyra/chat/message/message_change_queue.hpp"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <iostream>
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

void check_validation_and_capacity() {
    auto empty_queue = veyra::ChatMessageChangeQueue({});
    auto change = veyra::ChatMessageChange{veyra::ChatMessageChangeKind::edited, 99};
    require(!empty_queue.valid(), "empty storage is invalid");
    require(
        empty_queue.try_push(change) == veyra::ChatMessageChangeQueueResult::invalid_workspace,
        "empty storage rejects pushes"
    );
    require(!empty_queue.try_pop(change), "empty storage rejects pops");
    require(change.message_id == 99, "failed pop leaves output unchanged");

    auto storage = std::array<veyra::ChatMessageChange, 2>{};
    auto queue = veyra::ChatMessageChangeQueue(storage);
    require(queue.valid() && queue.capacity() == storage.size(), "caller storage sets capacity");
    require(
        queue.try_push({veyra::ChatMessageChangeKind::appended, 0}) ==
            veyra::ChatMessageChangeQueueResult::invalid_change,
        "zero message IDs are rejected"
    );
    require(
        queue.try_push({veyra::ChatMessageChangeKind::appended, 1}) ==
            veyra::ChatMessageChangeQueueResult::queued,
        "valid changes are queued"
    );
    require(
        queue.try_push({veyra::ChatMessageChangeKind::edited, 2}) ==
            veyra::ChatMessageChangeQueueResult::queued,
        "queue accepts entries up to its capacity"
    );
    require(
        queue.try_push({veyra::ChatMessageChangeKind::deleted, 3}) ==
            veyra::ChatMessageChangeQueueResult::full,
        "full queue reports backpressure"
    );

    auto popped = veyra::ChatMessageChange{};
    require(queue.try_pop(popped), "queued change can be popped");
    require(
        popped.kind == veyra::ChatMessageChangeKind::appended && popped.message_id == 1,
        "changes retain FIFO order"
    );
    require(
        queue.try_push({veyra::ChatMessageChangeKind::deleted, 3}) ==
            veyra::ChatMessageChangeQueueResult::queued,
        "queue accepts entries after a slot is released"
    );
    require(queue.try_pop(popped) && popped.message_id == 2, "second change remains FIFO");
    require(queue.try_pop(popped) && popped.message_id == 3, "wrapped slot contains new change");
    popped = {veyra::ChatMessageChangeKind::edited, 77};
    require(!queue.try_pop(popped), "empty queue reports no change");
    require(popped.message_id == 77, "empty pop leaves output unchanged");
}

void check_single_producer_consumer_order() {
    constexpr auto event_count = std::uint64_t{200'000};
    auto storage = std::array<veyra::ChatMessageChange, 64>{};
    auto queue = veyra::ChatMessageChangeQueue(storage);
    auto failed = std::atomic<bool>{};

    auto producer = std::jthread([&] {
        auto id = std::uint64_t{1};
        while (id <= event_count && !failed.load(std::memory_order_relaxed)) {
            const auto kind = static_cast<veyra::ChatMessageChangeKind>((id - 1) % 3);
            const auto result = queue.try_push({kind, id});
            if (result == veyra::ChatMessageChangeQueueResult::full) {
                std::this_thread::yield();
                continue;
            }
            if (result != veyra::ChatMessageChangeQueueResult::queued) {
                failed.store(true, std::memory_order_relaxed);
                return;
            }
            ++id;
        }
    });

    auto batch = std::array<veyra::ChatMessageChange, 17>{};
    auto expected_id = std::uint64_t{1};
    while (expected_id <= event_count && !failed.load(std::memory_order_relaxed)) {
        const auto count = queue.try_pop_batch(batch);
        if (count == 0) {
            std::this_thread::yield();
            continue;
        }

        for (std::size_t index = 0; index < count; ++index) {
            const auto expected_kind =
                static_cast<veyra::ChatMessageChangeKind>((expected_id - 1) % 3);
            if (batch[index].message_id != expected_id || batch[index].kind != expected_kind) {
                failed.store(true, std::memory_order_relaxed);
                break;
            }
            ++expected_id;
        }
    }
    producer.join();
    require(!failed.load(std::memory_order_relaxed), "concurrent queue preserves event order");
}

void check_batch_limit_and_ring_wrap() {
    auto storage = std::array<veyra::ChatMessageChange, 3>{};
    auto queue = veyra::ChatMessageChangeQueue(storage);
    for (auto id = std::uint64_t{1}; id <= 3; ++id) {
        require(
            queue.try_push({veyra::ChatMessageChangeKind::appended, id}) ==
                veyra::ChatMessageChangeQueueResult::queued,
            "non-power-of-two storage accepts entries"
        );
    }

    auto batch = std::array<veyra::ChatMessageChange, 2>{};
    require(queue.try_pop_batch(batch) == 2, "batch pop respects caller buffer size");
    require(batch[0].message_id == 1 && batch[1].message_id == 2, "batch pop preserves FIFO");
    require(
        queue.try_push({veyra::ChatMessageChangeKind::edited, 4}) ==
            veyra::ChatMessageChangeQueueResult::queued,
        "producer reuses slots freed by a batch"
    );

    auto wrapped_batch = std::array<veyra::ChatMessageChange, 3>{};
    require(queue.try_pop_batch(wrapped_batch) == 2, "batch pop drains the available snapshot");
    require(
        wrapped_batch[0].message_id == 3 && wrapped_batch[1].message_id == 4,
        "batch pop crosses the end of a non-power-of-two ring"
    );
    wrapped_batch[0].message_id = 77;
    require(queue.try_pop_batch(wrapped_batch) == 0, "empty batch pop reports no events");
    require(wrapped_batch[0].message_id == 77, "empty batch pop leaves output untouched");
}

}

int main() {
    try {
        check_validation_and_capacity();
        check_batch_limit_and_ring_wrap();
        check_single_producer_consumer_order();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
