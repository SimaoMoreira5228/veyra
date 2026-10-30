#include "app_render.hpp"
#include "app_semantics.hpp"
#include "inspector_render.hpp"
#include "veyra/inspector/trace/trace_recorder.hpp"
#include "veyra/text/shaping/font_discovery.hpp"

#if defined(VEYRA_APP_HAS_SDL3)
#include "veyra/app/sdl3/application.hpp"
#endif

#include <array>
#include <charconv>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <span>
#include <string>
#include <string_view>
#include <thread>

namespace {

constexpr std::string_view kUsage =
    "usage: veyra_app [--backend null|sdl] [--theme PATH] [--frames N] [--shot OUT.ppm]";

struct Options {
    std::string_view backend{};
    std::string_view theme_path{};
    std::string_view shot_path{};
    std::uint64_t frames{};
    bool has_frames{};
};

constexpr std::uint32_t kKeyPageUp = 0x40000055U;
constexpr std::uint32_t kKeyPageDown = 0x40000056U;

struct RoutePayload {
    std::uint32_t first{};
    std::uint32_t second{};
};

RoutePayload route_payload(const veyra::WindowEvent& event) noexcept {
    const auto signed_bits = [](float value) noexcept {
        return static_cast<std::uint32_t>(static_cast<std::int32_t>(value));
    };

    switch (event.kind) {
    case veyra::WindowEventKind::resize:
        return {
            .first = static_cast<std::uint32_t>(event.data.resize.width),
            .second = static_cast<std::uint32_t>(event.data.resize.height),
        };
    case veyra::WindowEventKind::scale_change:
        return {
            .first = static_cast<std::uint32_t>(event.data.scale.x * 100.0F),
            .second = static_cast<std::uint32_t>(event.data.scale.y * 100.0F),
        };
    case veyra::WindowEventKind::pointer_down:
    case veyra::WindowEventKind::pointer_up:
    case veyra::WindowEventKind::pointer_move:
    case veyra::WindowEventKind::pointer_cancel:
        return {
            .first = signed_bits(event.data.pointer.position.x),
            .second = signed_bits(event.data.pointer.position.y),
        };
    case veyra::WindowEventKind::wheel:
        return {
            .first = signed_bits(event.data.wheel.delta_x * 100.0F),
            .second = signed_bits(event.data.wheel.delta_y * 100.0F),
        };
    case veyra::WindowEventKind::key_down:
    case veyra::WindowEventKind::key_up:
        return {.first = event.data.key.code, .second = event.data.key.modifiers};
    case veyra::WindowEventKind::state_change:
        return {.first = static_cast<std::uint32_t>(event.data.state)};
    case veyra::WindowEventKind::text_commit:
        return {.first = event.data.commit.length};
    case veyra::WindowEventKind::text_editing:
        return {.first = event.data.editing.length};
    default:
        return {};
    }
}

std::uint64_t parse_count(const char* value) {
    const std::string_view text(value);
    std::uint64_t parsed = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), parsed);
    if (error != std::errc{} || end != text.data() + text.size()) {
        throw std::invalid_argument("expected a non-negative integer");
    }
    return parsed;
}

Options parse_options(int argc, char** argv) {
    auto options = Options{};
    for (auto index = 1; index < argc; ++index) {
        const std::string_view token(argv[index]);
        if (token == "--backend" || token == "--theme" || token == "--shot" ||
            token == "--frames") {
            ++index;
            if (index >= argc) {
                throw std::invalid_argument(std::string(kUsage));
            }
            const std::string_view value(argv[index]);
            if (value.empty()) {
                throw std::invalid_argument(std::string(kUsage));
            }
            if (token == "--backend") {
                options.backend = value;
            } else if (token == "--theme") {
                options.theme_path = value;
            } else if (token == "--shot") {
                options.shot_path = value;
            } else {
                options.frames = parse_count(argv[index]);
                options.has_frames = true;
            }
            continue;
        }
        throw std::invalid_argument(std::string(kUsage));
    }
    return options;
}

std::string read_file(const std::string_view path) {
    std::ifstream input{std::string(path), std::ios::binary};
    if (!input) {
        throw std::runtime_error("app theme file cannot be opened");
    }
    auto body = std::string{};
    input.seekg(0, std::ios::end);
    const auto size = input.tellg();
    input.seekg(0, std::ios::beg);
    if (size > 0) {
        body.resize(static_cast<std::size_t>(size));
        input.read(body.data(), static_cast<std::streamsize>(body.size()));
    } else {
        body.assign(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
    }
    if (!input.eof() && !input.good()) {
        throw std::runtime_error("app theme file cannot be read");
    }
    return body;
}

void write_ppm(const char* path, std::span<const veyra::SoftwarePixel> pixels) {
    auto body = std::string("P6\n") + std::to_string(veyra::kAppWidth) + " " +
        std::to_string(veyra::kAppHeight) + "\n255\n";
    body.reserve(body.size() + pixels.size() * 3);
    for (const auto& pixel : pixels) {
        body.push_back(static_cast<char>(pixel.red));
        body.push_back(static_cast<char>(pixel.green));
        body.push_back(static_cast<char>(pixel.blue));
    }
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        throw std::runtime_error("app screenshot cannot be opened");
    }
    output.write(body.data(), static_cast<std::streamsize>(body.size()));
    output.flush();
    if (!output) {
        throw std::runtime_error("app screenshot cannot be written");
    }
}

void app_sleep(unsigned ms) noexcept {
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

struct AppFonts {
    std::array<std::filesystem::path, veyra::kFontDiscoveryCapacity> storage{};
    std::size_t count{};
    veyra::AppFontSource source{veyra::AppFontSource::none};
};

AppFonts find_fonts() {
    auto fonts = AppFonts{};
    const auto found = veyra::default_font_search_paths(fonts.storage);
    if (found.count > 0) {
        fonts.count = found.count;
        fonts.source = veyra::AppFontSource::system;
        return fonts;
    }
    const std::string fallback = VEYRA_TESTDATA_FONTS_DIR;
    const std::array<std::string_view, 1> wanted{std::string_view(fallback)};
    const auto retry = veyra::discover_fonts_in(wanted, fonts.storage);
    if (retry.count > 0) {
        fonts.count = retry.count;
        fonts.source = veyra::AppFontSource::testdata;
    }
    return fonts;
}

bool use_sdl(const Options& options) {
#if defined(VEYRA_APP_HAS_SDL3)
    if (options.backend.empty()) {
        return true;
    }
    if (options.backend == "sdl") {
        return true;
    }
    if (options.backend == "null") {
        return false;
    }
    throw std::invalid_argument(std::string(kUsage));
#else
    if (options.backend.empty() || options.backend == "null") {
        return false;
    }
    throw std::invalid_argument("sdl backend unavailable in this build");
#endif
}

int run_null(const Options& options, const std::string& theme_text, const AppFonts& fonts) {
    auto window = veyra::NullWindow("veyra app", 480, 320);
    if (window.creation_result() != veyra::WindowResult::ready) {
        std::cerr << "null window failed to open\n";
        return 1;
    }
    auto model = veyra::AppModel{};
    model.open(
        std::span<const std::filesystem::path>(fonts.storage.data(), fonts.count),
        fonts.source,
        theme_text
    );
    auto scheduler = veyra::FrameScheduler{};
    static std::array<veyra::SoftwarePixel, veyra::kAppWidth * veyra::kAppHeight> backing{};
    const std::uint64_t cap = options.has_frames ? options.frames : 1;
    const auto outcome = veyra::run_app(window, model, scheduler, backing, &app_sleep, cap);
    if (!options.shot_path.empty()) {
        write_ppm(std::string(options.shot_path).c_str(), backing);
    }
    if (outcome == veyra::LoopResult::failed) {
        std::cerr << "null run failed\n";
        return 1;
    }
    return 0;
}

#if defined(VEYRA_APP_HAS_SDL3)
int run_sdl(const Options& options, const std::string& theme_text, const AppFonts& fonts) {
    auto app = veyra::app::sdl3::Sdl3Application{};
    auto trace_storage = std::array<veyra::TraceEvent, 256>{};
    auto trace = veyra::TraceRecorder(trace_storage);
    trace.set_enabled(true);
    struct RouteTrace {
        veyra::TraceRecorder* recorder;
    };
    auto route_trace = RouteTrace{&trace};
    app.set_route_observer(
        [](void* user, const veyra::WindowEvent& event, veyra::WindowRouteResult result) noexcept {
            auto* state = static_cast<RouteTrace*>(user);
            const auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
                                 std::chrono::steady_clock::now().time_since_epoch()
            )
                                 .count();
            const auto payload = route_payload(event);
            state->recorder->record_route(
                static_cast<std::uint64_t>(now),
                static_cast<std::uint8_t>(event.kind),
                static_cast<std::uint8_t>(result),
                event.window,
                payload.first,
                payload.second
            );
        },
        &route_trace
    );
    auto main_window = veyra::WindowId{};
    auto inspector = veyra::WindowId{};

    if (app.create("veyra app", veyra::kAppWidth, veyra::kAppHeight, {}, main_window) !=
        veyra::WindowSetResult::ok) {
        std::cerr << "sdl window failed to open\n";
        return 1;
    }
    if (app.create("veyra inspector", 400, 300, {.owner = main_window}, inspector) !=
        veyra::WindowSetResult::ok) {
        std::cerr << "sdl inspector failed to open\n";
        return 1;
    }
    if (app.show(main_window) != veyra::WindowSetResult::ok ||
        app.show(inspector) != veyra::WindowSetResult::ok) {
        std::cerr << "sdl window failed to show\n";
        return 1;
    }

    auto semantics = veyra::AppSemantics{};
    app.bind_semantics(main_window, semantics.tree(), semantics.root);
    app.bind_semantics(inspector, semantics.tree(), semantics.inspector_root);

    auto model = veyra::AppModel{};
    model.open(
        std::span<const std::filesystem::path>(fonts.storage.data(), fonts.count),
        fonts.source,
        theme_text
    );
    app.set_focused_node(main_window, semantics.focus_node(model.focused()));
    app.set_focused_node(inspector, semantics.inspector_root);

    auto provenance_storage = std::array<veyra::TokenProvenance, 32>{};
    auto provenance_count = std::size_t{};
    const auto collect_provenance = [&]() noexcept {
        provenance_count = 0;
        const auto add = [&](veyra::TokenKind kind, std::size_t count) noexcept {
            for (auto index = std::size_t{}; index < count; ++index) {
                const auto provenance =
                    model.theme().provenance(kind, static_cast<std::uint8_t>(index));
                if (provenance.layer == veyra::ThemeLayer::defaults ||
                    provenance_count == provenance_storage.size()) {
                    continue;
                }
                provenance_storage[provenance_count] = provenance;
                ++provenance_count;
            }
        };
        add(veyra::TokenKind::color, static_cast<std::size_t>(veyra::ColorToken::count));
        add(veyra::TokenKind::space, static_cast<std::size_t>(veyra::SpaceToken::count));
        add(veyra::TokenKind::radius, static_cast<std::size_t>(veyra::RadiusToken::count));
        add(veyra::TokenKind::motion, static_cast<std::size_t>(veyra::MotionToken::count));
    };
    collect_provenance();
    for (auto index = std::size_t{}; index < provenance_count; ++index) {
        const auto& provenance = provenance_storage[index];
        trace.record_provenance(
            0,
            static_cast<std::uint8_t>(provenance.kind),
            provenance.token,
            static_cast<std::uint8_t>(provenance.layer),
            provenance.line
        );
    }

    static std::array<veyra::SoftwarePixel, veyra::kAppWidth * veyra::kAppHeight> backing{};
    static std::array<veyra::SoftwarePixel, veyra::kAppWidth * veyra::kAppHeight>
        inspector_backing{};

    auto main_width = std::uint32_t{};
    auto main_height = std::uint32_t{};
    auto inspector_width = std::uint32_t{};
    auto inspector_height = std::uint32_t{};
    if (!app.surface_size(main_window, main_width, main_height) ||
        !app.surface_size(inspector, inspector_width, inspector_height)) {
        std::cerr << "sdl surface size failed\n";
        return 1;
    }

    auto frame_id = std::uint64_t{};
    auto inspector_scroll = std::size_t{};
    const auto now_ms = []() noexcept -> std::uint64_t {
        return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                              std::chrono::steady_clock::now().time_since_epoch()
        )
                                              .count());
    };

    auto on_event = [&](veyra::WindowId window, veyra::WindowEvent& event) noexcept {
        if (event.kind == veyra::WindowEventKind::close_request) {
            app.request_close(window, veyra::CloseReason::user);
            return window == main_window;
        }
        if (event.kind == veyra::WindowEventKind::resize ||
            event.kind == veyra::WindowEventKind::scale_change) {
            if (window == main_window) {
                app.surface_size(main_window, main_width, main_height);
            } else if (window == inspector) {
                app.surface_size(inspector, inspector_width, inspector_height);
            }
        }
        if (window == inspector) {
            if (event.kind == veyra::WindowEventKind::key_down) {
                if (event.data.key.code == kKeyPageUp || event.data.key.code == veyra::kAppKeyUp) {
                    ++inspector_scroll;
                } else if (
                    (event.data.key.code == kKeyPageDown ||
                     event.data.key.code == veyra::kAppKeyDown) &&
                    inspector_scroll > 0
                ) {
                    --inspector_scroll;
                }
                app.scheduler().request_immediate();
            } else if (event.kind == veyra::WindowEventKind::wheel) {
                if (event.data.wheel.delta_y > 0.0F && inspector_scroll > 0) {
                    --inspector_scroll;
                } else if (event.data.wheel.delta_y < 0.0F) {
                    ++inspector_scroll;
                }
                app.scheduler().request_immediate();
            }
            return false;
        }
        if (window == main_window) {
            if (event.kind == veyra::WindowEventKind::pointer_down) {
                auto target = veyra::NodeId{};
                app.press_pointer(
                    main_window,
                    event.data.pointer.pointer,
                    event.data.pointer.position,
                    semantics.bounds(),
                    target
                );
            } else if (
                event.kind == veyra::WindowEventKind::pointer_move ||
                event.kind == veyra::WindowEventKind::pointer_up
            ) {
                auto target = veyra::NodeId{};
                app.hit_test_and_hover(
                    main_window,
                    event.data.pointer.position,
                    semantics.bounds(),
                    target
                );
            }
            const bool quit = model.handle_event(event);
            app.set_focused_node(main_window, semantics.focus_node(model.focused()));
            app.scheduler().request_immediate();
            return quit;
        }
        return false;
    };

    auto submit = [&](veyra::WindowId window,
                      std::span<const veyra::DisplayCommand> commands,
                      std::uint32_t width,
                      std::uint32_t height,
                      std::span<veyra::SoftwarePixel> pixels) noexcept -> bool {
        auto result = app.commit(window, commands, width, height);
        if (result == veyra::WindowSetResult::capacity) {
            if (!app.wait_presented()) {
                return false;
            }

            auto previous = veyra::WindowSetResult::ok;
            if (app.take_present_result(window, previous) &&
                previous != veyra::WindowSetResult::ok) {
                return false;
            }
            result = app.commit(window, commands, width, height);
        }
        if (result != veyra::WindowSetResult::ok) {
            return false;
        }

        return app.present_async(window, pixels) == veyra::WindowSetResult::ok;
    };

    auto on_frame = [&](veyra::WindowId window, bool) noexcept -> veyra::FrameAction {
        const auto frame = ++frame_id;
        const auto frame_start = now_ms();
        trace.record_frame_begin(frame_start, frame);

        if (window == inspector) {
            collect_provenance();
            auto nodes = std::array<veyra::SemanticNode, 32>{};
            auto node_count = std::size_t{};
            if (semantics.snapshot(app.windows(), main_window, nodes, node_count) !=
                veyra::SemanticSnapshotResult::complete) {
                return {.failed = true};
            }

            const auto view = veyra::InspectorView{
                .nodes = std::span<const veyra::SemanticNode>(nodes.data(), node_count),
                .trace = trace.events(),
                .provenance = std::span<const veyra::TokenProvenance>(
                    provenance_storage.data(),
                    provenance_count
                ),
                .focused = app.windows().focused_node(main_window),
                .window_focused = app.windows().focused_node(inspector) == semantics.inspector_root,
            };
            const auto display = veyra::build_inspector_display(
                model.theme(),
                view,
                inspector_width,
                inspector_height
            );
            if (display.count == 0 || display.overflowed) {
                return {.failed = true};
            }
            if (!submit(
                    inspector,
                    std::span<const veyra::DisplayCommand>(display.commands.data(), display.count),
                    inspector_width,
                    inspector_height,
                    inspector_backing
                )) {
                return {.failed = true};
            }
            trace.record_frame_end(now_ms(), frame);
            return {.rendered = true};
        }

        if (window != main_window) {
            return {};
        }

        const auto display = veyra::build_app_display(model, main_width, main_height);
        if (display.count == 0 || display.overflowed) {
            return {.failed = true};
        }
        if (!submit(
                main_window,
                std::span<const veyra::DisplayCommand>(display.commands.data(), display.count),
                main_width,
                main_height,
                backing
            )) {
            return {.failed = true};
        }
        trace.record_frame_end(now_ms(), frame);
        return {.rendered = true, .quit = model.quit_requested()};
    };

    const std::uint64_t cap = options.has_frames ? options.frames : 0;
    const auto outcome = app.run(on_event, on_frame, &app_sleep, cap);
    app.wait_presented();
    if (app.windows().open_count() > 0) {
        app.request_close(main_window, veyra::CloseReason::application);
    }
    if (!options.shot_path.empty()) {
        write_ppm(std::string(options.shot_path).c_str(), backing);
    }
    if (outcome == veyra::LoopResult::failed) {
        std::cerr << "sdl run failed\n";
        return 1;
    }
    return 0;
}

#endif

int run(int argc, char** argv) {
    const Options options = parse_options(argc, argv);
    const std::string_view theme_path =
        options.theme_path.empty() ? VEYRA_APP_THEME_PATH : options.theme_path;
    const std::string theme_text = read_file(theme_path);
    const AppFonts fonts = find_fonts();
    if (use_sdl(options)) {
#if defined(VEYRA_APP_HAS_SDL3)
        return run_sdl(options, theme_text, fonts);
#endif
    }
    return run_null(options, theme_text, fonts);
}

}

int main(int argc, char** argv) {
    try {
        return run(argc, argv);
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 2;
    }
}
