#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include "veyra/platform/glfw/glfw_window.hpp"

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

void check_no_gamepads(veyra::GlfwWindow& window) {
    require(window.gamepad_count() == 0, "the null platform reports zero gamepads");

    veyra::WindowEvent event{};
    while (window.poll_event(event)) {
        require(
            event.kind != veyra::WindowEventKind::gamepad_axis &&
                event.kind != veyra::WindowEventKind::gamepad_button &&
                event.kind != veyra::WindowEventKind::gamepad_added &&
                event.kind != veyra::WindowEventKind::gamepad_removed,
            "no gamepad events surface without a device"
        );
    }
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

        require(main_window.capabilities().max_texture_width == 0, "GLFW proves no max width");
        require(main_window.capabilities().max_texture_height == 0, "GLFW proves no max height");
        require(main_window.capabilities().texture_formats == 0, "GLFW proves no pixel format");
        require(
            main_window.capabilities().clip == veyra::WindowClipSupport::none,
            "GLFW clips nothing without a renderer"
        );
        require(main_window.capabilities().present_modes == 0, "GLFW proves no present mode");
        require(!main_window.capabilities().present_timestamp, "GLFW stamps no present time");
        require(!main_window.capabilities().external_image_import, "GLFW imports no images");
        require(!main_window.capabilities().external_image_export, "GLFW exports no handles");
        require(main_window.capabilities().color_spaces == 0, "GLFW manages no color space");
        require(!main_window.capabilities().srgb_transfer_approximate, "GLFW converts no transfer");
        require(!main_window.capabilities().device_loss_recovery, "GLFW recovers no lost device");
        require(
            !main_window.capabilities().external_render_target,
            "GLFW targets no outside surface"
        );

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

        require(
            main_window.window_state() == veyra::WindowState::normal,
            "GLFW windows start normal"
        );
        const auto check_state = [&](veyra::WindowState target, std::string_view message) {
            require(main_window.set_state(target) == veyra::WindowResult::ready, message);
            require(main_window.window_state() == target, "the state query follows the request");

            auto event = veyra::WindowEvent{};
            auto found = false;
            for (auto count = std::size_t{}; count < veyra::GlfwWindow::kEventCapacity; ++count) {
                if (!main_window.poll_event(event)) {
                    break;
                }
                if (event.kind == veyra::WindowEventKind::state_change &&
                    event.data.state == target) {
                    found = true;
                    break;
                }
            }
            require(found, "a state change event reports the new state");
        };

        check_state(veyra::WindowState::minimized, "GLFW minimizes");
        check_state(veyra::WindowState::normal, "GLFW restores from minimize");
        check_state(veyra::WindowState::maximized, "GLFW maximizes");
        check_state(veyra::WindowState::normal, "GLFW restores from maximize");
        check_state(veyra::WindowState::fullscreen, "GLFW enters fullscreen");
        check_state(veyra::WindowState::normal, "GLFW leaves fullscreen");
        require(
            main_window.logical_width() == 640.0F && main_window.logical_height() == 480.0F,
            "leaving fullscreen restores the windowed size"
        );
        require(
            main_window.set_state(static_cast<veyra::WindowState>(99)) ==
                veyra::WindowResult::invalid_input,
            "GLFW rejects unknown states"
        );

        require(drain_events(main_window), "main events carry the main window id");
        require(drain_events(inspector), "inspector events carry the inspector id");
        check_no_gamepads(main_window);
        std::cout << "GLFW window checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
