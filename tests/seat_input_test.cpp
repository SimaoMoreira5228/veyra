#include "veyra/platform/core/seat.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

void require(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

veyra::NodeId node(std::uint32_t index, std::uint32_t generation = 1) {
    return {.index = index, .generation = generation};
}

void check_two_seats_keep_independent_state() {
    auto storage = std::array<veyra::SeatInputState, veyra::kSeatCapacity>{};
    auto seats = veyra::SeatInputSet(storage);

    auto pointer_seat = veyra::SeatId{};
    auto keyboard_seat = veyra::SeatId{};
    require(seats.create(pointer_seat) == veyra::SeatResult::ok, "the first seat is created");
    require(seats.create(keyboard_seat) == veyra::SeatResult::ok, "the second seat is created");
    require(pointer_seat != keyboard_seat, "seat ids are distinct");
    require(seats.size() == 2, "both seats are live");

    require(
        seats.focus(pointer_seat, veyra::WindowId{10}, node(1)) &&
            seats.focus(keyboard_seat, veyra::WindowId{11}, node(2)),
        "both seats accept focus"
    );
    require(
        seats.set_modifiers(pointer_seat, 0x5) && seats.set_modifiers(keyboard_seat, 0x2),
        "both seats accept modifiers"
    );
    require(
        seats.capture_pointer(pointer_seat, 0, node(3)) == veyra::SeatResult::ok &&
            seats.capture_pointer(keyboard_seat, 0, node(4)) == veyra::SeatResult::ok,
        "the same pointer id can be captured on both seats"
    );
    require(
        seats.set_hover(pointer_seat, node(5)) && seats.set_hover(keyboard_seat, node(6)),
        "both seats track hover"
    );

    const auto* pointer = seats.find(pointer_seat);
    const auto* keyboard = seats.find(keyboard_seat);
    require(pointer != nullptr && keyboard != nullptr, "both seats resolve");
    require(pointer->focused_window() == veyra::WindowId{10}, "seat one keeps its window");
    require(keyboard->focused_window() == veyra::WindowId{11}, "seat two keeps its window");
    require(pointer->focused_node() == node(1), "seat one keeps its node");
    require(keyboard->focused_node() == node(2), "seat two keeps its node");
    require(pointer->modifiers() == 0x5 && keyboard->modifiers() == 0x2, "modifiers stay separate");
    require(pointer->capture_target_for(0) == node(3), "seat one keeps its capture");
    require(keyboard->capture_target_for(0) == node(4), "seat two keeps its capture");
    require(pointer->hovered_node() == node(5), "seat one keeps its hover");
    require(keyboard->hovered_node() == node(6), "seat two keeps its hover");

    require(seats.release_pointer(pointer_seat, 0), "a seat releases its own capture");
    require(!pointer->pointer_captured(0), "the released seat drops the capture");
    require(keyboard->capture_target_for(0) == node(4), "the other seat keeps its capture");

    require(seats.clear_pointer_state(pointer_seat), "a seat clears transient pointer state");
    require(
        !pointer->pointer_captured(0) && pointer->hovered_node().is_null(),
        "seat one is clear"
    );
    require(keyboard->capture_target_for(0) == node(4), "seat two keeps pointer state");
    require(keyboard->hovered_node() == node(6), "seat two keeps hover");
}

void check_focus_change_drops_pointer_state() {
    auto storage = std::array<veyra::SeatInputState, veyra::kSeatCapacity>{};
    auto seats = veyra::SeatInputSet(storage);
    auto seat = veyra::SeatId{};
    seats.create(seat);

    seats.focus(seat, veyra::WindowId{20}, node(1));
    seats.capture_pointer(seat, 2, node(2));
    seats.set_hover(seat, node(3));

    require(seats.focus(seat, veyra::WindowId{20}, node(4)), "focus moves inside a window");
    const auto* state = seats.find(seat);
    require(state->capture_target_for(2) == node(2), "same-window focus keeps captures");
    require(state->hovered_node() == node(3), "same-window focus keeps hover");

    require(seats.focus(seat, veyra::WindowId{21}, node(5)), "focus moves to another window");
    require(state->focused_node() == node(5), "the new focus node is stored");
    require(state->capture_count() == 0, "a window change drops captures");
    require(state->hovered_node().is_null(), "a window change drops hover");
}

void check_capture_updates_capacity_and_release() {
    auto storage = std::array<veyra::SeatInputState, veyra::kSeatCapacity>{};
    auto seats = veyra::SeatInputSet(storage);
    auto seat = veyra::SeatId{};
    seats.create(seat);

    for (std::uint32_t pointer = 0; pointer < veyra::kSeatPointerCapacity; ++pointer) {
        require(
            seats.capture_pointer(seat, pointer, node(pointer + 1)) == veyra::SeatResult::ok,
            "each pointer captures"
        );
    }
    require(
        seats.capture_pointer(seat, 9, node(9)) == veyra::SeatResult::capacity,
        "a full capture table rejects another pointer"
    );

    require(
        seats.capture_pointer(seat, 1, node(7)) == veyra::SeatResult::ok,
        "capturing a known pointer updates it"
    );
    const auto* state = seats.find(seat);
    require(state->capture_count() == veyra::kSeatPointerCapacity, "the update does not grow");
    require(state->capture_target_for(1) == node(7), "the update replaces the target");
    require(
        seats.capture_pointer(seat, 0, {}) == veyra::SeatResult::invalid_target,
        "a null target is rejected"
    );

    require(seats.release_pointer(seat, 1), "a captured pointer releases");
    require(state->capture_count() == 3, "release compacts the table");
    require(!seats.release_pointer(seat, 1), "releasing an unknown pointer is ignored");
    require(
        seats.capture_pointer(seat, 10, node(10)) == veyra::SeatResult::ok,
        "a freed slot accepts a new pointer"
    );
}

void check_seat_lifecycle_and_invalid_ids() {
    auto storage = std::array<veyra::SeatInputState, 2>{};
    auto seats = veyra::SeatInputSet(storage);

    auto first = veyra::SeatId{};
    auto second = veyra::SeatId{};
    auto overflow = veyra::SeatId{};
    require(seats.create(first) == veyra::SeatResult::ok, "the first slot allocates");
    require(seats.create(second) == veyra::SeatResult::ok, "the second slot allocates");
    require(
        seats.create(overflow) == veyra::SeatResult::capacity,
        "a full set rejects another seat"
    );

    seats.focus(first, veyra::WindowId{30}, node(1));
    seats.set_modifiers(first, 0x1);
    seats.capture_pointer(first, 1, node(2));
    seats.set_hover(first, node(3));

    require(seats.release(first), "a seat releases");
    require(seats.find(first) == nullptr, "a released seat no longer resolves");
    require(seats.size() == 1, "the live count drops");

    auto reused = veyra::SeatId{};
    require(seats.create(reused) == veyra::SeatResult::ok, "the freed slot is reusable");
    require(reused != first && reused != second, "reuse issues a fresh id");
    const auto* state = seats.find(reused);
    require(
        state->focused_window() == veyra::kInvalidWindowId && state->modifiers() == 0 &&
            state->capture_count() == 0 && state->hovered_node().is_null(),
        "a reused seat starts clean"
    );

    require(!seats.release(veyra::kInvalidSeatId), "the invalid id does not release");
    require(!seats.release(999), "an unknown id does not release");
    require(!seats.focus(999, veyra::WindowId{1}, node(1)), "an unknown seat rejects focus");
    require(!seats.set_modifiers(999, 1), "an unknown seat rejects modifiers");
    require(
        seats.capture_pointer(999, 0, node(1)) == veyra::SeatResult::invalid_seat,
        "an unknown seat rejects capture"
    );
    require(!seats.set_hover(999, node(1)), "an unknown seat rejects hover");
    require(!seats.clear_pointer_state(999), "an unknown seat rejects pointer clearing");
}

}

int main() {
    try {
        check_two_seats_keep_independent_state();
        check_focus_change_drops_pointer_state();
        check_capture_updates_capacity_and_release();
        check_seat_lifecycle_and_invalid_ids();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';

        return 1;
    }

    std::cout << "seat input checks passed\n";

    return 0;
}
