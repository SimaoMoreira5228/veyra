#include "veyra/chat/message/message_change_queue.hpp"

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>

namespace {

using Clock = std::chrono::steady_clock;

std::uint64_t event_count(int argc, char** argv) {
    constexpr auto default_count = std::uint64_t{1'000'000};
    if (argc == 1) {
        return default_count;
    }
    if (argc != 3 || std::string_view(argv[1]) != "--events") {
        throw std::runtime_error("usage: veyra_chat_message_change_queue_bench [--events count]");
    }

    const auto count = std::stoull(argv[2]);
    if (count == 0) {
        throw std::runtime_error("event count must be positive");
    }
    return count;
}

int run(int argc, char** argv) {
    constexpr auto queue_capacity = std::size_t{1024};
    const auto count = event_count(argc, argv);
    auto storage = std::array<veyra::ChatMessageChange, queue_capacity>{};
    auto queue = veyra::ChatMessageChangeQueue(storage);
    auto batch = std::array<veyra::ChatMessageChange, 64>{};
    auto failed = std::atomic<bool>{};
    auto checksum = std::uint64_t{};

    const auto begin = Clock::now();
    auto producer = std::jthread([&] {
        auto id = std::uint64_t{1};
        while (id <= count && !failed.load(std::memory_order_relaxed)) {
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

    auto expected_id = std::uint64_t{1};
    while (expected_id <= count && !failed.load(std::memory_order_relaxed)) {
        const auto batch_count = queue.try_pop_batch(batch);
        if (batch_count == 0) {
            std::this_thread::yield();
            continue;
        }

        for (std::size_t index = 0; index < batch_count; ++index) {
            if (batch[index].message_id != expected_id) {
                failed.store(true, std::memory_order_relaxed);
                break;
            }
            checksum += batch[index].message_id + static_cast<std::uint64_t>(batch[index].kind);
            ++expected_id;
        }
    }
    producer.join();
    const auto elapsed = std::chrono::duration<double>(Clock::now() - begin).count();
    if (failed.load(std::memory_order_relaxed)) {
        throw std::runtime_error("queue benchmark observed invalid event order");
    }

    std::cout
        << "scenario,platform,compiler,build_config,events,capacity,batch_size,elapsed_seconds,"
           "events_per_second,checksum\n"
        << "chat_message_change_spsc_batch," << VEYRA_BUILD_PLATFORM << ',' << VEYRA_BUILD_COMPILER
        << ',' << VEYRA_BUILD_CONFIG << ',' << count << ',' << queue_capacity << ',' << batch.size()
        << ',' << elapsed << ',' << static_cast<double>(count) / elapsed << ',' << checksum << '\n';
    return 0;
}

}

int main(int argc, char** argv) {
    try {
        return run(argc, argv);
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 2;
    }
}
