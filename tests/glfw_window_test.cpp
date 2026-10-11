#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include "veyra/platform/glfw_window.hpp"

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

bool drain_events(veyra::GlfwWindow& window) {
    for (std::size_t count = 0; count < veyra::GlfwWindow::kEventCapacity; ++count) {
        veyra::WindowEvent event{};
        if (!window.poll_event(event)) {
            return true;
        }
        if (event.window != window.window_id()) {
            return false;
        }
    }

    return true;
}

}

int main() {
    if (glfwPlatformSupported(GLFW_PLATFORM_NULL) != GLFW_TRUE) {
        return 77;
    }
    glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_NULL);

    veyra::GlfwWindow main_window(11, "Veyra main", 640, 480, false);
    if (main_window.creation_result() != veyra::WindowResult::ready) {
        std::cerr << "GLFW could not create a window in this environment\n";
        return 77;
    }

    veyra::GlfwWindow inspector(12, "Veyra inspector", 320, 240, false);
    try {
        require(inspector.creation_result() == veyra::WindowResult::ready, "second window creates");
        require(
            main_window.window_id() == 11 && inspector.window_id() == 12,
            "ids remain distinct"
        );
        require(main_window.logical_width() == 640.0F, "main logical width is reported");
        require(inspector.logical_height() == 240.0F, "inspector logical height is reported");
        require(
            main_window.scale_x() > 0.0F && main_window.scale_y() > 0.0F,
            "main content scale is positive"
        );
        require(main_window.surface_generation() > 0, "surface generation starts initialized");

        const auto capabilities = main_window.capabilities();
        require(!capabilities.clipboard, "null platform does not claim clipboard support");
        require(!capabilities.text_input, "null platform does not claim text input");
        require(!capabilities.text_preedit, "GLFW does not claim preedit support");
        require(!capabilities.text_input_area, "GLFW does not claim IME placement support");
        require(
            main_window.start_text_input() == veyra::WindowResult::unsupported,
            "unavailable platform text input is explicit"
        );
        require(
            main_window.set_text_input_area({}) == veyra::WindowResult::unsupported,
            "unsupported IME placement is explicit"
        );
        require(
            main_window.set_clipboard_text("clipboard") == veyra::WindowResult::unsupported,
            "unsupported clipboard is explicit"
        );
        std::size_t clipboard_length = 0;
        require(
            main_window.get_clipboard_text({}, clipboard_length) ==
                veyra::WindowResult::unsupported,
            "unsupported clipboard reads are explicit"
        );
        require(
            main_window.stop_text_input() == veyra::WindowResult::unsupported,
            "unavailable platform text input remains explicit"
        );
        require(drain_events(main_window), "main events carry the main window id");
        require(drain_events(inspector), "inspector events carry the inspector id");
        std::cout << "GLFW window checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
