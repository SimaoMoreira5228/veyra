#include "veyra/engine.hpp"

#include <memory>
#include <span>
#include <stdexcept>
#include <string>

namespace veyra {
namespace {

constexpr std::string_view kEngines[] = {"arena", "reactive", "ecs"};

}

std::span<const std::string_view> available_engines() noexcept {
    return kEngines;
}

std::unique_ptr<Engine> create_engine(std::string_view name) {
    if (name == "arena") {
        return std::make_unique<ArenaEngine>();
    }

    if (name == "reactive") {
        return std::make_unique<ReactiveEngine>();
    }

    if (name == "ecs") {
        return std::make_unique<EcsEngine>();
    }

    throw std::invalid_argument("Engine not built: " + std::string(name));
}

}
