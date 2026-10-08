#include "veyra/chat/message_change_queue.hpp"

#include <algorithm>

namespace veyra {

ChatMessageChangeQueue::ChatMessageChangeQueue(std::span<ChatMessageChange> storage) noexcept :
    storage_(storage) {
}

bool ChatMessageChangeQueue::valid() const noexcept {
    return !storage_.empty();
}

std::size_t ChatMessageChangeQueue::capacity() const noexcept {
    return storage_.size();
}

ChatMessageChangeQueueResult ChatMessageChangeQueue::try_push(ChatMessageChange change) noexcept {
    if (!valid()) {
        return ChatMessageChangeQueueResult::invalid_workspace;
    }
    if (!valid_change(change)) {
        return ChatMessageChangeQueueResult::invalid_change;
    }

    const auto write_sequence = write_sequence_.load(std::memory_order_relaxed);
    const auto read_sequence = read_sequence_.load(std::memory_order_acquire);
    if (write_sequence - read_sequence >= storage_.size()) {
        return ChatMessageChangeQueueResult::full;
    }

    storage_[write_index_] = change;
    ++write_index_;
    if (write_index_ == storage_.size()) {
        write_index_ = 0;
    }
    write_sequence_.store(write_sequence + 1, std::memory_order_release);
    return ChatMessageChangeQueueResult::queued;
}

bool ChatMessageChangeQueue::try_pop(ChatMessageChange& change) noexcept {
    if (!valid()) {
        return false;
    }

    const auto read_sequence = read_sequence_.load(std::memory_order_relaxed);
    const auto write_sequence = write_sequence_.load(std::memory_order_acquire);
    if (read_sequence == write_sequence) {
        return false;
    }

    change = storage_[read_index_];
    ++read_index_;
    if (read_index_ == storage_.size()) {
        read_index_ = 0;
    }
    read_sequence_.store(read_sequence + 1, std::memory_order_release);
    return true;
}

std::size_t ChatMessageChangeQueue::try_pop_batch(std::span<ChatMessageChange> changes) noexcept {
    if (!valid() || changes.empty()) {
        return 0;
    }

    const auto read_sequence = read_sequence_.load(std::memory_order_relaxed);
    const auto write_sequence = write_sequence_.load(std::memory_order_acquire);
    const auto available = write_sequence - read_sequence;
    const auto count = std::min(changes.size(), available);
    if (count == 0) {
        return 0;
    }

    for (auto index = std::size_t{0}; index < count; ++index) {
        changes[index] = storage_[read_index_];
        ++read_index_;
        if (read_index_ == storage_.size()) {
            read_index_ = 0;
        }
    }
    read_sequence_.store(read_sequence + count, std::memory_order_release);
    return count;
}

bool ChatMessageChangeQueue::valid_change(ChatMessageChange change) noexcept {
    if (change.message_id == 0) {
        return false;
    }

    switch (change.kind) {
    case ChatMessageChangeKind::appended:
    case ChatMessageChangeKind::edited:
    case ChatMessageChangeKind::deleted:
        return true;
    }

    return false;
}

}
