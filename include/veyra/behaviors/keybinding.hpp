#ifndef VEYRA_BEHAVIORS_KEYBINDING_HPP
#define VEYRA_BEHAVIORS_KEYBINDING_HPP

#include "veyra/behaviors/combobox.hpp"
#include "veyra/core/shortcut_map.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace veyra {

enum class KeybindingResult : std::uint8_t {
    ignored,
    handled,
    changed,
    opened,
    overflow,
    closed,
    dismissed,
};

enum class KeybindingFeedResult : std::uint8_t {
    ignored,
    bound,
    handled,
    conflict,
    invalid,
    overflow,
};

struct KeybindingFeed {
    KeybindingFeedResult result{KeybindingFeedResult::ignored};
    std::uint64_t occupier{};
};

struct KeybindingClear {
    KeybindingResult result{KeybindingResult::ignored};
    std::size_t removed{};
};

class KeybindingEditor {
  public:
    KeybindingEditor() noexcept = default;

    bool capturing() const noexcept;
    std::uint64_t capture_id() const noexcept;
    const ComboBoxController& list() const noexcept;
    bool is_open() const noexcept;
    bool disabled() const noexcept;

    bool set_disabled(bool disabled) noexcept;

    KeybindingResult
    open(std::span<const ComboOption> actions, std::string_view query = {}) noexcept;
    KeybindingResult
    apply_filter(std::string_view query, std::span<const ComboOption> actions) noexcept;
    KeybindingResult move_active(ComboMove direction) noexcept;
    ComboCommit commit_active() noexcept;
    ComboCommit commit_id(std::uint64_t action_id, std::span<const ComboOption> actions) noexcept;
    ComboCommit commit_text(std::string_view query) noexcept;
    KeybindingResult start_capture(std::uint64_t action_id) noexcept;
    KeybindingFeed feed_chord(ShortcutChord chord, ShortcutMap& map) noexcept;
    KeybindingResult cancel_capture() noexcept;
    KeybindingClear clear_binding(std::uint64_t action_id, ShortcutMap& map) noexcept;
    KeybindingResult close() noexcept;
    KeybindingResult dismiss() noexcept;

  private:
    static KeybindingResult to_result(ComboResult result) noexcept;

    ComboBoxController box_{};
    bool capturing_{};
    std::uint64_t capture_{};
};

}

#endif
