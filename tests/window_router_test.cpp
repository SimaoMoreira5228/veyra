#include "veyra/platform/window_router.hpp"

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
        check_capacity_and_invalid_routes();
        std::cout << "window routing checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
