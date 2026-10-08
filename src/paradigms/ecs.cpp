#include "veyra/engine.hpp"

#include <algorithm>
#include <cstddef>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>

namespace veyra {

std::string_view EcsEngine::name() const noexcept {
    return "ecs";
}

void EcsEngine::reset(std::size_t count) {
    values_.assign(count, 0);
    painted_.assign(count, 0);
    changed_.assign(count, 1);
}

void EcsEngine::apply(std::span<const Update> changes) {
    for (const auto change : changes) {
        if (change.id >= values_.size()) {
            continue;
        }

        if (values_[change.id] == change.value) {
            continue;
        }

        values_[change.id] = change.value;
        changed_[change.id] = 1;
    }
}

Stats EcsEngine::frame(std::size_t first, std::size_t visible, std::span<Rect> out) {
    Stats stats;

    const auto start = std::min(first, values_.size());
    const auto window = std::min(visible, values_.size() - start);
    const auto count = std::min(window, out.size());

    for (std::size_t offset = 0; offset < count; ++offset) {
        const auto index = start + offset;
        ++stats.visited;

        if (changed_[index] != 0) {
            painted_[index] = values_[index];
            changed_[index] = 0;
            ++stats.invalidated;
        }

        out[offset] = {
            0.0f,
            static_cast<float>(offset) * 20.0f,
            640.0f,
            20.0f,
            painted_[index],
        };
        ++stats.emitted;
    }

    return stats;
}

std::uint64_t EcsEngine::checksum() const {
    std::uint64_t result = 0;

    for (const auto value : values_) {
        result += value;
    }

    return result;
}

#if !defined(VEYRA_ARENA) && !defined(VEYRA_REACTIVE)
namespace {

constexpr std::string_view kEngines[] = {"ecs"};

}

std::span<const std::string_view> available_engines() noexcept {
    return kEngines;
}

std::unique_ptr<Engine> create_engine(std::string_view name) {
    if (name == "ecs") {
        return std::make_unique<EcsEngine>();
    }

    throw std::invalid_argument("Engine not built: " + std::string(name));
}
#endif

}
