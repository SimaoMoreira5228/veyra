#include "inspector_render.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string_view>

namespace {

void require(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

std::uint64_t checksum(std::span<const veyra::SoftwarePixel> pixels) noexcept {
    auto hash = std::uint64_t{1469598103934665603U};
    for (const auto& pixel : pixels) {
        hash ^= pixel.red;
        hash *= 1099511628211U;
        hash ^= pixel.green;
        hash *= 1099511628211U;
        hash ^= pixel.blue;
        hash *= 1099511628211U;
        hash ^= pixel.alpha;
        hash *= 1099511628211U;
    }
    return hash;
}

}

int main() {
    try {
        constexpr auto width = std::size_t{240};
        constexpr auto height = std::size_t{160};
        auto pixels = std::array<veyra::SoftwarePixel, width * height>{};

        const auto root = veyra::NodeId{.index = 0, .generation = 1};
        const auto list = veyra::NodeId{.index = 1, .generation = 1};
        const auto field = veyra::NodeId{.index = 2, .generation = 1};
        const std::array nodes{
            veyra::SemanticNode{
                .id = root,
                .role = veyra::SemanticRole::window,
                .bounds = {.width = 480.0F, .height = 320.0F},
            },
            veyra::SemanticNode{
                .id = list,
                .parent = root,
                .role = veyra::SemanticRole::list,
                .bounds = {.x = 8.0F, .y = 48.0F, .width = 280.0F, .height = 264.0F},
            },
            veyra::SemanticNode{
                .id = field,
                .parent = root,
                .role = veyra::SemanticRole::text_input,
                .bounds = {.x = 296.0F, .y = 8.0F, .width = 176.0F, .height = 24.0F},
            },
        };

        auto trace = std::array<veyra::TraceEvent, 3>{};
        trace[0].kind = veyra::TraceEventKind::route;
        trace[1].kind = veyra::TraceEventKind::provenance;
        trace[2].kind = veyra::TraceEventKind::focus;

        auto view = veyra::InspectorView{
            .nodes = nodes,
            .trace = trace,
            .focused = list,
            .window_focused = true,
        };
        require(
            veyra::render_inspector(veyra::Theme::dark(), view, pixels, width, height) ==
                veyra::SoftwareRenderResult::complete,
            "inspector renders"
        );
        const auto list_focused = checksum(pixels);

        view.trace = {};
        require(
            veyra::render_inspector(veyra::Theme::dark(), view, pixels, width, height) ==
                veyra::SoftwareRenderResult::complete,
            "inspector renders without trace events"
        );
        require(checksum(pixels) != list_focused, "trace events change the inspector frame");

        view.trace = trace;
        view.focused = field;
        require(
            veyra::render_inspector(veyra::Theme::dark(), view, pixels, width, height) ==
                veyra::SoftwareRenderResult::complete,
            "inspector renders a different focus"
        );
        require(checksum(pixels) != list_focused, "focus changes the inspector frame");

        require(
            veyra::render_inspector(
                veyra::Theme::dark(),
                {.nodes = {}, .window_focused = false},
                std::span<veyra::SoftwarePixel>(pixels.data(), 8),
                width,
                height
            ) == veyra::SoftwareRenderResult::invalid_surface,
            "a short surface is rejected"
        );

        auto narrow = std::array<veyra::SoftwarePixel, width * height>{};
        require(
            veyra::render_inspector(
                veyra::Theme::dark(),
                {.nodes = {}, .window_focused = false},
                narrow,
                width,
                height
            ) == veyra::SoftwareRenderResult::complete,
            "an empty snapshot still renders the frame"
        );
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }

    return 0;
}
