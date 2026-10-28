#ifndef VEYRA_CHAT_MESSAGE_CHANGE_QUEUE_HPP
#define VEYRA_CHAT_MESSAGE_CHANGE_QUEUE_HPP

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <span>

namespace veyra {

enum class ChatMessageChangeKind : std::uint8_t {
    appended,
    edited,
    deleted,
};

struct ChatMessageChange {
    ChatMessageChangeKind kind{};
    std::uint64_t message_id{};
};

enum class ChatMessageChangeQueueResult : std::uint8_t {
    queued,
    full,
    invalid_workspace,
    invalid_change,
};

class ChatMessageChangeQueue {
  public:
    explicit ChatMessageChangeQueue(std::span<ChatMessageChange> storage) noexcept;

    ChatMessageChangeQueue(const ChatMessageChangeQueue&) = delete;
    ChatMessageChangeQueue& operator=(const ChatMessageChangeQueue&) = delete;

    bool valid() const noexcept;
    std::size_t capacity() const noexcept;
    ChatMessageChangeQueueResult try_push(ChatMessageChange change) noexcept;
    bool try_pop(ChatMessageChange& change) noexcept;
    std::size_t try_pop_batch(std::span<ChatMessageChange> changes) noexcept;

  private:
    std::span<ChatMessageChange> storage_;
    std::atomic<std::size_t> write_sequence_{};
    std::atomic<std::size_t> read_sequence_{};
    std::size_t write_index_{};
    std::size_t read_index_{};

    static bool valid_change(ChatMessageChange change) noexcept;
};

}

namespace veyra::chat {
using ::veyra::ChatMessageChange;
using ::veyra::ChatMessageChangeKind;
using ::veyra::ChatMessageChangeQueue;
using ::veyra::ChatMessageChangeQueueResult;
}

namespace veyra::chat::message {
using ::veyra::ChatMessageChange;
using ::veyra::ChatMessageChangeKind;
using ::veyra::ChatMessageChangeQueue;
using ::veyra::ChatMessageChangeQueueResult;
}

#endif
