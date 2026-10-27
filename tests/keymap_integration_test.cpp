#include "veyra/behaviors/navigation/command_palette.hpp"
#include "veyra/behaviors/navigation/focus_chain.hpp"
#include "veyra/core/shortcut_map.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

constexpr std::uint32_t kKeyTab = 9;
constexpr std::uint32_t kKeyEnter = 13;
constexpr std::uint32_t kKeyEscape = 27;
constexpr std::uint32_t kKeyP = 112;
constexpr std::uint32_t kCtrl = veyra::modifier_flag(veyra::ShortcutModifier::ctrl);
constexpr std::uint32_t kShift = veyra::modifier_flag(veyra::ShortcutModifier::shift);

constexpr std::uint64_t kCmdNext = 1;
constexpr std::uint64_t kCmdPrev = 2;
constexpr std::uint64_t kCmdPalette = 3;
constexpr std::uint64_t kCmdActivate = 4;
constexpr std::uint64_t kCmdClose = 5;

constexpr std::uint64_t kPanelAlpha = 11;
constexpr std::uint64_t kPanelBeta = 12;
constexpr std::uint64_t kPanelGamma = 13;
constexpr std::uint64_t kPanelDelta = 14;

constexpr std::uint64_t kPaletteAlpha = 301;
constexpr std::uint64_t kPaletteBeta = 302;
constexpr std::uint64_t kPaletteDelta = 303;

void require(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

std::span<const char> chars_of(std::string_view text) {
    return {text.data(), text.size()};
}

struct KeymapSession {
    veyra::FocusChain chain;
    veyra::ShortcutMap keys;
    veyra::CommandPalette palette;
    std::uint64_t saved_focus{};
};

void seed_chain(KeymapSession& session) {
    require(session.chain.register_item(kPanelAlpha), "alpha panel registers");
    require(session.chain.register_item(kPanelBeta), "beta panel registers");
    require(session.chain.register_item(kPanelGamma, false), "gamma registers disabled");
    require(session.chain.register_item(kPanelDelta), "delta panel registers");
    require(session.chain.focus_first(), "the session starts parked on alpha");
}

void seed_chords(KeymapSession& session) {
    require(
        session.keys.bind({kKeyTab, 0}, kCmdNext).result == veyra::ShortcutResult::bound,
        "bare tab binds the next command"
    );
    require(
        session.keys.bind({kKeyTab, kShift}, kCmdPrev).result == veyra::ShortcutResult::bound,
        "shift-tab binds the previous command"
    );
    require(
        session.keys.bind({kKeyP, kCtrl}, kCmdPalette).result == veyra::ShortcutResult::bound,
        "ctrl-p binds the palette command"
    );
    require(
        session.keys.bind({kKeyEnter, 0}, kCmdActivate).result == veyra::ShortcutResult::bound,
        "bare enter binds the activate command"
    );
    require(
        session.keys.bind({kKeyEscape, 0}, kCmdClose).result == veyra::ShortcutResult::bound,
        "bare escape binds the close command"
    );
}

void seed_palette(KeymapSession& session) {
    const std::array<veyra::PaletteItem, 3> items{{
        {.id = kPaletteAlpha},
        {.id = kPaletteBeta},
        {.id = kPaletteDelta},
    }};
    const std::array<std::string_view, 3> titles{{"Alpha panel", "Beta panel", "Delta panel"}};
    const std::array<std::string_view, 3> keywords{{"", "", ""}};

    require(
        session.palette.set_items(items) == veyra::PaletteResult::changed,
        "palette commands register"
    );
    require(
        session.palette.apply_filter(titles, keywords) == veyra::PaletteResult::changed,
        "the empty query parks on the first palette row"
    );
    require(session.palette.activate() == kPaletteAlpha, "the parked row commits");
}

std::uint64_t run_focus_command(KeymapSession& session, std::uint64_t command) {
    switch (command) {
    case kCmdNext:
        session.chain.focus_next();
        return session.chain.focused();
    case kCmdPrev:
        session.chain.focus_prev();
        return session.chain.focused();
    case kPaletteBeta:
        session.chain.focus(kPanelBeta);
        return session.chain.focused();
    default:
        return session.chain.focused();
    }
}

void check_chords_dispatch_to_commands() {
    auto session = KeymapSession{};
    seed_chain(session);
    seed_chords(session);

    require(
        session.keys.dispatch({kKeyTab, 0}).command == kCmdNext,
        "bare tab dispatches the next command"
    );
    require(
        session.keys.dispatch({kKeyTab, kShift}).command == kCmdPrev,
        "shift-tab dispatches the previous command"
    );
    require(
        session.keys.dispatch({kKeyP, kCtrl}).command == kCmdPalette,
        "ctrl-p dispatches the palette command"
    );
    require(
        session.keys.dispatch({kKeyEscape, 0}).command == kCmdClose,
        "bare escape dispatches the close command"
    );

    const veyra::ShortcutHit miss = session.keys.dispatch({kKeyTab, kCtrl});
    require(!miss.has && miss.command == 0, "an unbound modifier mix misses");
}

void check_conflict_keeps_old_binding() {
    auto session = KeymapSession{};
    seed_chain(session);
    seed_chords(session);

    const veyra::ShortcutBind clash = session.keys.bind({kKeyTab, 0}, 999);
    require(clash.result == veyra::ShortcutResult::conflict, "a taken chord reports conflict");
    require(clash.occupier == kCmdNext, "the conflict names the next command");
    require(
        session.keys.dispatch({kKeyTab, 0}).command == kCmdNext,
        "the conflict leaves the old binding firing"
    );
}

void check_tab_walks_mixed_chain() {
    auto session = KeymapSession{};
    seed_chain(session);
    seed_chords(session);
    require(session.chain.focused() == kPanelAlpha, "the session starts on alpha");

    require(
        run_focus_command(session, session.keys.dispatch({kKeyTab, 0}).command) == kPanelBeta,
        "tab steps from alpha to beta"
    );
    require(
        run_focus_command(session, session.keys.dispatch({kKeyTab, 0}).command) == kPanelDelta,
        "tab skips disabled gamma for delta"
    );
    require(
        run_focus_command(session, session.keys.dispatch({kKeyTab, 0}).command) == kPanelAlpha,
        "tab wraps from delta to alpha"
    );
    require(
        run_focus_command(session, session.keys.dispatch({kKeyTab, kShift}).command) == kPanelDelta,
        "shift-tab wraps back to delta"
    );
}

void check_palette_opens_with_saved_focus() {
    auto session = KeymapSession{};
    seed_chain(session);
    seed_chords(session);
    seed_palette(session);
    session.chain.focus(kPanelDelta);

    require(
        session.keys.dispatch({kKeyP, kCtrl}).command == kCmdPalette,
        "ctrl-p dispatches the palette command"
    );
    session.saved_focus = session.chain.focused();
    require(session.palette.set_expanded(true), "the palette command expands the palette");
    require(session.saved_focus == kPanelDelta, "opening stashes the pre-palette focus");
    require(session.palette.expanded(), "the palette stays open for typing");
}

void check_palette_filter_activate_runs_focus_command() {
    auto session = KeymapSession{};
    seed_chain(session);
    seed_chords(session);
    seed_palette(session);
    session.chain.focus(kPanelDelta);
    session.saved_focus = session.chain.focused();
    session.palette.set_expanded(true);

    const std::string query = "Beta";
    const std::array<std::string_view, 3> titles{{"Alpha panel", "Beta panel", "Delta panel"}};
    const std::array<std::string_view, 3> keywords{{"", "", ""}};

    require(
        session.palette.set_query(chars_of(query)) == veyra::PaletteResult::changed,
        "typing narrows the palette"
    );
    require(
        session.palette.apply_filter(titles, keywords) == veyra::PaletteResult::changed,
        "the filter parks on the beta row"
    );

    std::array<std::uint64_t, 4> matches{};
    require(
        session.palette.copy_matches(matches) == 1 && matches[0] == kPaletteBeta,
        "only the beta command survives the filter"
    );
    require(
        session.keys.dispatch({kKeyEnter, 0}).command == kCmdActivate,
        "enter dispatches the activate command"
    );

    const std::uint64_t picked = session.palette.activate();
    require(picked == kPaletteBeta, "activate commits the filtered command");
    require(
        run_focus_command(session, picked) == kPanelBeta,
        "the filtered command moves chain focus to beta"
    );
}

void check_empty_filter_commits_nothing() {
    auto session = KeymapSession{};
    seed_chain(session);
    seed_chords(session);
    seed_palette(session);
    session.chain.focus(kPanelBeta);

    const std::string query = "zzz";
    const std::array<std::string_view, 3> titles{{"Alpha panel", "Beta panel", "Delta panel"}};
    const std::array<std::string_view, 3> keywords{{"", "", ""}};

    session.palette.set_expanded(true);
    session.palette.set_query(chars_of(query));
    require(
        session.palette.apply_filter(titles, keywords) == veyra::PaletteResult::changed,
        "a hopeless query empties the window"
    );
    require(session.palette.activate() == 0, "an empty window commits nothing");
    require(session.chain.focused() == kPanelBeta, "a miss leaves chain focus alone");
}

void check_escape_restores_pre_palette_focus() {
    auto session = KeymapSession{};
    seed_chain(session);
    seed_chords(session);
    seed_palette(session);
    session.chain.focus(kPanelDelta);
    session.saved_focus = session.chain.focused();
    session.palette.set_expanded(true);
    session.chain.focus(kPanelBeta);
    require(session.chain.focused() == kPanelBeta, "the palette detour moves focus");

    require(
        session.keys.dispatch({kKeyEscape, 0}).command == kCmdClose,
        "escape dispatches the close command"
    );
    require(session.palette.set_expanded(false), "the close command collapses the palette");
    require(session.chain.focus(session.saved_focus), "closing refocuses the stashed id");
    require(session.chain.focused() == kPanelDelta, "escape restores the pre-palette focus");
}

}

int main() {
    try {
        check_chords_dispatch_to_commands();
        check_conflict_keeps_old_binding();
        check_tab_walks_mixed_chain();
        check_palette_opens_with_saved_focus();
        check_palette_filter_activate_runs_focus_command();
        check_empty_filter_commits_nothing();
        check_escape_restores_pre_palette_focus();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
