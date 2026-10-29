#include "veyra/platform/core/window_router.hpp"

#include <array>
#include <cmath>
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

veyra::WindowEvent make_scale(veyra::WindowId id, float x, float y) {
    veyra::WindowEvent event{};
    event.kind = veyra::WindowEventKind::scale_change;
    event.window = id;
    event.data.scale = {.x = x, .y = y};
    return event;
}

veyra::WindowEvent make_focus(veyra::WindowId id, veyra::WindowEventKind kind) {
    veyra::WindowEvent event{};
    event.kind = kind;
    event.window = id;
    return event;
}

veyra::WindowEvent make_pointer(veyra::WindowEventKind kind, std::uint32_t pointer) {
    veyra::WindowEvent event{};
    event.kind = kind;
    event.window = 1;
    event.data.pointer.pointer = pointer;
    return event;
}

struct RouteLog {
    int count{};
    veyra::WindowRouteResult result{veyra::WindowRouteResult::routed};
    veyra::WindowEventKind kind{veyra::WindowEventKind::close_request};
    veyra::WindowId window{};
};

void observe_route(
    void* user,
    const veyra::WindowEvent& event,
    veyra::WindowRouteResult result
) noexcept {
    auto* log = static_cast<RouteLog*>(user);
    ++log->count;
    log->result = result;
    log->kind = event.kind;
    log->window = event.window;
}

void check_route_observer() {
    std::array<veyra::WindowRouteState, 1> storage{};
    veyra::WindowRouter router(storage);
    require(router.attach(1, 100.0F, 80.0F, 1.0F, 1.0F) != nullptr, "observer window attaches");

    auto log = RouteLog{};
    router.set_observer(&observe_route, &log);
    require(
        router.route(make_focus(1, veyra::WindowEventKind::focus_gained)) ==
            veyra::WindowRouteResult::routed,
        "observed focus routes"
    );
    require(log.count == 1, "observer sees a routed event");
    require(log.result == veyra::WindowRouteResult::routed, "observer keeps the result");
    require(log.kind == veyra::WindowEventKind::focus_gained, "observer keeps the event kind");
    require(log.window == 1, "observer keeps the window id");

    veyra::WindowEvent missing{};
    require(
        router.route(missing) == veyra::WindowRouteResult::invalid_window,
        "invalid event still returns"
    );
    require(log.count == 2, "observer sees rejected events");
    require(log.result == veyra::WindowRouteResult::invalid_window, "observer keeps the rejection");

    router.set_observer(nullptr, nullptr);
    router.route(missing);
    require(log.count == 2, "clearing the observer stops reporting");
}

void check_window_state_is_isolated() {
    std::array<veyra::WindowRouteState, 2> storage{};
    veyra::WindowRouter router(storage);

    const auto* main = router.attach(1, 800.0F, 600.0F, 1.0F, 1.0F);
    const auto* inspector = router.attach(2, 640.0F, 480.0F, 1.5F, 1.5F);
    require(main != nullptr && inspector != nullptr, "both windows attach");
    require(router.size() == 2, "router tracks both attached windows");

    const veyra::NodeId main_focus{.index = 3, .generation = 4};
    const veyra::NodeId inspector_focus{.index = 8, .generation = 2};
    require(router.set_focused_node(1, main_focus), "main focus is stored");
    require(router.set_focused_node(2, inspector_focus), "inspector focus is stored");
    require(router.mark_presented(1), "main window can finish its frame");
    require(router.mark_presented(2), "inspector can finish its frame independently");

    require(
        router.route(make_focus(2, veyra::WindowEventKind::focus_gained)) ==
            veyra::WindowRouteResult::routed,
        "focus event routes to its window"
    );
    require(
        router.route(make_scale(2, 2.0F, 2.0F)) == veyra::WindowRouteResult::routed,
        "scale event routes to its window"
    );

    main = router.find(1);
    inspector = router.find(2);
    require(main != nullptr && inspector != nullptr, "both windows remain registered");
    require(!main->focused() && !main->damaged(), "other window state stays untouched");
    require(main->focused_node() == main_focus, "main focus target stays independent");
    require(
        inspector->focused() && inspector->damaged(),
        "focus and scale schedule inspector repaint"
    );
    require(inspector->focused_node() == inspector_focus, "inspector focus target is retained");
    require(inspector->scale_generation() == 2, "scale change advances only its cache generation");
    require(
        inspector->scale_x() == 2.0F && inspector->logical_width() == 640.0F,
        "scale change preserves logical dimensions"
    );

    require(router.mark_presented(2), "inspector repaint clears its damage flag");
    require(
        router.route(make_focus(2, veyra::WindowEventKind::focus_lost)) ==
            veyra::WindowRouteResult::routed,
        "focus loss routes to its window"
    );
    require(!inspector->focused() && inspector->damaged(), "focus loss schedules its own repaint");
    require(main->focused_node() == main_focus, "focus loss does not clear another window target");

    require(router.mark_presented(1), "main window clears its repaint state");
    require(router.set_focused_node(1, {.index = 4, .generation = 4}), "main focus target changes");
    require(router.find(1)->damaged(), "focus target change schedules a repaint");
}

void check_pointer_capture_and_hover() {
    std::array<veyra::WindowRouteState, 1> storage{};
    veyra::WindowRouter router(storage);
    require(router.attach(1, 100.0F, 80.0F, 1.0F, 1.0F) != nullptr, "pointer window attaches");

    const auto target = veyra::NodeId{.index = 4, .generation = 2};
    const auto hover = veyra::NodeId{.index = 5, .generation = 2};
    require(router.capture_pointer(1, 7, target), "pointer capture is accepted");
    require(router.find(1)->capture_count() == 1, "one pointer is captured");
    require(router.find(1)->captured_pointer(0) == 7, "captured pointer is stored");
    require(router.find(1)->capture_target(0) == target, "capture target is stored");
    require(router.find(1)->pointer_captured(7), "captured pointer is queryable");
    require(router.find(1)->capture_target_for(7) == target, "capture target is queryable");
    require(router.set_hover(1, hover), "hover target is accepted");
    require(router.find(1)->hovered_node() == hover, "hover target is stored");

    require(
        router.route(make_pointer(veyra::WindowEventKind::pointer_up, 7)) ==
            veyra::WindowRouteResult::routed,
        "pointer release routes"
    );
    require(router.find(1)->capture_count() == 0, "pointer release drops capture");
    require(!router.find(1)->pointer_captured(7), "released pointer is no longer captured");
    require(router.find(1)->hovered_node() == hover, "pointer release keeps hover");

    require(router.mark_presented(1), "capture window clears damage");
    require(router.set_hover(1, hover), "hover is set again");
    require(router.capture_pointer(1, 7, target), "capture is set again");
    require(
        router.route(make_focus(1, veyra::WindowEventKind::focus_lost)) ==
            veyra::WindowRouteResult::routed,
        "focus loss routes"
    );
    require(router.find(1)->capture_count() == 0, "focus loss drops capture");
    require(router.find(1)->hovered_node().is_null(), "focus loss drops hover");

    require(router.set_hover(1, hover), "hover is set for leave");
    require(router.capture_pointer(1, 7, target), "capture is set for leave");
    require(
        router.route(make_focus(1, veyra::WindowEventKind::pointer_leave)) ==
            veyra::WindowRouteResult::routed,
        "pointer leave routes"
    );
    require(router.find(1)->hovered_node().is_null(), "pointer leave drops hover");
    require(router.find(1)->pointer_captured(7), "pointer leave keeps capture");

    require(
        router.route(make_pointer(veyra::WindowEventKind::pointer_cancel, 7)) ==
            veyra::WindowRouteResult::routed,
        "pointer cancel routes"
    );
    require(router.find(1)->capture_count() == 0, "pointer cancel drops capture");
    require(router.find(1)->hovered_node().is_null(), "pointer cancel drops hover");

    require(!router.capture_pointer(1, 0, target), "zero pointer ids are rejected");
    require(!router.capture_pointer(2, 7, target), "unknown windows reject capture");
    require(router.clear_hover(1), "clear hover accepts a known window");
}

void check_multiple_pointer_captures() {
    std::array<veyra::WindowRouteState, 1> storage{};
    veyra::WindowRouter router(storage);
    require(
        router.attach(1, 100.0F, 80.0F, 1.0F, 1.0F) != nullptr,
        "multi capture window attaches"
    );

    const auto first_target = veyra::NodeId{.index = 1, .generation = 1};
    const auto second_target = veyra::NodeId{.index = 2, .generation = 1};
    const auto updated_target = veyra::NodeId{.index = 3, .generation = 1};
    require(router.capture_pointer(1, 7, first_target), "first pointer captures");
    require(router.capture_pointer(1, 8, second_target), "second pointer captures");
    require(router.find(1)->capture_count() == 2, "both pointers stay captured");
    require(router.find(1)->pointer_captured(7), "first pointer remains captured");
    require(router.find(1)->pointer_captured(8), "second pointer remains captured");

    require(router.capture_pointer(1, 7, updated_target), "a capture target can change");
    require(router.find(1)->capture_count() == 2, "changing a target keeps the count");
    require(router.find(1)->capture_target_for(7) == updated_target, "changed target is stored");

    require(router.release_pointer(1, 7), "one pointer releases");
    require(router.find(1)->capture_count() == 1, "the other pointer remains");
    require(!router.find(1)->pointer_captured(7), "released pointer is gone");
    require(router.find(1)->capture_target_for(8) == second_target, "other target survives");

    require(router.capture_pointer(1, 9, first_target), "third pointer captures");
    require(router.capture_pointer(1, 10, first_target), "fourth pointer captures");
    require(router.capture_pointer(1, 11, first_target), "capture table fills");
    require(router.find(1)->capture_count() == 4, "capture table holds four pointers");
    require(!router.capture_pointer(1, 12, first_target), "a full table rejects capture");

    require(
        router.route(make_pointer(veyra::WindowEventKind::pointer_up, 8)) ==
            veyra::WindowRouteResult::routed,
        "the second pointer releases through routing"
    );
    require(!router.find(1)->pointer_captured(8), "routed release drops only its pointer");
    require(router.find(1)->pointer_captured(9), "routed release keeps other pointers");
    require(router.find(1)->pointer_captured(10), "routed release keeps later pointers");

    require(
        router.route(make_focus(1, veyra::WindowEventKind::focus_lost)) ==
            veyra::WindowRouteResult::routed,
        "focus loss routes after multiple captures"
    );
    require(router.find(1)->capture_count() == 0, "focus loss drops every capture");
}

void check_capacity_and_invalid_routes() {
    std::array<veyra::WindowRouteState, 1> storage{};
    veyra::WindowRouter router(storage);

    require(router.attach(0, 10.0F, 10.0F, 1.0F, 1.0F) == nullptr, "zero id is reserved");
    require(router.attach(1, 0.0F, 10.0F, 1.0F, 1.0F) == nullptr, "empty geometry is rejected");
    require(router.attach(1, 10.0F, 10.0F, 1.0F, 1.0F) != nullptr, "valid window attaches");
    require(router.attach(1, 10.0F, 10.0F, 1.0F, 1.0F) == nullptr, "duplicate id is rejected");
    require(
        router.attach(2, 10.0F, 10.0F, 1.0F, 1.0F) == nullptr,
        "full storage rejects another window"
    );

    veyra::WindowEvent missing{};
    require(
        router.route(missing) == veyra::WindowRouteResult::invalid_window,
        "zero event id is rejected"
    );
    missing.window = 2;
    require(
        router.route(missing) == veyra::WindowRouteResult::unknown_window,
        "unregistered event id is rejected"
    );

    auto invalid_scale = make_scale(1, std::nanf(""), 1.0F);
    require(
        router.route(invalid_scale) == veyra::WindowRouteResult::invalid_geometry,
        "invalid scale is rejected"
    );
    require(
        router.find(1)->scale_generation() == 1,
        "invalid scale leaves cache generation unchanged"
    );

    require(router.detach(1), "registered window detaches");
    require(router.size() == 0 && router.find(1) == nullptr, "detached window leaves no route");
    require(!router.detach(1), "unknown window cannot detach twice");
}

}

int main() {
    try {
        check_window_state_is_isolated();
        check_route_observer();
        check_pointer_capture_and_hover();
        check_multiple_pointer_captures();
        check_capacity_and_invalid_routes();
        std::cout << "window routing checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
