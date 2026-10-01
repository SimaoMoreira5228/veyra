#include "veyra/engine.hpp"

#include <array>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void require(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

void check_engine(std::string_view name) {
    auto engine = veyra::create_engine(name);
    engine->reset(4);

    std::vector<veyra::Rect> rects;
    const auto first_frame = engine->frame(1, 2, rects);
    require(first_frame.visited == 2, "initial frame visits visible items");
    require(first_frame.invalidated == 2, "initial frame paints visible items");
    require(first_frame.emitted == 2, "initial frame emits visible rectangles");
    require(rects.size() == 2, "rectangle output matches emitted count");

    const std::array changes{
        veyra::Update{1, 8},
        veyra::Update{1, 9},
        veyra::Update{99, 7},
    };
    engine->apply(changes);
    const auto changed_frame = engine->frame(1, 2, rects);
    require(changed_frame.visited == 2, "changed frame visits visible items");
    require(changed_frame.invalidated == 1, "only changed visible items are repainted");
    require(rects[0].color == 9, "last update wins");
    require(rects[1].color == 0, "out-of-range update is ignored");
    require(engine->checksum() == 9, "checksum reflects valid updates");

    engine->apply(std::array{veyra::Update{1, 9}});
    const auto unchanged_frame = engine->frame(1, 2, rects);
    require(unchanged_frame.invalidated == 0, "unchanged values do not invalidate");

    const auto outside_frame = engine->frame(5, 2, rects);
    require(outside_frame.visited == 0, "frame beyond the item count is empty");
    require(rects.empty(), "empty frame clears previous rectangles");
}

}

int main() {
    try {
        const auto engines = veyra::available_engines();
        require(!engines.empty(), "at least one engine is registered");
        for (const auto name : engines) {
            check_engine(name);
        }
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
