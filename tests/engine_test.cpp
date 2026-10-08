#include "veyra/engine.hpp"

#include <array>
#include <cstdint>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>

namespace {

void require(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

template <typename EngineT> void check_engine(EngineT& engine) {
    engine.reset(4);

    std::array<veyra::Rect, 2> rects{};
    const auto first_frame = engine.frame(1, 2, rects);
    require(first_frame.visited == 2, "initial frame visits visible items");
    require(first_frame.invalidated == 2, "initial frame paints visible items");
    require(first_frame.emitted == 2, "initial frame emits visible rectangles");

    const std::array changes{
        veyra::Update{1, 8},
        veyra::Update{1, 9},
        veyra::Update{99, 7},
    };
    engine.apply(changes);

    const auto changed_frame = engine.frame(1, 2, rects);
    require(changed_frame.visited == 2, "changed frame visits visible items");
    require(changed_frame.invalidated == 1, "only changed visible items are repainted");
    require(rects[0].color == 9, "last update wins");
    require(rects[1].color == 0, "untouched items keep their paint");
    require(engine.checksum() == 9, "checksum reflects valid updates");

    engine.apply(std::array{veyra::Update{1, 9}});

    const auto unchanged_frame = engine.frame(1, 2, rects);
    require(unchanged_frame.invalidated == 0, "unchanged values do not invalidate");

    const auto outside_frame = engine.frame(5, 2, rects);
    require(outside_frame.visited == 0, "frame beyond the item count is empty");
    require(outside_frame.emitted == 0, "frame beyond the item count emits nothing");

    engine.reset(4);

    std::array<veyra::Rect, 1> small{};
    const auto truncated_frame = engine.frame(0, 4, small);
    require(truncated_frame.emitted == 1, "frame stops at the output capacity");
    require(truncated_frame.visited == 1, "frame only visits what it can emit");
    require(small[0].color == 0, "truncated frame still paints");

    const auto empty_frame = engine.frame(0, 4, std::span<veyra::Rect>{});
    require(empty_frame.emitted == 0, "zero-capacity frame emits nothing");
    require(empty_frame.visited == 0, "zero-capacity frame visits nothing");
    require(empty_frame.invalidated == 0, "zero-capacity frame invalidates nothing");

    std::array<veyra::Rect, 4> full{};
    const auto remainder_frame = engine.frame(0, 4, full);
    require(remainder_frame.emitted == 4, "full frame emits every visible item");
    require(remainder_frame.invalidated == 3, "items skipped by truncation stay dirty");
}

template <typename EngineT> void check_concrete() {
    EngineT engine;
    check_engine(engine);
}

}

int main() {
    try {
#ifdef VEYRA_ARENA
        check_concrete<veyra::ArenaEngine>();
#endif
#ifdef VEYRA_REACTIVE
        check_concrete<veyra::ReactiveEngine>();
#endif
#ifdef VEYRA_ECS
        check_concrete<veyra::EcsEngine>();
#endif

        const auto engines = veyra::available_engines();
        require(!engines.empty(), "at least one engine is registered");

        for (const auto name : engines) {
            auto engine = veyra::create_engine(name);
            check_engine(*engine);
        }
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
