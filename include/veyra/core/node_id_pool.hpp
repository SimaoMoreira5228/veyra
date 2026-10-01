#ifndef VEYRA_CORE_NODE_ID_POOL_HPP
#define VEYRA_CORE_NODE_ID_POOL_HPP

#include <cstddef>
#include <cstdint>
#include <vector>

namespace veyra {

struct NodeId {
    static constexpr std::uint32_t invalid_index = UINT32_MAX;

    std::uint32_t index{invalid_index};
    std::uint32_t generation{};

    constexpr bool is_null() const noexcept {
        return index == invalid_index;
    }

    friend constexpr bool operator==(NodeId, NodeId) = default;
};

class NodeIdPool {
    struct Slot {
        std::uint32_t generation{1};
        std::uint32_t next_free{NodeId::invalid_index};
        bool active{};
    };

    std::vector<Slot> slots_;
    std::uint32_t free_head_{NodeId::invalid_index};
    std::size_t live_count_{};

  public:
    NodeIdPool() = default;
    NodeIdPool(const NodeIdPool&) = delete;
    NodeIdPool& operator=(const NodeIdPool&) = delete;
    NodeIdPool(NodeIdPool&& other) noexcept;
    NodeIdPool& operator=(NodeIdPool&& other) noexcept;

    void reserve(std::size_t count);
    NodeId allocate();
    bool release(NodeId id) noexcept;
    bool contains(NodeId id) const noexcept;
    std::size_t size() const noexcept;
};

}

#endif
