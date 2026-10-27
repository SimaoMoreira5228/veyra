#include "veyra/platform/sdl3/sdl3_gpu_present.hpp"
#include "veyra/platform/sdl3/sdl3_window.hpp"
#include "veyra/render/present/present_choice.hpp"

#include <SDL3/SDL.h>

#include <array>
#include <cstdlib>
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

bool windowed_run() {
    const char* flag = std::getenv("VEYRA_GPU_WINDOWED");
    return flag != nullptr && flag[0] != '\0';
}

void check_headless_pins() {
    veyra::SdlGpuPresenter bad_name{};
    require(
        bad_name.create("veyra-no-such-driver") == veyra::WindowResult::unsupported,
        "an unknown driver name answers unsupported"
    );
    require(!bad_name.ready(), "a refused device never reads ready");
    require(bad_name.generation() == 0, "a refused device holds its generation");

    veyra::SdlGpuPresenter empty_name{};
    require(
        empty_name.create("") == veyra::WindowResult::invalid_input,
        "an empty driver name is invalid input"
    );

    veyra::SdlGpuPresenter presenter{};
    require(presenter.generation() == 0, "a fresh presenter holds generation zero");
    require(!presenter.ready(), "a fresh presenter holds no device");
    require(!presenter.claimed(), "a fresh presenter holds no window");
    require(
        std::string_view(presenter.device_driver()).empty(),
        "a fresh presenter names no driver"
    );

    veyra::Sdl3Window bad_window(nullptr, 320, 240);
    require(
        presenter.claim(bad_window) == veyra::WindowResult::invalid_state,
        "claiming without a device is invalid state"
    );

    veyra::SoftwareSurface surface{};
    std::array<veyra::SoftwarePixel, 64> backing{};
    require(
        presenter.acquire_surface(surface, backing) == veyra::WindowResult::invalid_state,
        "acquiring without a claim is invalid state"
    );
    require(
        presenter.present() == veyra::WindowResult::invalid_state,
        "presenting without an acquire is invalid state"
    );

    const veyra::WindowResult created = presenter.create();
    require(
        created == veyra::WindowResult::ready || created == veyra::WindowResult::unsupported,
        "a default device either creates or answers unsupported"
    );
    if (created != veyra::WindowResult::ready) {
        return;
    }

    require(presenter.ready(), "a created device reads ready");
    require(!presenter.claimed(), "creating alone claims nothing");
    require(
        presenter.create() == veyra::WindowResult::invalid_state,
        "a second create is invalid state"
    );
    require(
        std::string_view(presenter.device_driver()).size() > 0,
        "a live device names its driver"
    );

    veyra::Sdl3Window dummy("Veyra gpu dummy", 160, 120);
    require(
        dummy.creation_result() == veyra::WindowResult::ready,
        "the dummy window creates for the claim probe"
    );
    const veyra::WindowResult claimed = presenter.claim(dummy);
    require(
        claimed == veyra::WindowResult::ready || claimed == veyra::WindowResult::unsupported,
        "the dummy driver either claims or answers unsupported"
    );
    if (claimed == veyra::WindowResult::ready) {
        require(presenter.claimed(), "a good claim reads claimed");
        presenter.release();
        require(!presenter.claimed(), "release drops the claim");
    }
    require(
        presenter.acquire_surface(surface, backing) == veyra::WindowResult::invalid_state,
        "acquiring after release is invalid state"
    );
}

void paint_solid(std::span<veyra::SoftwarePixel> pixels, veyra::SoftwarePixel color) {
    for (auto& pixel : pixels) {
        pixel = color;
    }
}

bool await_pixels(SDL_Window* native, int want_width, int want_height) {
    for (int attempt = 0; attempt < 200; ++attempt) {
        int width = 0;
        int height = 0;
        SDL_GetWindowSizeInPixels(native, &width, &height);
        if (width == want_width && height == want_height) {
            return true;
        }
        SDL_PumpEvents();
        SDL_Delay(10);
    }

    return false;
}

void check_windowed_leg() {
    veyra::Sdl3Window window("Veyra gpu present", 640, 480);
    if (window.creation_result() != veyra::WindowResult::ready) {
        std::cerr << "SDL could not create a window in this environment\n";
        std::exit(77);
    }

    veyra::SdlGpuPresenter presenter{};
    if (presenter.create() != veyra::WindowResult::ready) {
        std::cerr << "SDL could not create a gpu device in this environment\n";
        std::exit(77);
    }
    std::cerr << "gpu driver: " << presenter.device_driver() << '\n';

    require(
        presenter.claim(window) == veyra::WindowResult::ready,
        "the real driver claims the window"
    );
    require(presenter.generation() == 1, "claiming a window bumps the generation");

    auto choice = veyra::PresentChoice{};
    veyra::select_present_path(choice, veyra::PresentPath::gpu);

    std::array<veyra::SoftwarePixel, 640 * 480> backing{};
    veyra::SoftwareSurface surface{};
    require(
        presenter.acquire_surface(surface, backing) == veyra::WindowResult::ready,
        "the claimed window acquires caller pixels"
    );
    require(
        surface.width == 640 && surface.height == 480,
        "the acquired surface keeps the window geometry"
    );
    paint_solid(surface.pixels, {.red = 32, .green = 96, .blue = 160, .alpha = 255});
    require(presenter.present() == veyra::WindowResult::ready, "the solid frame presents");
    require(
        veyra::notify_presented(choice, true) == veyra::PresentPath::gpu,
        "a presented gpu frame stays on the gpu"
    );
    require(presenter.generation() == 2, "first upload resources bump the generation");

    require(
        presenter.acquire_surface(surface, backing) == veyra::WindowResult::ready,
        "the second frame acquires"
    );
    paint_solid(surface.pixels, {.red = 160, .green = 96, .blue = 32, .alpha = 255});
    require(presenter.present() == veyra::WindowResult::ready, "the second frame presents");
    require(presenter.generation() == 2, "a same-size frame reuses its resources");

    SDL_Window* native = SDL_GetWindowFromID(static_cast<Uint32>(window.window_id()));
    require(native != nullptr, "the native window resolves for the resize probe");

    const std::uint64_t before_resize = presenter.generation();
    SDL_SetWindowSize(native, 320, 240);
    require(await_pixels(native, 320, 240), "the resize lands");

    std::array<veyra::SoftwarePixel, 640 * 480> large_backing{};
    require(
        presenter.acquire_surface(surface, large_backing) == veyra::WindowResult::ready,
        "the resized window acquires"
    );
    require(
        surface.width == 320 && surface.height == 240,
        "the acquired surface follows the resize"
    );
    paint_solid(surface.pixels, {.red = 96, .green = 160, .blue = 32, .alpha = 255});
    require(presenter.present() == veyra::WindowResult::ready, "the resized frame presents");
    require(presenter.generation() > before_resize, "the resize recreates upload resources");

    std::system("import -window root /tmp/opencode/sdl-gpu-present.png");

    presenter.release();
    require(!presenter.claimed(), "the windowed leg releases its claim");
}

}

int main() {
    try {
        if (windowed_run()) {
            check_windowed_leg();
        } else {
            check_headless_pins();
        }
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }

    return 0;
}
