#ifndef VEYRA_TESTS_SDL_GPU_PARITY_HELPERS_HPP
#define VEYRA_TESTS_SDL_GPU_PARITY_HELPERS_HPP

#include "veyra/platform/sdl3/sdl3_gpu_present.hpp"
#include "veyra/platform/sdl3/sdl3_window.hpp"
#include "veyra/render/gpu/sdl/sdl_gpu_renderer.hpp"
#include "veyra/render/cpu/software_renderer.hpp"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>

namespace veyra::sdl_gpu_parity {

using TestClock = std::chrono::steady_clock;

inline void require(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

inline std::uint64_t elapsed_ns(TestClock::time_point begin, TestClock::time_point end) noexcept {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(end - begin).count()
    );
}

struct SoftwareRooms {
    std::array<SoftwareClipRect, 2> clips{};
    std::array<LayoutPoint, 2> translations{};
    std::array<double, 512> path_intersections{};
    std::array<std::int8_t, 512> path_windings{};
};

inline SoftwareRenderWorkspace workspace_view(SoftwareRooms& rooms) noexcept {
    return SoftwareRenderWorkspace{
        .clips = rooms.clips,
        .translations = rooms.translations,
        .path_intersections = rooms.path_intersections,
        .path_windings = rooms.path_windings,
    };
}

inline void fill_black(std::span<SoftwarePixel> pixels) noexcept {
    const auto black = SoftwarePixel{.alpha = 255};

    for (auto& pixel : pixels) {
        pixel = black;
    }
}

inline void render_reference(
    std::span<const DisplayCommand> commands,
    std::span<SoftwarePixel> pixels,
    std::size_t width,
    std::size_t height,
    SoftwareRooms& rooms
) {
    fill_black(pixels);

    const auto surface = SoftwareSurface{
        .width = width,
        .height = height,
        .stride = width,
        .pixels = pixels,
    };

    const auto result = render_software(commands, surface, workspace_view(rooms));

    require(result == SoftwareRenderResult::complete, "software reference renders");
}

struct SdlGpuFixture {
    std::unique_ptr<Sdl3Window> window{};
    SdlGpuPresenter presenter{};
    SdlGpuRenderer renderer{};

    bool setup(std::size_t width, std::size_t height) {
        window = std::make_unique<Sdl3Window>("Veyra sdl gpu parity", 160, 120);

        if (window->creation_result() != WindowResult::ready) {
            return false;
        }

        if (presenter.create() != WindowResult::ready) {
            return false;
        }

        if (presenter.claim(*window) != WindowResult::ready) {
            return false;
        }

        if (create_sdl_gpu_renderer(presenter, width, height, renderer) != WindowResult::ready) {
            return false;
        }

        return true;
    }

    void teardown() {
        destroy_sdl_gpu_renderer(renderer);
        presenter.release();
        window.reset();
    }
};

inline void check_parity(
    SdlGpuRenderer& renderer,
    std::span<const DisplayCommand> commands,
    std::span<SoftwarePixel> expected,
    std::span<SoftwarePixel> actual,
    std::string_view stage,
    std::uint64_t tolerance
) {
    auto stats = SdlGpuRenderStats{};

    const auto begin = TestClock::now();
    const auto result = render_sdl_gpu_rects(renderer, commands, actual, stats);
    const auto render_ns = elapsed_ns(begin, TestClock::now());

    require(result == WindowResult::ready, "sdl gpu submit completes");

    require(expected.size() == actual.size(), "parity compares equal extents");

    auto mismatches = std::size_t{};
    auto max_error = 0;

    const auto within_tolerance = [&max_error, tolerance](std::uint8_t want, std::uint8_t got) {
        const auto difference = static_cast<int>(want) - static_cast<int>(got);

        max_error = std::max(max_error, difference >= 0 ? difference : -difference);

        return difference >= -static_cast<int>(tolerance) &&
            difference <= static_cast<int>(tolerance);
    };

    for (auto i = std::size_t{}; i < expected.size(); ++i) {
        const auto& want = expected[i];
        const auto& got = actual[i];

        if (!within_tolerance(want.red, got.red) || !within_tolerance(want.green, got.green) ||
            !within_tolerance(want.blue, got.blue) || !within_tolerance(want.alpha, got.alpha)) {
            if (mismatches < 8) {
                std::cout << "sdl-gpu mismatch x=" << (i % renderer.width)
                          << " y=" << (i / renderer.width);
                std::cout << " want=" << static_cast<int>(want.red) << ','
                          << static_cast<int>(want.green) << ',' << static_cast<int>(want.blue)
                          << ',' << static_cast<int>(want.alpha);
                std::cout << " got=" << static_cast<int>(got.red) << ','
                          << static_cast<int>(got.green) << ',' << static_cast<int>(got.blue) << ','
                          << static_cast<int>(got.alpha) << '\n';
            }

            ++mismatches;
        }
    }

    if (mismatches != 0) {
        std::cout << "sdl-gpu mismatched_pixels=" << mismatches << '\n';
    }

    require(mismatches == 0, "sdl gpu pixels match software within tolerance");

    std::cout << "sdl-gpu stage=" << stage << " render_ns=" << render_ns;
    std::cout << " submitted=" << stats.submitted;
    std::cout << " draws=" << stats.draws;
    std::cout << " rect_draws=" << stats.rect_draws;
    std::cout << " rounded_draws=" << stats.rounded_draws;
    std::cout << " image_draws=" << stats.image_draws;
    std::cout << " mask_draws=" << stats.mask_draws;
    std::cout << " stroke_draws=" << stats.stroke_draws;
    std::cout << " rounded_stroke_draws=" << stats.rounded_stroke_draws;
    std::cout << " glyph_draws=" << stats.glyph_draws;
    std::cout << " uploads=" << stats.uploads;
    std::cout << " upload_bytes=" << stats.upload_bytes;
    std::cout << " max_abs_error=" << max_error << '\n';
}

}

#endif
