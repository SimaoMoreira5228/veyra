#include "veyra/engine.hpp"

#include <algorithm>
#include <cstddef>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>

namespace veyra {

std::string_view ArenaEngine::name() const noexcept {
    return "arena";
}

void ArenaEngine::reset(std::size_t count) {
    nodes_.assign(count, {});
}

void ArenaEngine::apply(std::span<const Update> changes) {
    for (const auto change : changes) {
        if (change.id >= nodes_.size()) {
            continue;
        }

        auto& node = nodes_[change.id];

        if (node.value == change.value) {
            continue;
        }

        node.value = change.value;
        node.dirty = true;
    }
}

Stats ArenaEngine::frame(std::size_t first, std::size_t visible, std::span<Rect> out) {
    Stats stats;

    const auto start = std::min(first, nodes_.size());
    const auto window = std::min(visible, nodes_.size() - start);
    const auto count = std::min(window, out.size());

    for (std::size_t offset = 0; offset < count; ++offset) {
        const auto index = start + offset;
        auto& node = nodes_[index];
        ++stats.visited;

        if (node.dirty) {
            node.painted = node.value;
            node.dirty = false;
            ++stats.invalidated;
        }

        out[offset] = {
            0.0f,
            static_cast<float>(offset) * 20.0f,
            640.0f,
            20.0f,
            node.painted,
        };
        ++stats.emitted;
    }

    return stats;
}

std::uint64_t ArenaEngine::checksum() const {
    std::uint64_t result = 0;

    for (const auto& node : nodes_) {
        result += node.value;
    }

    return result;
}

#if !defined(VEYRA_REACTIVE) && !defined(VEYRA_ECS)
namespace {

constexpr std::string_view kEngines[] = {"arena"};

}

std::span<const std::string_view> available_engines() noexcept {
    return kEngines;
}

std::unique_ptr<Engine> create_engine(std::string_view name) {
    if (name == "arena") {
        return std::make_unique<ArenaEngine>();
    }

    throw std::invalid_argument("Engine not built: " + std::string(name));
}
#endif

}
