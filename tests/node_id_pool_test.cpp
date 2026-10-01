#include "veyra/core/node_id_pool.hpp"

#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

void require(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

void check_stable_ids_and_reuse() {
    veyra::NodeIdPool pool;
    pool.reserve(1);

    const auto stale_id = pool.allocate();
    require(!stale_id.is_null(), "allocated node id is non-null");
    require(pool.contains(stale_id), "new node id is active");

    std::vector<veyra::NodeId> other_ids;
    other_ids.reserve(256);
    for (std::size_t index = 0; index < 256; ++index) {
        other_ids.push_back(pool.allocate());
    }
    require(pool.contains(stale_id), "node id remains stable after pool growth");
    require(pool.size() == 257, "pool counts active ids");

    require(pool.release(stale_id), "active id can be released");
    require(!pool.contains(stale_id), "released id is no longer active");
    require(!pool.release(stale_id), "stale id cannot be released twice");

    const auto reused_id = pool.allocate();
    require(reused_id.index == stale_id.index, "released slot is reused");
    require(reused_id.generation != stale_id.generation, "reused slot advances generation");
    require(!pool.contains(stale_id), "stale id cannot access a reused slot");
    require(pool.contains(reused_id), "reused id is active");

    const veyra::NodeId wrong_generation{
        reused_id.index,
        static_cast<std::uint32_t>(reused_id.generation + 1),
    };
    require(!pool.contains(wrong_generation), "unknown generation is rejected");
    require(!pool.release({veyra::NodeId::invalid_index - 1, 1}), "unknown index is rejected");

    veyra::NodeIdPool moved_pool(std::move(pool));
    require(moved_pool.contains(reused_id), "move preserves active ids");

    veyra::NodeIdPool assigned_pool;
    assigned_pool = std::move(moved_pool);
    require(assigned_pool.contains(reused_id), "move assignment preserves active ids");
}

}

int main() {
    try {
        check_stable_ids_and_reuse();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
