#ifndef VEYRA_RENDER_E3_SCENES_HPP
#define VEYRA_RENDER_E3_SCENES_HPP

#include "veyra/render/software_renderer.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace veyra {

// NOTE: glyph runs are fixed-coverage mask tiles, not shaped text. Shaping
// needs font bytes and I/O; these scenes must stay freestanding and
// deterministic, so three tile widths stand in for latin, CJK, and arabic runs.

constexpr std::size_t kE3Width = 1280;
constexpr std::size_t kE3Height = 720;

enum class E3Scene : std::uint8_t {
    shadowed_boxes_600,
    row_scroll_1000,
    virtual_rows_100k,
    dashboard_12,
    changing_text,
    chat_cjk_arabic,
    translucent_overlays,
    explorer_1m,
};

enum class E3BuildStatus : std::uint8_t {
    complete,
    truncated,
    invalid_scene,
};

struct E3BuildResult {
    E3BuildStatus status{E3BuildStatus::invalid_scene};
    std::size_t produced{};
    std::uint64_t content_hash{};
};

struct E3Scratch {
    std::array<LayoutPoint, 96> polyline_points{};
    std::array<std::uint8_t, 288> mask_coverage{};
    std::array<SoftwareMask, 3> masks{};
    std::array<SoftwarePixel, 320> image_pixels{};
    std::array<SoftwareImage, 2> images{};
};

constexpr std::size_t e3_max_commands(E3Scene scene) noexcept {
    switch (scene) {
    case E3Scene::shadowed_boxes_600:
        return 1201;
    case E3Scene::row_scroll_1000:
        return 94;
    case E3Scene::virtual_rows_100k:
        return 121;
    case E3Scene::dashboard_12:
        return 133;
    case E3Scene::changing_text:
        return 43;
    case E3Scene::chat_cjk_arabic:
        return 61;
    case E3Scene::translucent_overlays:
        return 78;
    case E3Scene::explorer_1m:
        return 163;
    }

    return 0;
}

std::string_view e3_scene_name(E3Scene scene) noexcept;

bool e3_parse_scene(std::string_view name, E3Scene& scene) noexcept;

E3BuildResult e3_build_scene(
    E3Scene scene,
    std::uint64_t seed,
    std::size_t frame,
    std::span<DisplayCommand> out,
    E3Scratch& scratch
) noexcept;

}

#endif
