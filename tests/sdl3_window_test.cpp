#include "veyra/platform/sdl3_window.hpp"

#include <SDL3/SDL.h>

#include <array>
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

void check_window_contract() {
    veyra::Sdl3Window first("Veyra SDL test one", 160, 120);
    veyra::Sdl3Window second("Veyra SDL test two", 120, 90);
    require(first.creation_result() == veyra::WindowResult::ready, "SDL creates the first window");
    require(
        second.creation_result() == veyra::WindowResult::ready,
        "SDL creates the second window"
    );
    require(first.window_id() != second.window_id(), "SDL assigns distinct window ids");
    require(first.capabilities().text_preedit, "SDL reports preedit support");
    require(
        first.logical_width() > 0.0F && first.logical_height() > 0.0F,
        "SDL reports logical size"
    );
    require(first.surface_generation() > 0, "SDL reports a surface generation");

    std::array<veyra::SoftwarePixel, 19200> pixels{};
    veyra::SoftwareSurface surface{};
    require(
        first.acquire_surface(surface, pixels) == veyra::WindowResult::ready,
        "SDL acquires caller-owned pixels"
    );
    require(surface.pixels.data() == pixels.data(), "surface uses the caller buffer");
    require(
        first.acquire_surface(surface, pixels) == veyra::WindowResult::invalid_state,
        "SDL rejects a second acquire before present"
    );
    require(first.present() == veyra::WindowResult::ready, "SDL presents the acquired surface");
    require(
        first.present() == veyra::WindowResult::invalid_state,
        "SDL requires acquire before present"
    );

    std::array<veyra::SoftwarePixel, 4> short_pixels{};
    require(
        first.acquire_surface(surface, short_pixels) == veyra::WindowResult::insufficient_capacity,
        "SDL rejects a short pixel buffer"
    );
    require(
        first.set_text_input_area({.x = 8, .y = 12, .width = 80, .height = 24, .cursor = 16}) ==
            veyra::WindowResult::ready,
        "SDL accepts a valid text area"
    );
    require(first.start_text_input() == veyra::WindowResult::ready, "SDL starts text input");
    require(first.stop_text_input() == veyra::WindowResult::ready, "SDL stops text input");
    require(first.clear_text_input_area() == veyra::WindowResult::ready, "SDL clears text area");

    veyra::WindowEvent received{};
    while (first.poll_event(received)) {
    }
    while (second.poll_event(received)) {
    }

    SDL_Event event{};
    event.type = SDL_EVENT_WINDOW_RESIZED;
    event.window.windowID = static_cast<Uint32>(second.window_id());
    event.window.data1 = 640;
    event.window.data2 = 480;
    require(SDL_PushEvent(&event), "test queues an event for the second window");
    event = {};
    event.type = SDL_EVENT_WINDOW_CLOSE_REQUESTED;
    event.window.windowID = static_cast<Uint32>(first.window_id());
    require(SDL_PushEvent(&event), "test queues an event for the first window");

    require(first.poll_event(received), "first poll finds its event in the process queue");
    require(
        received.kind == veyra::WindowEventKind::close_request &&
            received.window == first.window_id(),
        "first poll returns only the first window's close request"
    );
    require(second.poll_event(received), "second window retains its queued event");
    require(
        received.kind == veyra::WindowEventKind::resize && received.window == second.window_id() &&
            received.data.resize.width == 640.0F && received.data.resize.height == 480.0F,
        "second poll returns its resize payload"
    );

    event = {};
    event.type = SDL_EVENT_QUIT;
    require(SDL_PushEvent(&event), "test queues a process-wide quit");
    require(first.poll_event(received), "first window receives process quit");
    require(
        received.kind == veyra::WindowEventKind::close_request,
        "first quit becomes close request"
    );
    require(second.poll_event(received), "second window receives process quit");
    require(
        received.kind == veyra::WindowEventKind::close_request,
        "second quit becomes close request"
    );

    for (std::size_t index = 0; index < veyra::Sdl3Window::kEventCapacity + 3; ++index) {
        event = {};
        event.type = SDL_EVENT_WINDOW_CLOSE_REQUESTED;
        event.window.windowID = static_cast<Uint32>(first.window_id());
        require(SDL_PushEvent(&event), "test queues events to fill one window's ring");
    }
    require(!second.poll_event(received), "poll pumps events for every window");
    require(
        first.dropped_events() == 3,
        "a full per-window ring drops excess events and reports the count"
    );
}

void check_clipboard() {
    veyra::Sdl3Window window("Veyra clipboard test", 80, 60);
    require(window.creation_result() == veyra::WindowResult::ready, "SDL creates clipboard window");
    require(
        window.set_clipboard_text("clipboard text") == veyra::WindowResult::ready,
        "clipboard writes"
    );

    std::array<char, 32> output{};
    std::size_t length = 0;
    require(
        window.get_clipboard_text(output, length) == veyra::WindowResult::ready,
        "clipboard copies into caller storage"
    );
    require(std::string_view(output.data(), length) == "clipboard text", "clipboard round-trips");

    std::array<char, 4> short_output{'x', 'x', 'x', 'x'};
    require(
        window.get_clipboard_text(short_output, length) ==
            veyra::WindowResult::insufficient_capacity,
        "clipboard reports short output"
    );
    require(length == 14, "clipboard reports required length");
    require(short_output == std::array<char, 4>{'x', 'x', 'x', 'x'}, "short output is untouched");
}

}

int main() {
    try {
        check_window_contract();
        check_clipboard();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
