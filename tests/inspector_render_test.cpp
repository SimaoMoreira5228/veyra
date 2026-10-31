#include "inspector_render.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <vector>

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
        constexpr auto width = std::size_t{400};
        constexpr auto height = std::size_t{300};
        auto pixels = std::vector<veyra::SoftwarePixel>(width * height);

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

        auto trace = std::array<veyra::TraceEvent, 7>{};
        trace[0].kind = veyra::TraceEventKind::route;
        trace[0].payload.route = {
            .event_kind = 1,
            .route_result = 0,
            .window = 1,
            .data1 = 640,
            .data2 = 480,
        };
        trace[1].kind = veyra::TraceEventKind::provenance;
        trace[1].payload.provenance = {.token_kind = 0, .token = 2, .layer = 2, .line = 9};
        trace[2].kind = veyra::TraceEventKind::focus;
        trace[3].kind = veyra::TraceEventKind::frame_begin;
        trace[3].time_ms = 10;
        trace[3].payload.frame.frame = 1;
        trace[4].kind = veyra::TraceEventKind::frame_end;
        trace[4].time_ms = 18;
        trace[4].payload.frame.frame = 1;
        trace[5].kind = veyra::TraceEventKind::frame_begin;
        trace[5].time_ms = 20;
        trace[5].payload.frame.frame = 2;
        trace[6].kind = veyra::TraceEventKind::frame_end;
        trace[6].time_ms = 35;
        trace[6].payload.frame.frame = 2;

        auto provenance = std::array<veyra::TokenProvenance, 4>{
            veyra::TokenProvenance{
                .kind = veyra::TokenKind::color,
                .token = static_cast<std::uint8_t>(veyra::ColorToken::surface),
                .layer = veyra::ThemeLayer::file,
                .line = 5,
            },
            veyra::TokenProvenance{
                .kind = veyra::TokenKind::color,
                .token = static_cast<std::uint8_t>(veyra::ColorToken::action_primary),
                .layer = veyra::ThemeLayer::override,
            },
            veyra::TokenProvenance{
                .kind = veyra::TokenKind::space,
                .token = static_cast<std::uint8_t>(veyra::SpaceToken::medium),
                .layer = veyra::ThemeLayer::preset,
            },
            veyra::TokenProvenance{
                .kind = veyra::TokenKind::motion,
                .token = static_cast<std::uint8_t>(veyra::MotionToken::normal),
                .layer = veyra::ThemeLayer::preset,
            },
        };

        auto view = veyra::InspectorView{
            .nodes = nodes,
            .trace = trace,
            .provenance = provenance,
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
        view.provenance = {};
        require(
            veyra::render_inspector(veyra::Theme::dark(), view, pixels, width, height) ==
                veyra::SoftwareRenderResult::complete,
            "inspector renders without token provenance"
        );
        require(checksum(pixels) != list_focused, "token provenance changes the inspector frame");

        provenance[0].layer = veyra::ThemeLayer::override;
        view.provenance = provenance;
        require(
            veyra::render_inspector(veyra::Theme::dark(), view, pixels, width, height) ==
                veyra::SoftwareRenderResult::complete,
            "inspector renders a changed provenance layer"
        );
        require(checksum(pixels) != list_focused, "provenance layers change the inspector frame");

        provenance[0].layer = veyra::ThemeLayer::file;
        view.trace = std::span<const veyra::TraceEvent>(trace.data(), 3);
        require(
            veyra::render_inspector(veyra::Theme::dark(), view, pixels, width, height) ==
                veyra::SoftwareRenderResult::complete,
            "inspector renders without frame timing"
        );
        require(checksum(pixels) != list_focused, "frame timing changes the inspector frame");

        provenance[0].line = 0;
        view.provenance = provenance;
        require(
            veyra::render_inspector(veyra::Theme::dark(), view, pixels, width, height) ==
                veyra::SoftwareRenderResult::complete,
            "inspector renders without a token line"
        );
        require(checksum(pixels) != list_focused, "token line numbers change the inspector frame");

        provenance[0].line = 5;
        view.provenance = provenance;
        trace[0].payload.route.data1 = 1280;
        require(
            veyra::render_inspector(veyra::Theme::dark(), view, pixels, width, height) ==
                veyra::SoftwareRenderResult::complete,
            "inspector renders changed route payload"
        );
        require(checksum(pixels) != list_focused, "route payloads change the inspector frame");

        trace[0].payload.route.data1 = 640;

        auto history = std::array<veyra::TraceEvent, 8>{};
        for (auto index = std::size_t{}; index < history.size(); ++index) {
            history[index].kind = veyra::TraceEventKind::route;
            history[index].payload.route = {
                .event_kind = 1,
                .route_result = 0,
                .window = 1,
                .data1 = static_cast<std::uint32_t>(100 + index),
                .data2 = 80,
            };
        }

        view.trace = history;
        view.event_scroll = 0;
        require(
            veyra::render_inspector(veyra::Theme::dark(), view, pixels, width, height) ==
                veyra::SoftwareRenderResult::complete,
            "inspector renders the latest event rows"
        );
        const auto latest_events = checksum(pixels);

        view.event_scroll = 2;
        require(
            veyra::render_inspector(veyra::Theme::dark(), view, pixels, width, height) ==
                veyra::SoftwareRenderResult::complete,
            "inspector renders older event rows"
        );
        require(checksum(pixels) != latest_events, "event scrolling changes the inspector frame");

        view.event_scroll = 0;
        history[7].payload.route.data1 = 999;
        require(
            veyra::render_inspector(veyra::Theme::dark(), view, pixels, width, height) ==
                veyra::SoftwareRenderResult::complete,
            "inspector renders a changed event payload"
        );
        require(checksum(pixels) != latest_events, "event payloads change the inspector frame");

        view.trace = trace;
        view.event_scroll = 0;
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

        const auto* glyph = veyra::DebugFont::instance().mask('A');
        require(glyph != nullptr, "the debug font exposes a glyph");
        require(
            glyph->width == 10 && glyph->height == 14,
            "the debug glyph is scaled for the inspector"
        );
        auto label_pixels = std::array<veyra::SoftwarePixel, std::size_t{120} * 30>{};
        auto label_commands = std::array<veyra::DisplayCommand, 1>{veyra::DrawMask{
            .x = 2.0F,
            .y = 2.0F,
            .mask = glyph,
            .color = {.red = 255, .green = 255, .blue = 255},
        }};
        auto label_clips = std::array<veyra::SoftwareClipRect, 1>{};
        auto label_translations = std::array<veyra::LayoutPoint, 1>{};
        const veyra::SoftwareSurface label_surface{
            .width = 120,
            .height = 30,
            .stride = 120,
            .pixels = label_pixels,
        };
        const veyra::SoftwareRenderWorkspace label_workspace{
            .clips = label_clips,
            .translations = label_translations,
        };
        require(
            veyra::render_software(label_commands, label_surface, label_workspace) ==
                veyra::SoftwareRenderResult::complete,
            "the debug glyph renders"
        );
        auto lit = std::size_t{};
        for (const auto& pixel : label_pixels) {
            if (pixel.red != 0) {
                ++lit;
            }
        }
        require(lit > 20, "the debug glyph paints coverage pixels");

        auto narrow = std::vector<veyra::SoftwarePixel>(width * height);
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
