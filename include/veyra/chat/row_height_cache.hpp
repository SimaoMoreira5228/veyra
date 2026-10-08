#ifndef VEYRA_CHAT_ROW_HEIGHT_CACHE_HPP
#define VEYRA_CHAT_ROW_HEIGHT_CACHE_HPP

#include <cstddef>
#include <cstdint>
#include <span>

namespace veyra {

struct ChatRowHeightKey {
    std::uint64_t message_id{};
    float available_width{};
    std::uint64_t style_generation{};
};

struct ChatRowHeightCacheSlot {
  private:
    friend class ChatRowHeightCache;

    ChatRowHeightKey key;
    float height{};
    std::uint8_t recency{};
    bool occupied{};
};

enum class ChatRowHeightCacheResult : std::uint8_t {
    hit,
    miss,
    inserted,
    updated,
    invalid_workspace,
    invalid_key,
    invalid_height,
};

class ChatRowHeightCache {
  public:
    explicit ChatRowHeightCache(std::span<ChatRowHeightCacheSlot> slots) noexcept;

    bool valid() const noexcept;
    void clear() noexcept;
    ChatRowHeightCacheResult find(ChatRowHeightKey key, float& height) noexcept;
    ChatRowHeightCacheResult insert(ChatRowHeightKey key, float height) noexcept;
    std::size_t invalidate_message(std::uint64_t message_id) noexcept;

  private:
    static constexpr std::size_t ways_per_bucket = 4;

    std::span<ChatRowHeightCacheSlot> slots_;
    std::size_t bucket_count_{};
    bool valid_{};

    static bool valid_key(ChatRowHeightKey key) noexcept;
    std::size_t bucket_start(ChatRowHeightKey key) const noexcept;
    std::size_t find_slot(ChatRowHeightKey key) const noexcept;
    void mark_recent(std::size_t slot_index) noexcept;
    void remove(std::size_t slot_index) noexcept;
};

}

#endif
