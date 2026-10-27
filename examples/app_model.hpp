#ifndef VEYRA_EXAMPLES_APP_MODEL_HPP
#define VEYRA_EXAMPLES_APP_MODEL_HPP

#include "veyra/behaviors/navigation/command_palette.hpp"
#include "veyra/behaviors/input/editable_text.hpp"
#include "veyra/behaviors/navigation/focus_chain.hpp"
#include "veyra/behaviors/overlay/overlay.hpp"
#include "veyra/core/shortcut_map.hpp"
#include "veyra/platform/text_input.hpp"
#include "veyra/platform/window.hpp"
#include "veyra/style/theme.hpp"
#include "veyra/style/theme_file.hpp"
#include "veyra/text/editing/text_editor_buffer.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string_view>

namespace veyra {

inline constexpr std::size_t kAppWidth = 480;
inline constexpr std::size_t kAppHeight = 320;
inline constexpr std::size_t kAppRowCount = 20;
inline constexpr float kAppRowHeight = 12.0F;

inline constexpr std::uint32_t kAppKeyTab = 9;
inline constexpr std::uint32_t kAppKeyEnter = 13;
inline constexpr std::uint32_t kAppKeyEscape = 27;
inline constexpr std::uint32_t kAppKeyBackspace = 8;
inline constexpr std::uint32_t kAppKeyUp = 0x40000052U;
inline constexpr std::uint32_t kAppKeyDown = 0x40000054U;
inline constexpr std::uint32_t kAppKeyP = 112;

inline constexpr std::uint64_t kAppFocusList = 1;
inline constexpr std::uint64_t kAppFocusField = 2;
inline constexpr std::uint64_t kAppFocusButton = 3;

inline constexpr std::uint64_t kAppCmdNext = 1;
inline constexpr std::uint64_t kAppCmdPrev = 2;
inline constexpr std::uint64_t kAppCmdPalette = 3;
inline constexpr std::uint64_t kAppCmdActivate = 4;
inline constexpr std::uint64_t kAppCmdClose = 5;

inline constexpr std::uint64_t kAppPaletteOpenDialog = 101;
inline constexpr std::uint64_t kAppPaletteToggleTheme = 102;

enum class AppFontSource : std::uint8_t {
    none,
    system,
    testdata,
};

class AppModel {
  public:
    AppModel() = default;

    bool open(
        std::span<const std::filesystem::path> fonts,
        AppFontSource source,
        std::string_view theme_text
    );

    bool apply_theme_text(std::string_view theme_text);
    void toggle_theme();

    bool handle_event(const WindowEvent& event) noexcept;

    bool quit_requested() const noexcept;
    std::uint64_t focused() const noexcept;
    bool dialog_open() const noexcept;
    bool palette_open() const noexcept;
    AppFontSource font_source() const noexcept;
    std::size_t font_count() const noexcept;
    bool theme_ok() const noexcept;
    const Theme& theme() const noexcept;
    std::size_t selected_row() const noexcept;
    std::size_t current_row() const noexcept;
    std::string_view submitted() const noexcept;
    std::size_t buffer_size() const noexcept;
    TextEditResult copy_buffer(std::span<char> output, std::size_t& length) const noexcept;

  private:
    static constexpr std::size_t kFieldCapacity = 128;
    static constexpr std::size_t kSubmitCapacity = 128;
    static constexpr std::size_t kQueryCapacity = 48;
    static constexpr std::size_t kMatchCapacity = 8;

    static NodeId node_for(std::uint64_t id) noexcept;

    bool handle_key(const WindowKey& key) noexcept;
    bool handle_commit(const WindowTextCommit& commit) noexcept;
    bool handle_editing(const WindowTextEditing& editing) noexcept;
    bool handle_pointer(const WindowPointer& pointer) noexcept;
    bool run_command(std::uint64_t command) noexcept;
    bool run_palette(std::uint64_t item) noexcept;
    bool open_dialog() noexcept;
    bool close_dialog() noexcept;
    bool commit_dialog() noexcept;
    bool open_palette() noexcept;
    bool close_palette(bool restore) noexcept;
    void move_list(int delta) noexcept;
    void insert_char(char value) noexcept;
    void palette_type(char value) noexcept;
    void palette_erase() noexcept;

    std::array<char, kFieldCapacity> field_storage_{};
    TextEditorBuffer field_buffer_{std::span<char>{field_storage_}, ""};
    EditableTextController field_{field_buffer_};
    std::array<char, 64> ime_scratch_{};
    std::array<char, kSubmitCapacity> submitted_storage_{};
    std::size_t submitted_length_{};
    std::array<char, kQueryCapacity> query_storage_{};
    std::size_t query_length_{};
    Theme base_{Theme::dark()};
    Theme theme_{Theme::dark()};
    Theme file_overlay_{};
    bool overlay_on_{};
    bool theme_ok_{};
    FocusChain chain_{};
    ShortcutMap keys_{};
    CommandPalette palette_{};
    OverlayController overlays_{};
    std::uint64_t palette_saved_{};
    std::size_t selected_{};
    std::size_t current_{};
    bool dialog_open_{};
    std::uint64_t dialog_saved_{};
    bool button_pressed_{};
    AppFontSource font_source_{AppFontSource::none};
    std::size_t font_count_{};
    bool quit_{};

    friend SoftwareRenderResult render_app(
        const AppModel& model,
        std::span<SoftwarePixel> pixels,
        std::size_t width,
        std::size_t height
    ) noexcept;
};

inline NodeId AppModel::node_for(std::uint64_t id) noexcept {
    return NodeId{.index = static_cast<std::uint32_t>(id), .generation = 1};
}

inline bool AppModel::open(
    std::span<const std::filesystem::path> fonts,
    AppFontSource source,
    std::string_view theme_text
) {
    font_count_ = fonts.size();
    font_source_ = font_count_ == 0 ? AppFontSource::none : source;

    chain_.register_item(kAppFocusList);
    chain_.register_item(kAppFocusField);
    chain_.register_item(kAppFocusButton);
    chain_.focus_first();

    keys_.bind({kAppKeyTab, 0}, kAppCmdNext);
    keys_.bind({kAppKeyTab, modifier_flag(ShortcutModifier::shift)}, kAppCmdPrev);
    keys_.bind({kAppKeyP, modifier_flag(ShortcutModifier::ctrl)}, kAppCmdPalette);
    keys_.bind({kAppKeyEnter, 0}, kAppCmdActivate);
    keys_.bind({kAppKeyEscape, 0}, kAppCmdClose);

    constexpr std::array<PaletteItem, 2> items{{
        {.id = kAppPaletteOpenDialog},
        {.id = kAppPaletteToggleTheme},
    }};
    palette_.set_items(items);

    theme_ok_ = apply_theme_text(theme_text);
    overlay_on_ = theme_ok_;
    return theme_ok_;
}

inline bool AppModel::apply_theme_text(std::string_view theme_text) {
    auto overlay = Theme{};
    auto error = ThemeFileError{};
    const std::span<const char> text(theme_text.data(), theme_text.size());
    if (parse_theme_file(text, overlay, error) != ThemeFileResult::ok) {
        return false;
    }
    file_overlay_ = overlay;
    theme_ = base_;
    if (overlay_on_ || !theme_ok_) {
        theme_.apply(file_overlay_);
        overlay_on_ = true;
    }
    return true;
}

inline void AppModel::toggle_theme() {
    overlay_on_ = !overlay_on_;
    theme_ = base_;
    if (overlay_on_) {
        theme_.apply(file_overlay_);
    }
}

inline bool AppModel::quit_requested() const noexcept {
    return quit_;
}

inline std::uint64_t AppModel::focused() const noexcept {
    return chain_.focused();
}

inline bool AppModel::dialog_open() const noexcept {
    return dialog_open_;
}

inline bool AppModel::palette_open() const noexcept {
    return palette_.expanded();
}

inline AppFontSource AppModel::font_source() const noexcept {
    return font_source_;
}

inline std::size_t AppModel::font_count() const noexcept {
    return font_count_;
}

inline bool AppModel::theme_ok() const noexcept {
    return theme_ok_;
}

inline const Theme& AppModel::theme() const noexcept {
    return theme_;
}

inline std::size_t AppModel::selected_row() const noexcept {
    return selected_;
}

inline std::size_t AppModel::current_row() const noexcept {
    return current_;
}

inline std::string_view AppModel::submitted() const noexcept {
    return {submitted_storage_.data(), submitted_length_};
}

inline std::size_t AppModel::buffer_size() const noexcept {
    return field_buffer_.size();
}

inline TextEditResult
AppModel::copy_buffer(std::span<char> output, std::size_t& length) const noexcept {
    return field_buffer_.copy_text(output, length);
}

inline void AppModel::move_list(int delta) noexcept {
    auto next = static_cast<int>(current_) + delta;
    if (next < 0) {
        next = 0;
    }
    if (next >= static_cast<int>(kAppRowCount)) {
        next = static_cast<int>(kAppRowCount) - 1;
    }
    current_ = static_cast<std::size_t>(next);
    selected_ = current_;
}

inline void AppModel::insert_char(char value) noexcept {
    const char text[1] = {value};
    field_.insert(std::string_view(text, 1));
}

inline void AppModel::palette_type(char value) noexcept {
    if (query_length_ >= kQueryCapacity) {
        return;
    }
    query_storage_[query_length_] = value;
    ++query_length_;
    palette_.set_query(std::span<const char>(query_storage_.data(), query_length_));
    constexpr std::array<std::string_view, 2> titles{{"Open dialog", "Toggle theme"}};
    constexpr std::array<std::string_view, 2> keywords{{"dialog open", "theme dark light"}};
    palette_.apply_filter(titles, keywords);
}

inline void AppModel::palette_erase() noexcept {
    if (query_length_ == 0) {
        return;
    }
    --query_length_;
    palette_.set_query(std::span<const char>(query_storage_.data(), query_length_));
    constexpr std::array<std::string_view, 2> titles{{"Open dialog", "Toggle theme"}};
    constexpr std::array<std::string_view, 2> keywords{{"dialog open", "theme dark light"}};
    palette_.apply_filter(titles, keywords);
}

inline bool AppModel::open_dialog() noexcept {
    if (dialog_open_) {
        return false;
    }
    if (palette_.expanded()) {
        close_palette(true);
    }
    dialog_saved_ = chain_.focused();
    const auto opened = overlays_.open({
        .dialog = node_for(100),
        .restore_focus = node_for(dialog_saved_),
    });
    if (opened != OverlayResult::opened) {
        return false;
    }
    dialog_open_ = true;
    return true;
}

inline bool AppModel::close_dialog() noexcept {
    if (!dialog_open_) {
        return false;
    }
    const auto closed = overlays_.close_top();
    dialog_open_ = false;
    chain_.focus(closed.restore_focus.index);
    return true;
}

inline bool AppModel::commit_dialog() noexcept {
    if (field_buffer_.size() == 0) {
        return false;
    }
    std::array<char, kSubmitCapacity> copy{};
    std::size_t length = 0;
    if (field_buffer_.copy_text(copy, length) != TextEditResult::complete) {
        return false;
    }
    if (length > kSubmitCapacity) {
        length = kSubmitCapacity;
    }
    for (std::size_t index = 0; index < length; ++index) {
        submitted_storage_[index] = copy[index];
    }
    submitted_length_ = length;
    close_dialog();
    return true;
}

inline bool AppModel::open_palette() noexcept {
    if (dialog_open_ || palette_.expanded()) {
        return false;
    }
    palette_saved_ = chain_.focused();
    query_length_ = 0;
    palette_.set_query(std::span<const char>{});
    constexpr std::array<std::string_view, 2> titles{{"Open dialog", "Toggle theme"}};
    constexpr std::array<std::string_view, 2> keywords{{"dialog open", "theme dark light"}};
    palette_.apply_filter(titles, keywords);
    palette_.set_expanded(true);
    return true;
}

inline bool AppModel::close_palette(bool restore) noexcept {
    if (!palette_.expanded()) {
        return false;
    }
    palette_.set_expanded(false);
    if (restore) {
        chain_.focus(palette_saved_);
    }
    return true;
}

inline bool AppModel::run_palette(std::uint64_t item) noexcept {
    if (item == kAppPaletteOpenDialog) {
        close_palette(true);
        open_dialog();
        return true;
    }
    if (item == kAppPaletteToggleTheme) {
        toggle_theme();
        close_palette(true);
        return true;
    }
    return false;
}

inline bool AppModel::run_command(std::uint64_t command) noexcept {
    switch (command) {
    case kAppCmdNext:
        chain_.focus_next();
        return true;
    case kAppCmdPrev:
        chain_.focus_prev();
        return true;
    case kAppCmdPalette:
        open_palette();
        return true;
    case kAppCmdActivate:
        if (chain_.focused() == kAppFocusButton) {
            open_dialog();
            return true;
        }
        return false;
    case kAppCmdClose:
        return false;
    default:
        return false;
    }
}

inline bool AppModel::handle_key(const WindowKey& key) noexcept {
    if (palette_.expanded()) {
        const ShortcutHit hit = keys_.dispatch({key.code, key.modifiers});
        if (hit.has && hit.command == kAppCmdClose && !key.repeat) {
            close_palette(true);
            return true;
        }
        if (hit.has && hit.command == kAppCmdActivate && !key.repeat) {
            run_palette(palette_.activate());
            return true;
        }
        if (key.code == kAppKeyUp && !key.repeat) {
            palette_.move_prev();
            return true;
        }
        if (key.code == kAppKeyDown && !key.repeat) {
            palette_.move_next();
            return true;
        }
        if (key.code == kAppKeyBackspace && key.modifiers == 0) {
            palette_erase();
            return true;
        }
        if (key.code >= 32 && key.code <= 126 && key.modifiers == 0) {
            palette_type(static_cast<char>(key.code));
            return true;
        }
        return false;
    }

    if (dialog_open_) {
        const ShortcutHit hit = keys_.dispatch({key.code, key.modifiers});
        if (hit.has && hit.command == kAppCmdClose && !key.repeat) {
            close_dialog();
            return true;
        }
        if (hit.has && hit.command == kAppCmdActivate && !key.repeat) {
            commit_dialog();
            return true;
        }
        return false;
    }

    const ShortcutHit hit = keys_.dispatch({key.code, key.modifiers});
    if (hit.has && !key.repeat) {
        if (hit.command == kAppCmdClose) {
            return false;
        }
        run_command(hit.command);
        return true;
    }

    if (chain_.focused() == kAppFocusList) {
        if (key.code == kAppKeyUp && key.modifiers == 0 && !key.repeat) {
            move_list(-1);
            return true;
        }
        if (key.code == kAppKeyDown && key.modifiers == 0 && !key.repeat) {
            move_list(1);
            return true;
        }
    }

    if (chain_.focused() == kAppFocusField) {
        if (key.code == kAppKeyBackspace && key.modifiers == 0) {
            field_.erase_backward();
            return true;
        }
        if (key.code >= 32 && key.code <= 126 && key.modifiers == 0) {
            insert_char(static_cast<char>(key.code));
            return true;
        }
    }

    return false;
}

inline bool AppModel::handle_commit(const WindowTextCommit& commit) noexcept {
    if (chain_.focused() != kAppFocusField) {
        return false;
    }
    auto applied = TextCommitApplied{};
    apply_text_commit(field_buffer_, commit, applied);
    return applied.edit == TextEditResult::complete;
}

inline bool AppModel::handle_editing(const WindowTextEditing& editing) noexcept {
    if (chain_.focused() != kAppFocusField) {
        return false;
    }
    auto applied = TextEditingApplied{};
    apply_text_editing(field_buffer_, ime_scratch_, editing, applied);
    return applied.edit == TextEditResult::complete;
}

inline bool AppModel::handle_pointer(const WindowPointer& pointer) noexcept {
    const float x = pointer.position.x;
    const float y = pointer.position.y;

    if (dialog_open_) {
        const bool ok = x >= 110.0F && x < 190.0F && y >= 190.0F && y < 218.0F;
        const bool cancel = x >= 210.0F && x < 290.0F && y >= 190.0F && y < 218.0F;
        if (ok) {
            commit_dialog();
            return true;
        }
        if (cancel) {
            close_dialog();
            return true;
        }
        return true;
    }

    if (palette_.expanded()) {
        const bool panel = x >= 90.0F && x < 390.0F && y >= 100.0F && y < 156.0F;
        if (!panel) {
            close_palette(true);
            return true;
        }
        if (y >= 128.0F) {
            const auto row = static_cast<std::size_t>((y - 128.0F) / 22.0F);
            std::array<std::uint64_t, kMatchCapacity> matches{};
            const std::size_t count = palette_.copy_matches(matches);
            if (row < count) {
                run_palette(matches[row]);
                return true;
            }
        }
        return true;
    }

    button_pressed_ = x >= 232.0F && x < 352.0F && y >= 60.0F && y < 92.0F;
    if (button_pressed_) {
        open_dialog();
        return true;
    }
    if (x >= 232.0F && x < 464.0F && y >= 16.0F && y < 44.0F) {
        chain_.focus(kAppFocusField);
        return true;
    }
    if (x >= 16.0F && x < 216.0F && y >= 16.0F && y < 256.0F) {
        chain_.focus(kAppFocusList);
        const auto row = static_cast<std::size_t>((y - 16.0F) / kAppRowHeight);
        if (row < kAppRowCount) {
            current_ = row;
            selected_ = row;
        }
        return true;
    }
    return false;
}

inline bool AppModel::handle_event(const WindowEvent& event) noexcept {
    switch (event.kind) {
    case WindowEventKind::close_request:
        quit_ = true;
        return true;
    case WindowEventKind::key_down:
        handle_key(event.data.key);
        return false;
    case WindowEventKind::key_up:
        if (event.data.key.code == kAppKeyTab || event.data.key.code == kAppKeyEnter ||
            event.data.key.code == kAppKeyEscape) {
            button_pressed_ = false;
        }
        return false;
    case WindowEventKind::text_commit:
        handle_commit(event.data.commit);
        return false;
    case WindowEventKind::text_editing:
        handle_editing(event.data.editing);
        return false;
    case WindowEventKind::pointer_down:
        handle_pointer(event.data.pointer);
        return false;
    case WindowEventKind::pointer_up:
        button_pressed_ = false;
        return false;
    default:
        return false;
    }
}

}

#endif
