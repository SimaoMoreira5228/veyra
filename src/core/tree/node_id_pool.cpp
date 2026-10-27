#include "veyra/core/tree/node_id_pool.hpp"

#include <utility>

namespace veyra {

NodeIdPool::NodeIdPool(NodeIdPool&& other) noexcept :
    slots_(std::move(other.slots_)),
    free_head_(std::exchange(other.free_head_, NodeId::invalid_index)),
    live_count_(std::exchange(other.live_count_, 0)) {
    other.slots_.clear();
}

NodeIdPool& NodeIdPool::operator=(NodeIdPool&& other) noexcept {
    if (this == &other) {
        return *this;
    }

    slots_ = std::move(other.slots_);
    free_head_ = std::exchange(other.free_head_, NodeId::invalid_index);
    live_count_ = std::exchange(other.live_count_, 0);
    other.slots_.clear();
    return *this;
}

void NodeIdPool::reserve(std::size_t count) {
    slots_.reserve(count);
}

NodeId NodeIdPool::allocate() {
    if (free_head_ != NodeId::invalid_index) {
        const auto index = free_head_;
        auto& slot = slots_[index];
        free_head_ = slot.next_free;
        slot.next_free = NodeId::invalid_index;
        slot.active = true;
        ++live_count_;
        return {index, slot.generation};
    }

    if (slots_.size() >= NodeId::invalid_index) {
        return {};
    }

    const auto index = static_cast<std::uint32_t>(slots_.size());
    slots_.push_back({});
    slots_.back().active = true;
    ++live_count_;
    return {index, slots_.back().generation};
}

bool NodeIdPool::release(NodeId id) noexcept {
    if (!contains(id)) {
        return false;
    }

    auto& slot = slots_[id.index];
    slot.active = false;
    --live_count_;

    if (slot.generation == UINT32_MAX) {
        return true;
    }

    ++slot.generation;
    slot.next_free = free_head_;
    free_head_ = id.index;
    return true;
}

bool NodeIdPool::contains(NodeId id) const noexcept {
    if (id.is_null() || id.index >= slots_.size()) {
        return false;
    }

    const auto& slot = slots_[id.index];
    return slot.active && slot.generation == id.generation;
}

std::size_t NodeIdPool::size() const noexcept {
    return live_count_;
}

}
