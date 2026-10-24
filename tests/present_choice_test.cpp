#include "veyra/platform/null_window.hpp"
#include "veyra/render/present_choice.hpp"

#include <cstdint>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

void require(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

void check_select_starts_on_cpu() {
    auto choice = veyra::PresentChoice{};
    require(choice.path == veyra::PresentPath::software, "a fresh choice stays on the cpu");
    require(veyra::present_choice_generation(choice) == 0, "a fresh choice has no generation");
    require(
        static_cast<int>(veyra::PresentPath::software) == 0 &&
            static_cast<int>(veyra::PresentPath::gpu) == 1,
        "the gpu enumerator covers every gpu route behind one value"
    );

    veyra::select_present_path(choice, veyra::PresentPath::gpu);
    require(choice.path == veyra::PresentPath::gpu, "explicit select arms the gpu path");
    require(veyra::present_choice_generation(choice) == 1, "arming the gpu bumps generation");

    veyra::select_present_path(choice, veyra::PresentPath::gpu);
    require(veyra::present_choice_generation(choice) == 1, "re-selecting changes nothing");
}

void check_failure_falls_back_without_retry() {
    auto choice = veyra::PresentChoice{};
    veyra::select_present_path(choice, veyra::PresentPath::gpu);

    const auto after_ok = veyra::notify_presented(choice, true);
    require(after_ok == veyra::PresentPath::gpu, "a presented gpu frame stays on the gpu");
    require(veyra::present_choice_generation(choice) == 1, "a good frame moves no generation");

    const auto after_failure = veyra::notify_presented(choice, false);
    require(after_failure == veyra::PresentPath::software, "a failed gpu frame falls back");
    require(veyra::present_choice_generation(choice) == 2, "fallback bumps the generation");

    const auto still_down = veyra::notify_presented(choice, false);
    require(still_down == veyra::PresentPath::software, "fallback stays down on its own");
    require(veyra::present_choice_generation(choice) == 2, "staying down moves no generation");

    veyra::select_present_path(choice, veyra::PresentPath::gpu);
    require(choice.path == veyra::PresentPath::gpu, "only an explicit select retries the gpu");
    require(veyra::present_choice_generation(choice) == 3, "explicit retry bumps the generation");
}

void check_software_frame_survives_fallback() {
    auto choice = veyra::PresentChoice{};
    veyra::select_present_path(choice, veyra::PresentPath::gpu);
    veyra::notify_presented(choice, false);
    require(choice.path == veyra::PresentPath::software, "the choice is back on software");

    veyra::NullWindow window("fallback view", 64, 48);
    require(window.creation_result() == veyra::WindowResult::ready, "fallback window creates");

    std::vector<veyra::SoftwarePixel> backing(64 * 48);
    auto surface = veyra::SoftwareSurface{};
    require(
        window.acquire_surface(surface, std::span<veyra::SoftwarePixel>(backing)) ==
            veyra::WindowResult::ready,
        "the software surface still acquires after fallback"
    );
    require(surface.width == 64 && surface.height == 48, "fallback keeps the geometry");
    require(window.present() == veyra::WindowResult::ready, "the next frame still presents");
    require(
        veyra::notify_presented(choice, true) == veyra::PresentPath::software,
        "good software frames never silently re-arm the gpu"
    );
}

}

int main() {
    try {
        check_select_starts_on_cpu();
        check_failure_falls_back_without_retry();
        check_software_frame_survives_fallback();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
