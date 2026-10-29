#include "app_render.hpp"
#include "app_semantics.hpp"
#include "inspector_render.hpp"
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
    auto main_window = veyra::WindowId{};
    auto inspector = veyra::WindowId{};

    if (app.create("veyra app", veyra::kAppWidth, veyra::kAppHeight, {}, main_window) !=
        veyra::WindowSetResult::ok) {
        std::cerr << "sdl window failed to open\n";
        return 1;
    }
    if (app.create("veyra inspector", 240, 160, {.owner = main_window}, inspector) !=
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

    static std::array<veyra::SoftwarePixel, veyra::kAppWidth * veyra::kAppHeight> backing{};
    static std::array<veyra::SoftwarePixel, veyra::kAppWidth * veyra::kAppHeight>
        inspector_backing{};

    auto on_event = [&](veyra::WindowId window, veyra::WindowEvent& event) noexcept {
        if (event.kind == veyra::WindowEventKind::close_request) {
            app.request_close(window, veyra::CloseReason::user);
            return window == main_window;
        }
        if (window == main_window) {
            const bool quit = model.handle_event(event);
            app.set_focused_node(main_window, semantics.focus_node(model.focused()));
            app.scheduler().request_immediate();
            return quit;
        }
        return false;
    };
    auto on_frame = [&](veyra::WindowId window, bool) noexcept -> veyra::FrameAction {
        auto* backend = app.host().backend(window);
        if (backend == nullptr) {
            return {.failed = true};
        }

        if (window == inspector) {
            const auto width = backend->pixel_width();
            const auto height = backend->pixel_height();

            auto nodes = std::array<veyra::SemanticNode, 32>{};
            auto node_count = std::size_t{};
            if (semantics.snapshot(app.windows(), main_window, nodes, node_count) !=
                veyra::SemanticSnapshotResult::complete) {
                return {.failed = true};
            }

            const auto view = veyra::InspectorView{
                .nodes = std::span<const veyra::SemanticNode>(nodes.data(), node_count),
                .focused = app.windows().focused_node(main_window),
                .window_focused = app.windows().focused_node(inspector) == semantics.inspector_root,
            };
            const auto display = veyra::build_inspector_display(model.theme(), view, width, height);
            if (display.count == 0) {
                return {.failed = true};
            }
            if (app.commit(
                    inspector,
                    std::span<const veyra::DisplayCommand>(display.commands.data(), display.count),
                    width,
                    height
                ) != veyra::WindowSetResult::ok) {
                return {.failed = true};
            }
            if (app.present(inspector, inspector_backing) != veyra::WindowSetResult::ok) {
                return {.failed = true};
            }
            return {.rendered = true};
        }

        if (window != main_window) {
            return {};
        }

        const auto width = backend->pixel_width();
        const auto height = backend->pixel_height();
        const auto display = veyra::build_app_display(model, width, height);
        if (display.count == 0) {
            return {.failed = true};
        }
        if (app.commit(
                main_window,
                std::span<const veyra::DisplayCommand>(display.commands.data(), display.count),
                width,
                height
            ) != veyra::WindowSetResult::ok) {
            return {.failed = true};
        }
        if (app.present(main_window, backing) != veyra::WindowSetResult::ok) {
            return {.failed = true};
        }
        return {.rendered = true, .quit = model.quit_requested()};
    };

    const std::uint64_t cap = options.has_frames ? options.frames : 0;
    const auto outcome = app.run(on_event, on_frame, &app_sleep, cap);
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
