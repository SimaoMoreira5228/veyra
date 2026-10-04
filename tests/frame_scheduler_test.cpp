#include "veyra/core/frame_scheduler.hpp"

#include <chrono>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

using namespace std::chrono_literals;

void require(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

void check_idle_and_immediate_requests() {
    veyra::FrameScheduler scheduler;
    const auto now = veyra::FrameScheduler::Deadline{};

    require(!scheduler.immediate_pending(), "new scheduler has no immediate request");
    require(!scheduler.deadline().has_value(), "new scheduler has no deadline");
    require(!scheduler.frame_due(now), "idle scheduler has no frame due");
    require(!scheduler.consume_frame(now), "idle scheduler consumes no frame");

    scheduler.request_immediate();
    scheduler.request_immediate();
    require(scheduler.immediate_pending(), "immediate requests coalesce");
    require(scheduler.frame_due(now), "immediate request is ready at once");
    require(scheduler.consume_frame(now), "immediate request can be consumed");
    require(!scheduler.immediate_pending(), "consuming clears the immediate request");
    require(!scheduler.frame_due(now), "consumed request does not schedule an extra frame");
}

void check_deadlines_and_replacement() {
    veyra::FrameScheduler scheduler;
    const auto now = veyra::FrameScheduler::Deadline{};
    const auto first_deadline = now + 8ms;
    const auto next_deadline = now + 16ms;

    scheduler.set_deadline(first_deadline);
    require(scheduler.deadline() == first_deadline, "deadline is exposed for a wait operation");
    require(!scheduler.frame_due(first_deadline - 1ns), "deadline does not fire early");
    require(!scheduler.consume_frame(first_deadline - 1ns), "early deadline stays pending");
    require(scheduler.deadline() == first_deadline, "early check preserves the deadline");

    scheduler.set_deadline(next_deadline);
    require(scheduler.deadline() == next_deadline, "new deadline replaces the previous one");
    require(!scheduler.frame_due(next_deadline - 1ns), "replacement deadline also waits");
    require(scheduler.consume_frame(next_deadline), "deadline fires at its exact time");
    require(!scheduler.deadline().has_value(), "consuming a due deadline clears it");

    scheduler.set_deadline(first_deadline);
    scheduler.set_deadline(std::nullopt);
    require(!scheduler.deadline().has_value(), "deadline can be cancelled");
    require(!scheduler.frame_due(next_deadline), "cancelled deadline does not request a frame");
}

void check_immediate_frame_preserves_future_deadline() {
    veyra::FrameScheduler scheduler;
    const auto now = veyra::FrameScheduler::Deadline{};
    const auto deadline = now + 12ms;

    scheduler.set_deadline(deadline);
    scheduler.request_immediate();
    require(scheduler.consume_frame(now), "immediate work can run before the deadline");
    require(!scheduler.immediate_pending(), "immediate work is consumed once");
    require(scheduler.deadline() == deadline, "future deadline survives the immediate frame");
    require(!scheduler.frame_due(now), "future deadline still waits after immediate work");
    require(scheduler.consume_frame(deadline), "future work runs at its deadline");

    scheduler.set_deadline(deadline);
    scheduler.request_immediate();
    require(scheduler.consume_frame(deadline), "immediate work consumes a due deadline once");
    require(!scheduler.frame_due(deadline), "coalesced work does not schedule a second frame");
}

}

int main() {
    try {
        check_idle_and_immediate_requests();
        check_deadlines_and_replacement();
        check_immediate_frame_preserves_future_deadline();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
