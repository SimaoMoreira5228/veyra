#include "veyra/render/cpu/software_renderer.hpp"

#if defined(VEYRA_GALLERY_PARITY_HAS_SDLGPU)
#include "veyra/platform/sdl3/sdl3_gpu_present.hpp"
#include "veyra/platform/sdl3/sdl3_window.hpp"
#include "veyra/render/gpu/sdl/sdl_gpu_renderer.hpp"
#endif

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

constexpr std::size_t kWidth = 96;
constexpr std::size_t kHeight = 72;

void require(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

struct SoftwareRooms {
    std::array<veyra::SoftwareClipRect, 2> clips{};
    std::array<veyra::LayoutPoint, 2> translations{};
    std::array<double, 512> path_intersections{};
    std::array<std::int8_t, 512> path_windings{};
};

void render_reference(
    std::span<const veyra::DisplayCommand> commands,
    std::span<veyra::SoftwarePixel> pixels,
    SoftwareRooms& rooms
) {
    const veyra::SoftwarePixel black{.alpha = 255};
    for (auto& pixel : pixels) {
        pixel = black;
    }
    const veyra::SoftwareSurface surface{
        .width = kWidth,
        .height = kHeight,
        .stride = kWidth,
        .pixels = pixels,
    };
    const veyra::SoftwareRenderWorkspace workspace{
        .clips = rooms.clips,
        .translations = rooms.translations,
        .path_intersections = rooms.path_intersections,
        .path_windings = rooms.path_windings,
    };
    require(
        veyra::render_software(commands, surface, workspace) ==
            veyra::SoftwareRenderResult::complete,
        "software reference renders"
    );
}

void check_close(
    std::span<veyra::SoftwarePixel> expected,
    std::span<veyra::SoftwarePixel> actual,
    std::string_view stage,
    std::uint64_t tolerance
) {
    require(expected.size() == actual.size(), "parity compares equal extents");
    auto mismatches = std::size_t{};
    auto max_error = 0;
    for (auto index = std::size_t{}; index < expected.size(); ++index) {
        const auto& want = expected[index];
        const auto& got = actual[index];
        const auto channels = {want.red, want.green, want.blue, want.alpha};
        const auto results = {got.red, got.green, got.blue, got.alpha};
        auto bad = false;
        auto channel = channels.begin();
        for (const auto result : results) {
            const auto difference = static_cast<int>(*channel) - static_cast<int>(result);
            max_error = std::max(max_error, difference >= 0 ? difference : -difference);
            bad = bad || difference < -static_cast<int>(tolerance) ||
                difference > static_cast<int>(tolerance);
            ++channel;
        }
        if (bad) {
            if (mismatches < 8) {
                std::cout << "gallery mismatch x=" << (index % kWidth) << " y=" << (index / kWidth)
                          << " want=" << static_cast<int>(want.red) << ','
                          << static_cast<int>(want.green) << ',' << static_cast<int>(want.blue)
                          << ',' << static_cast<int>(want.alpha)
                          << " got=" << static_cast<int>(got.red) << ','
                          << static_cast<int>(got.green) << ',' << static_cast<int>(got.blue) << ','
                          << static_cast<int>(got.alpha) << '\n';
            }
            ++mismatches;
        }
    }
    if (mismatches != 0) {
        std::cout << "gallery mismatched_pixels=" << mismatches << '\n';
    }
    require(mismatches == 0, "gallery gpu pixels match software within tolerance");
    std::cout << "gallery stage=" << stage << " max_abs_error=" << max_error << '\n';
}

std::span<const veyra::DisplayCommand>
build_fills(std::array<veyra::DisplayCommand, 4>& storage) noexcept {
    storage[0] = veyra::FillRect{
        .bounds = {.width = static_cast<float>(kWidth), .height = static_cast<float>(kHeight)},
        .color = {.red = 12, .green = 20, .blue = 28, .alpha = 255},
    };
    storage[1] = veyra::FillRect{
        .bounds = {.x = 8.0F, .y = 10.0F, .width = 40.0F, .height = 30.0F},
        .color = {.red = 200, .green = 40, .blue = 30, .alpha = 255},
    };
    storage[2] = veyra::FillRect{
        .bounds = {.x = 30.0F, .y = 32.0F, .width = 50.0F, .height = 24.0F},
        .color = {.red = 40, .green = 70, .blue = 210, .alpha = 255},
    };
    storage[3] = veyra::FillRect{
        .bounds = {.x = -12.0F, .y = 56.0F, .width = 200.0F, .height = 40.0F},
        .color = {.red = 150, .green = 40, .blue = 170, .alpha = 255},
    };
    return {storage.data(), storage.size()};
}

std::span<const veyra::DisplayCommand>
build_rounded(std::array<veyra::DisplayCommand, 3>& storage) noexcept {
    storage[0] = veyra::FillRect{
        .bounds = {.width = static_cast<float>(kWidth), .height = static_cast<float>(kHeight)},
        .color = {.red = 12, .green = 20, .blue = 28, .alpha = 255},
    };
    storage[1] = veyra::FillRoundedRect{
        .bounds = {.x = 8.0F, .y = 10.0F, .width = 48.0F, .height = 32.0F},
        .radius = 6.0F,
        .color = {.red = 200, .green = 40, .blue = 30, .alpha = 255},
    };
    storage[2] = veyra::FillRoundedRect{
        .bounds = {.x = 40.0F, .y = 30.0F, .width = 48.0F, .height = 32.0F},
        .radius = 10.0F,
        .color = {.red = 40, .green = 70, .blue = 210, .alpha = 255},
    };
    return {storage.data(), storage.size()};
}

std::span<const veyra::DisplayCommand>
build_strokes(std::array<veyra::DisplayCommand, 4>& storage) noexcept {
    storage[0] = veyra::FillRect{
        .bounds = {.width = static_cast<float>(kWidth), .height = static_cast<float>(kHeight)},
        .color = {.red = 12, .green = 20, .blue = 28, .alpha = 255},
    };
    storage[1] = veyra::StrokeLine{
        .start = {.x = 6.0F, .y = 6.0F},
        .end = {.x = 90.0F, .y = 66.0F},
        .thickness = 2.0F,
        .color = {.red = 230, .green = 180, .blue = 20, .alpha = 255},
    };
    storage[2] = veyra::StrokeRect{
        .bounds = {.x = 10.0F, .y = 12.0F, .width = 40.0F, .height = 28.0F},
        .thickness = 1.0F,
        .color = {.red = 40, .green = 140, .blue = 60, .alpha = 255},
    };
    storage[3] = veyra::StrokeRoundedRect{
        .bounds = {.x = 48.0F, .y = 28.0F, .width = 40.0F, .height = 28.0F},
        .radius = 6.0F,
        .thickness = 2.0F,
        .color = {.red = 200, .green = 60, .blue = 30, .alpha = 255},
    };
    return {storage.data(), storage.size()};
}


#if defined(VEYRA_GALLERY_PARITY_HAS_SDLGPU)

bool check_sdlgpu(
    std::span<const veyra::DisplayCommand> fills,
    std::span<const veyra::DisplayCommand> rounded,
    std::span<const veyra::DisplayCommand> strokes,
    std::span<veyra::SoftwarePixel> expected,
    std::span<veyra::SoftwarePixel> actual,
    SoftwareRooms& rooms
) {
    auto window = veyra::Sdl3Window("veyra gallery parity", 160, 120);
    if (window.creation_result() != veyra::WindowResult::ready) {
        std::cout << "gallery sdl-gpu window unavailable, skipping\n";
        return false;
    }
    auto presenter = veyra::SdlGpuPresenter{};
    if (presenter.create() != veyra::WindowResult::ready) {
        std::cout << "gallery sdl-gpu device unavailable, skipping\n";
        return false;
    }
    if (presenter.claim(window) != veyra::WindowResult::ready) {
        presenter.release();
        std::cout << "gallery sdl-gpu window claim unavailable, skipping\n";
        return false;
    }
    auto renderer = veyra::SdlGpuRenderer{};
    if (veyra::create_sdl_gpu_renderer(presenter, kWidth, kHeight, renderer) !=
        veyra::WindowResult::ready) {
        presenter.release();
        std::cout << "gallery sdl-gpu renderer unavailable, skipping\n";
        return false;
    }
    auto stats = veyra::SdlGpuRenderStats{};
    const auto render = [&](std::span<const veyra::DisplayCommand> scene) {
        require(
            veyra::render_sdl_gpu_rects(renderer, scene, actual, stats) ==
                veyra::WindowResult::ready,
            "sdl-gpu gallery frame completes"
        );
    };
    render_reference(fills, expected, rooms);
    render(fills);
    check_close(expected, actual, "sdlgpu-fills", 0);
    render_reference(rounded, expected, rooms);
    render(rounded);
    check_close(expected, actual, "sdlgpu-rounded", 1);
    render_reference(strokes, expected, rooms);
    render(strokes);
    check_close(expected, actual, "sdlgpu-strokes", 2);
    veyra::destroy_sdl_gpu_renderer(renderer);
    presenter.release();
    require(!veyra::sdl_gpu_renderer_ready(renderer), "sdl-gpu renderer tears down");
    return true;
}

#endif

int run() {
    auto fills_storage = std::array<veyra::DisplayCommand, 4>{};
    auto rounded_storage = std::array<veyra::DisplayCommand, 3>{};
    auto strokes_storage = std::array<veyra::DisplayCommand, 4>{};
    const auto fills = build_fills(fills_storage);
    const auto rounded = build_rounded(rounded_storage);
    const auto strokes = build_strokes(strokes_storage);
    auto rooms = SoftwareRooms{};
    auto expected = std::array<veyra::SoftwarePixel, kWidth * kHeight>{};
    auto actual = std::array<veyra::SoftwarePixel, kWidth * kHeight>{};
    render_reference(fills, expected, rooms);
    render_reference(fills, actual, rooms);
    require(
        std::equal(
            expected.begin(),
            expected.end(),
            actual.begin(),
            [](const auto& a, const auto& b) {
                return a.red == b.red && a.green == b.green && a.blue == b.blue &&
                    a.alpha == b.alpha;
            }
        ),
        "software replay stays byte-identical"
    );
    auto proven = 0;
#if defined(VEYRA_GALLERY_PARITY_HAS_SDLGPU)
    proven += check_sdlgpu(fills, rounded, strokes, expected, actual, rooms) ? 1 : 0;
#endif
    if (proven == 0) {
        std::cout << "gallery gpu parity unavailable, skipping\n";
        return 77;
    }
    return 0;
}

}

int main() {
    try {
        return run();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
