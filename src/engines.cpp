#include "veyra/engine.hpp"

#include <algorithm>
#include <stdexcept>
#include <string>

namespace veyra {
namespace {

struct Node {
    std::uint32_t value{};
    std::uint32_t painted{};
    bool dirty{true};
};

class Arena final : public Engine {
    std::vector<Node> nodes_;

  public:
    std::string_view name() const noexcept override {
        return "arena";
    }

    void reset(std::size_t count) override {
        nodes_.assign(count, {});
    }

    void apply(std::span<const Update> updates) override {
        for (const auto update : updates) {
            if (update.id >= nodes_.size()) {
                continue;
            }

            auto& node = nodes_[update.id];
            if (node.value == update.value) {
                continue;
            }

            node.value = update.value;
            node.dirty = true;
        }
    }

    Stats frame(std::size_t first, std::size_t visible, std::vector<Rect>& out) override {
        Stats stats;
        out.clear();

        const auto start = std::min(first, nodes_.size());
        const auto end = start + std::min(visible, nodes_.size() - start);
        for (auto index = start; index < end; ++index) {
            auto& node = nodes_[index];
            ++stats.visited;

            if (node.dirty) {
                node.painted = node.value;
                node.dirty = false;
                ++stats.invalidated;
            }

            out.push_back({
                0.0f,
                static_cast<float>(index - start) * 20.0f,
                640.0f,
                20.0f,
                node.painted,
            });
            ++stats.emitted;
        }

        return stats;
    }

    std::uint64_t checksum() const override {
        std::uint64_t result = 0;
        for (const auto& node : nodes_) {
            result += node.value;
        }
        return result;
    }
};

class Reactive final : public Engine {
    std::vector<std::uint32_t> values_;
    std::vector<std::uint32_t> cache_;
    std::vector<std::uint8_t> pending_;

  public:
    std::string_view name() const noexcept override {
        return "reactive";
    }

    void reset(std::size_t count) override {
        values_.assign(count, 0);
        cache_.assign(count, 0);
        pending_.assign(count, 1);
    }

    void apply(std::span<const Update> updates) override {
        for (const auto update : updates) {
            if (update.id >= values_.size() || values_[update.id] == update.value) {
                continue;
            }

            values_[update.id] = update.value;
            pending_[update.id] = 1;
        }
    }

    Stats frame(std::size_t first, std::size_t visible, std::vector<Rect>& out) override {
        Stats stats;
        out.clear();

        const auto start = std::min(first, values_.size());
        const auto end = start + std::min(visible, values_.size() - start);
        for (auto index = start; index < end; ++index) {
            ++stats.visited;
            if (pending_[index] != 0) {
                cache_[index] = values_[index];
                pending_[index] = 0;
                ++stats.invalidated;
            }

            out.push_back({
                0.0f,
                static_cast<float>(index - start) * 20.0f,
                640.0f,
                20.0f,
                cache_[index],
            });
            ++stats.emitted;
        }

        return stats;
    }

    std::uint64_t checksum() const override {
        std::uint64_t result = 0;
        for (const auto value : values_) {
            result += value;
        }
        return result;
    }
};

// Prototype: columnar component storage and per-entity change flags, not a full archetype ECS.
class Ecs final : public Engine {
    std::vector<std::uint32_t> values_;
    std::vector<std::uint32_t> painted_;
    std::vector<std::uint8_t> changed_;

  public:
    std::string_view name() const noexcept override {
        return "ecs";
    }

    void reset(std::size_t count) override {
        values_.assign(count, 0);
        painted_.assign(count, 0);
        changed_.assign(count, 1);
    }

    void apply(std::span<const Update> updates) override {
        for (const auto update : updates) {
            if (update.id >= values_.size() || values_[update.id] == update.value) {
                continue;
            }

            values_[update.id] = update.value;
            changed_[update.id] = 1;
        }
    }

    Stats frame(std::size_t first, std::size_t visible, std::vector<Rect>& out) override {
        Stats stats;
        out.clear();

        const auto start = std::min(first, values_.size());
        const auto end = start + std::min(visible, values_.size() - start);
        for (auto index = start; index < end; ++index) {
            ++stats.visited;
            if (changed_[index] != 0) {
                painted_[index] = values_[index];
                changed_[index] = 0;
                ++stats.invalidated;
            }

            out.push_back({
                0.0f,
                static_cast<float>(index - start) * 20.0f,
                640.0f,
                20.0f,
                painted_[index],
            });
            ++stats.emitted;
        }

        return stats;
    }

    std::uint64_t checksum() const override {
        std::uint64_t result = 0;
        for (const auto value : values_) {
            result += value;
        }
        return result;
    }
};

}

std::vector<std::string_view> available_engines() {
    std::vector<std::string_view> engines;
#ifdef VEYRA_ARENA
    engines.push_back("arena");
#endif
#ifdef VEYRA_REACTIVE
    engines.push_back("reactive");
#endif
#ifdef VEYRA_ECS
    engines.push_back("ecs");
#endif
    return engines;
}

std::unique_ptr<Engine> create_engine(std::string_view name) {
#ifdef VEYRA_ARENA
    if (name == "arena") {
        return std::make_unique<Arena>();
    }
#endif
#ifdef VEYRA_REACTIVE
    if (name == "reactive") {
        return std::make_unique<Reactive>();
    }
#endif
#ifdef VEYRA_ECS
    if (name == "ecs") {
        return std::make_unique<Ecs>();
    }
#endif
    throw std::invalid_argument("Engine not built: " + std::string(name));
}

}
