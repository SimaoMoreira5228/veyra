#include "../examples/app_render.hpp"
#include "allocation_counter.hpp"
#include "veyra/text/font_discovery.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

void require(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

veyra::WindowEvent make_key(std::uint32_t code, std::uint32_t modifiers = 0) {
    veyra::WindowEvent event{};
    event.kind = veyra::WindowEventKind::key_down;
    event.data.key = {.code = code, .modifiers = modifiers, .repeat = false};
    return event;
}

veyra::WindowEvent make_pointer(float x, float y) {
    veyra::WindowEvent event{};
    event.kind = veyra::WindowEventKind::pointer_down;
    event.data.pointer = {.position = {.x = x, .y = y}};
    return event;
}

veyra::WindowEvent make_commit(std::string_view text) {
    veyra::WindowEvent event{};
    event.kind = veyra::WindowEventKind::text_commit;
    require(text.size() <= event.data.commit.text.size(), "commit text fits the inline buffer");
    for (std::size_t index = 0; index < text.size(); ++index) {
        event.data.commit.text[index] = text[index];
    }
    event.data.commit.length = static_cast<std::uint32_t>(text.size());
    return event;
}

veyra::WindowEvent make_editing(std::string_view text) {
    veyra::WindowEvent event{};
    event.kind = veyra::WindowEventKind::text_editing;
    require(text.size() <= event.data.editing.text.size(), "preedit text fits the inline buffer");
    for (std::size_t index = 0; index < text.size(); ++index) {
        event.data.editing.text[index] = text[index];
    }
    event.data.editing.length = static_cast<std::uint32_t>(text.size());
    event.data.editing.cursor = static_cast<std::int32_t>(text.size());
    event.data.editing.selection_length = 0;
    return event;
}

veyra::WindowEvent make_close() {
    veyra::WindowEvent event{};
    event.kind = veyra::WindowEventKind::close_request;
    return event;
}

struct Harness {
    veyra::NullWindow window{"session", 480, 320};
    veyra::FrameScheduler scheduler{};
    veyra::AppModel model{};
    static std::array<veyra::SoftwarePixel, veyra::kAppWidth * veyra::kAppHeight>& backing() {
        static std::array<veyra::SoftwarePixel, veyra::kAppWidth * veyra::kAppHeight> pixels{};
        return pixels;
    }

    void pump() {
        auto on_event = [&](veyra::WindowEvent& event) noexcept {
            const bool quit = model.handle_event(event);
            scheduler.request_immediate();
            return quit;
        };
        auto on_frame = [&](bool) noexcept {
            auto surface = veyra::SoftwareSurface{};
            if (window.acquire_surface(surface, backing()) != veyra::WindowResult::ready) {
                return veyra::FrameAction{.failed = true};
            }
            const auto rendered =
                veyra::render_app(model, surface.pixels, surface.width, surface.height);
            if (rendered != veyra::SoftwareRenderResult::complete) {
                return veyra::FrameAction{.failed = true};
            }
            if (window.present() != veyra::WindowResult::ready) {
                return veyra::FrameAction{.failed = true};
            }
            return veyra::FrameAction{.rendered = true, .quit = model.quit_requested()};
        };
        auto config = veyra::RunLoopConfig{window, scheduler, on_event, on_frame, nullptr};
        const auto result = veyra::pump_once(config, veyra::FrameScheduler::Clock::now());
        require(
            result == veyra::LoopResult::rendered || result == veyra::LoopResult::idle ||
                result == veyra::LoopResult::quit,
            "pump runs the scripted events"
        );
    }

    void send(veyra::WindowEvent event) {
        require(window.push_event(event), "scripted event queues");
        pump();
    }

    std::uint64_t checksum() {
        auto& pixels = backing();
        require(
            veyra::render_app(model, pixels, veyra::kAppWidth, veyra::kAppHeight) ==
                veyra::SoftwareRenderResult::complete,
            "replay frame renders"
        );
        return veyra::app_checksum(pixels);
    }
};

std::string read_file(const std::string_view path) {
    std::ifstream input{std::string(path), std::ios::binary};
    require(input.good(), "app theme file opens");
    auto body = std::string{};
    input.seekg(0, std::ios::end);
    body.resize(static_cast<std::size_t>(input.tellg()));
    input.seekg(0, std::ios::beg);
    input.read(body.data(), static_cast<std::streamsize>(body.size()));
    return body;
}

std::string buffer_text(const veyra::AppModel& model) {
    std::array<char, 128> copy{};
    std::size_t length = 0;
    require(
        model.copy_buffer(copy, length) == veyra::TextEditResult::complete,
        "buffer copies into caller storage"
    );
    return {copy.data(), length};
}

void open_with_testdata(Harness& harness) {
    const std::string fonts = std::string(VEYRA_TESTDATA_DIR) + "/fonts";
    const std::array<std::string_view, 1> wanted{std::string_view(fonts)};
    std::array<std::filesystem::path, veyra::kFontDiscoveryCapacity> found{};
    const auto result = veyra::discover_fonts_in(wanted, found);
    require(result.count == 2, "testdata scan finds both sample fonts");
    const std::string theme = read_file(VEYRA_APP_EXAMPLE_THEME);
    require(
        harness.model.open(
            std::span<const std::filesystem::path>(found.data(), result.count),
            veyra::AppFontSource::testdata,
            theme
        ),
        "bundled theme file parses"
    );
    require(
        harness.model.font_source() == veyra::AppFontSource::testdata,
        "replay records the testdata font path"
    );
    require(harness.model.font_count() == 2, "replay records both discovered fonts");
}

void check_focus_order_wraps() {
    auto harness = Harness{};
    open_with_testdata(harness);
    require(harness.model.focused() == 1, "session starts on the list");

    constexpr std::uint32_t shift = veyra::modifier_flag(veyra::ShortcutModifier::shift);
    harness.send(make_key(veyra::kAppKeyTab));
    require(harness.model.focused() == 2, "tab steps list to field");
    harness.send(make_key(veyra::kAppKeyTab));
    require(harness.model.focused() == 3, "tab steps field to button");
    harness.send(make_key(veyra::kAppKeyTab));
    require(harness.model.focused() == 1, "tab wraps button to list");
    harness.send(make_key(veyra::kAppKeyTab, shift));
    require(harness.model.focused() == 3, "shift-tab wraps list back to button");
}

void check_typing_and_ime_land_in_buffer() {
    auto harness = Harness{};
    open_with_testdata(harness);
    harness.send(make_key(veyra::kAppKeyTab));
    require(harness.model.focused() == 2, "field holds focus for typing");

    harness.send(make_key('h'));
    harness.send(make_key('i'));
    require(buffer_text(harness.model) == "hi", "key codes insert into the field buffer");

    harness.send(make_commit("!"));
    require(buffer_text(harness.model) == "hi!", "ime commit inserts at the cursor");

    harness.send(make_editing("pre"));
    harness.send(make_commit("?"));
    require(buffer_text(harness.model) == "hi!?", "commit during preedit lands once");

    harness.send(make_key(veyra::kAppKeyBackspace));
    require(buffer_text(harness.model) == "hi!", "backspace erases one char");
}

void check_dialog_opens_closes_and_restores() {
    auto harness = Harness{};
    open_with_testdata(harness);
    harness.send(make_key(veyra::kAppKeyTab));
    harness.send(make_key(veyra::kAppKeyTab));
    require(harness.model.focused() == 3, "button holds focus before the dialog");

    harness.send(make_key(veyra::kAppKeyEnter));
    require(harness.model.dialog_open(), "enter on the button opens the dialog");
    harness.send(make_key(veyra::kAppKeyTab));
    require(harness.model.focused() == 3, "tab stays trapped while the dialog is open");
    harness.send(make_key(veyra::kAppKeyEscape));
    require(!harness.model.dialog_open(), "escape closes the dialog");
    require(harness.model.focused() == 3, "close restores the opener focus");

    harness.send(make_pointer(240.0F, 76.0F));
    require(harness.model.dialog_open(), "pointer on the button opens the dialog");
    harness.send(make_key(veyra::kAppKeyEscape));
    require(!harness.model.dialog_open(), "escape closes the pointer-opened dialog");
}

void check_empty_dialog_commit_is_noop() {
    auto harness = Harness{};
    open_with_testdata(harness);
    constexpr std::uint32_t shift = veyra::modifier_flag(veyra::ShortcutModifier::shift);
    harness.send(make_key(veyra::kAppKeyTab));
    harness.send(make_key(veyra::kAppKeyTab));
    harness.send(make_key(veyra::kAppKeyEnter));
    require(harness.model.dialog_open(), "dialog opens over the empty buffer");
    require(harness.model.buffer_size() == 0, "buffer starts empty");

    harness.send(make_key(veyra::kAppKeyEnter));
    require(harness.model.dialog_open(), "empty commit keeps the dialog open");
    require(harness.model.submitted().empty(), "empty commit records nothing");
    harness.send(make_key(veyra::kAppKeyEscape));
    require(!harness.model.dialog_open(), "escape closes after the no-op");
    require(harness.model.focused() == 3, "escape restores the button focus");

    harness.send(make_key(veyra::kAppKeyTab, shift));
    harness.send(make_key('o'));
    harness.send(make_key('k'));
    harness.send(make_key(veyra::kAppKeyTab));
    harness.send(make_key(veyra::kAppKeyEnter));
    require(harness.model.dialog_open(), "dialog reopens over the typed text");
    harness.send(make_key(veyra::kAppKeyEnter));
    require(!harness.model.dialog_open(), "commit closes over non-empty buffer");
    require(std::string(harness.model.submitted()) == "ok", "commit records the field text");
    require(harness.model.focused() == 3, "commit restores the opener focus");
}

void check_palette_filters_and_restores() {
    auto harness = Harness{};
    open_with_testdata(harness);
    const std::uint64_t before = harness.model.theme().version();
    constexpr std::uint32_t ctrl = veyra::modifier_flag(veyra::ShortcutModifier::ctrl);

    harness.send(make_key(veyra::kAppKeyTab));
    harness.send(make_key(veyra::kAppKeyTab));
    harness.send(make_key(veyra::kAppKeyP, ctrl));
    require(harness.model.palette_open(), "ctrl-p expands the palette");
    harness.send(make_key('t'));
    harness.send(make_key('h'));
    harness.send(make_key('e'));
    harness.send(make_key(veyra::kAppKeyEnter));
    require(!harness.model.palette_open(), "activate collapses the palette");
    require(
        harness.model.theme().version() != before,
        "the filtered command toggles the file theme"
    );
    require(harness.model.focused() == 3, "palette activate restores the stashed focus");

    harness.send(make_key(veyra::kAppKeyP, ctrl));
    require(harness.model.palette_open(), "palette reopens for the escape leg");
    harness.send(make_key(veyra::kAppKeyEscape));
    require(!harness.model.palette_open(), "escape collapses the palette");
    require(harness.model.focused() == 3, "escape restores the pre-palette focus");
}

void check_empty_palette_commit_is_noop() {
    auto harness = Harness{};
    open_with_testdata(harness);
    constexpr std::uint32_t ctrl = veyra::modifier_flag(veyra::ShortcutModifier::ctrl);
    harness.send(make_key(veyra::kAppKeyP, ctrl));
    require(harness.model.palette_open(), "palette opens for the empty-filter leg");
    harness.send(make_key('z'));
    harness.send(make_key(veyra::kAppKeyEnter));
    require(harness.model.palette_open(), "empty filter commits nothing");
    harness.send(make_key(veyra::kAppKeyEscape));
    require(!harness.model.palette_open(), "escape still collapses after the miss");
}

void check_theme_file_applies_and_rejects() {
    auto harness = Harness{};
    open_with_testdata(harness);
    const std::uint64_t filed = harness.model.theme().version();
    const veyra::Srgba8 filed_primary =
        harness.model.theme().color(veyra::tokens::color_action_primary);

    harness.model.toggle_theme();
    require(
        harness.model.theme().version() != filed,
        "toggling off the overlay changes the version"
    );
    harness.model.toggle_theme();
    require(
        harness.model.theme().color(veyra::tokens::color_action_primary) == filed_primary,
        "toggling back on restores the file color"
    );

    const std::uint64_t settled = harness.model.theme().version();
    require(!harness.model.apply_theme_text("bogus = = =\n"), "garbage theme text fails");
    require(
        harness.model.theme().version() == settled,
        "a rejected file leaves the theme version alone"
    );
    require(
        harness.model.theme().color(veyra::tokens::color_action_primary) == filed_primary,
        "a rejected file keeps the old colors"
    );

    require(
        !harness.model.apply_theme_text("schema: 2\ncolor_action_primary = #ffffff\n"),
        "a future schema fails"
    );
}

void run_full_script(Harness& harness) {
    open_with_testdata(harness);
    constexpr std::uint32_t shift = veyra::modifier_flag(veyra::ShortcutModifier::shift);
    constexpr std::uint32_t ctrl = veyra::modifier_flag(veyra::ShortcutModifier::ctrl);
    harness.send(make_key(veyra::kAppKeyTab));
    harness.send(make_key('h'));
    harness.send(make_key('i'));
    harness.send(make_commit("!"));
    harness.send(make_key(veyra::kAppKeyTab));
    harness.send(make_key(veyra::kAppKeyEnter));
    harness.send(make_key(veyra::kAppKeyEscape));
    harness.send(make_key(veyra::kAppKeyTab, shift));
    harness.send(make_key(veyra::kAppKeyTab));
    harness.send(make_key(veyra::kAppKeyP, ctrl));
    harness.send(make_key('t'));
    harness.send(make_key(veyra::kAppKeyEnter));
}

void check_determinism_pins_pixels() {
    auto first = Harness{};
    run_full_script(first);
    const std::uint64_t first_hash = first.checksum();

    auto second = Harness{};
    run_full_script(second);
    const std::uint64_t second_hash = second.checksum();

    require(first_hash == second_hash, "the same script renders identical pixels");
    require(buffer_text(first.model) == "hi!", "replay keeps the typed text");

    const auto& pixels = Harness::backing();
    const auto button = pixels[76 * veyra::kAppWidth + 240];
    const veyra::Srgba8 primary = first.model.theme().color(veyra::tokens::color_action_primary);
    require(
        button.red == primary.red && button.green == primary.green && button.blue == primary.blue,
        "the button pixel shows the theme primary"
    );
}

void check_run_template_quits_on_close() {
    auto harness = Harness{};
    open_with_testdata(harness);
    require(harness.window.push_event(make_key('x')), "key scripts");
    require(harness.window.push_event(make_close()), "close scripts");
    auto& pixels = Harness::backing();
    const auto outcome = veyra::run_app(
        harness.window,
        harness.model,
        harness.scheduler,
        std::span<veyra::SoftwarePixel>(pixels),
        nullptr,
        0
    );
    require(outcome == veyra::LoopResult::quit, "close request quits the templated loop");
    require(harness.model.quit_requested(), "quit latches in the model");
    require(veyra::app_checksum(pixels) != 0, "the templated run leaves a rendered frame behind");
}

void check_steady_loop_allocates_nothing() {
    auto harness = Harness{};
    run_full_script(harness);
    auto& pixels = Harness::backing();
    auto allocations = std::size_t{0};
    {
        auto measure = Measure{};
        for (std::size_t repeat = 0; repeat < 64; ++repeat) {
            auto idle = veyra::WindowEvent{};
            idle.kind = veyra::WindowEventKind::pointer_move;
            harness.model.handle_event(idle);
            require(
                veyra::render_app(harness.model, pixels, veyra::kAppWidth, veyra::kAppHeight) ==
                    veyra::SoftwareRenderResult::complete,
                "steady frame renders"
            );
        }
        allocations = measure.count();
    }
    require(allocations == 0, "steady handle plus render allocates nothing");
}

void check_missing_fonts_render_textless() {
    auto harness = Harness{};
    const std::string theme = read_file(VEYRA_APP_EXAMPLE_THEME);
    require(
        harness.model
            .open(std::span<const std::filesystem::path>{}, veyra::AppFontSource::none, theme),
        "the app opens with no fonts"
    );
    require(
        harness.model.font_source() == veyra::AppFontSource::none,
        "empty discovery records no source"
    );
    harness.send(make_key(veyra::kAppKeyTab));
    harness.send(make_key('A'));
    require(buffer_text(harness.model) == "A", "typing works without fonts");

    const std::uint64_t textless = harness.checksum();

    const veyra::TextFieldAppearance look = veyra::textfield_appearance(
        harness.model.theme(),
        veyra::EditableTextState{.focused = true}
    );
    const std::size_t tick_x = static_cast<std::size_t>(232.0F + look.horizontal_padding.value);
    const auto bare = Harness::backing()[30 * veyra::kAppWidth + tick_x];
    require(
        bare.red == look.box_fill.red && bare.green == look.box_fill.green &&
            bare.blue == look.box_fill.blue,
        "the fontless field keeps the bare box fill where a tick would sit"
    );

    auto with_fonts = Harness{};
    open_with_testdata(with_fonts);
    with_fonts.send(make_key(veyra::kAppKeyTab));
    with_fonts.send(make_key('A'));
    const std::uint64_t ticked = with_fonts.checksum();
    require(textless != ticked, "font presence changes the field pixels");
}

}

int main() {
    try {
        check_focus_order_wraps();
        check_typing_and_ime_land_in_buffer();
        check_dialog_opens_closes_and_restores();
        check_empty_dialog_commit_is_noop();
        check_palette_filters_and_restores();
        check_empty_palette_commit_is_noop();
        check_theme_file_applies_and_rejects();
        check_determinism_pins_pixels();
        check_run_template_quits_on_close();
        check_steady_loop_allocates_nothing();
        check_missing_fonts_render_textless();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
