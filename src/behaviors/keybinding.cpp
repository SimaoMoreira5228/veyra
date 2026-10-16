#include "veyra/behaviors/keybinding.hpp"

namespace veyra {

bool KeybindingEditor::capturing() const noexcept {
    return capturing_;
}

std::uint64_t KeybindingEditor::capture_id() const noexcept {
    return capture_;
}

const ComboBoxController& KeybindingEditor::list() const noexcept {
    return box_;
}

bool KeybindingEditor::is_open() const noexcept {
    return box_.is_open();
}

bool KeybindingEditor::disabled() const noexcept {
    return box_.disabled();
}

bool KeybindingEditor::set_disabled(bool disabled) noexcept {
    const bool changed = box_.set_disabled(disabled);

    if (!changed) {
        return false;
    }

    if (disabled) {
        capturing_ = false;
        capture_ = 0;
    }

    return true;
}

KeybindingResult
KeybindingEditor::open(std::span<const ComboOption> actions, std::string_view query) noexcept {
    if (box_.disabled()) {
        return KeybindingResult::ignored;
    }

    capturing_ = false;
    capture_ = 0;

    return to_result(box_.open(actions, query));
}

KeybindingResult KeybindingEditor::apply_filter(
    std::string_view query,
    std::span<const ComboOption> actions
) noexcept {
    if (box_.disabled()) {
        return KeybindingResult::ignored;
    }

    return to_result(box_.apply_filter(query, actions));
}

KeybindingResult KeybindingEditor::move_active(ComboMove direction) noexcept {
    if (box_.disabled()) {
        return KeybindingResult::ignored;
    }

    if (capturing_) {
        return KeybindingResult::ignored;
    }

    return to_result(box_.move_active(direction));
}

ComboCommit KeybindingEditor::commit_active() noexcept {
    if (box_.disabled() || capturing_) {
        return {};
    }

    return box_.commit_active();
}

ComboCommit KeybindingEditor::commit_id(
    std::uint64_t action_id,
    std::span<const ComboOption> actions
) noexcept {
    if (box_.disabled() || capturing_) {
        return {};
    }

    return box_.commit_id(action_id, actions);
}

ComboCommit KeybindingEditor::commit_text(std::string_view query) noexcept {
    if (box_.disabled() || capturing_) {
        return {};
    }

    return box_.commit_text(query);
}

KeybindingResult KeybindingEditor::start_capture(std::uint64_t action_id) noexcept {
    if (box_.disabled()) {
        return KeybindingResult::ignored;
    }

    if (!box_.is_open()) {
        return KeybindingResult::ignored;
    }

    if (capturing_) {
        return KeybindingResult::ignored;
    }

    if (action_id == 0) {
        return KeybindingResult::ignored;
    }

    if (!box_.contains(action_id)) {
        return KeybindingResult::ignored;
    }

    capturing_ = true;
    capture_ = action_id;

    return KeybindingResult::changed;
}

KeybindingFeed KeybindingEditor::feed_chord(ShortcutChord chord, ShortcutMap& map) noexcept {
    if (box_.disabled()) {
        return {};
    }

    if (!capturing_) {
        return {};
    }

    const std::uint64_t target = capture_;
    capturing_ = false;
    capture_ = 0;

    const ShortcutBind bound = map.bind(chord, target);

    if (bound.result == ShortcutResult::bound) {
        return {
            .result = KeybindingFeedResult::bound,
        };
    }

    if (bound.result == ShortcutResult::handled) {
        return {
            .result = KeybindingFeedResult::handled,
        };
    }

    if (bound.result == ShortcutResult::conflict) {
        return {
            .result = KeybindingFeedResult::conflict,
            .occupier = bound.occupier,
        };
    }

    if (bound.result == ShortcutResult::overflow) {
        return {
            .result = KeybindingFeedResult::overflow,
        };
    }

    return {
        .result = KeybindingFeedResult::invalid,
    };
}

KeybindingResult KeybindingEditor::cancel_capture() noexcept {
    if (box_.disabled()) {
        return KeybindingResult::ignored;
    }

    if (!capturing_) {
        return KeybindingResult::handled;
    }

    capturing_ = false;
    capture_ = 0;

    return KeybindingResult::changed;
}

KeybindingClear
KeybindingEditor::clear_binding(std::uint64_t action_id, ShortcutMap& map) noexcept {
    if (box_.disabled()) {
        return {};
    }

    if (action_id == 0) {
        return {};
    }

    const std::size_t removed = map.unbind_command(action_id);

    if (removed == 0) {
        return {
            .result = KeybindingResult::handled,
        };
    }

    return {
        .result = KeybindingResult::changed,
        .removed = removed,
    };
}

KeybindingResult KeybindingEditor::close() noexcept {
    if (box_.disabled()) {
        return KeybindingResult::ignored;
    }

    capturing_ = false;
    capture_ = 0;

    return to_result(box_.close());
}

KeybindingResult KeybindingEditor::dismiss() noexcept {
    if (box_.disabled()) {
        return KeybindingResult::ignored;
    }

    capturing_ = false;
    capture_ = 0;

    return to_result(box_.dismiss());
}

KeybindingResult KeybindingEditor::to_result(ComboResult result) noexcept {
    switch (result) {
    case ComboResult::ignored:
        return KeybindingResult::ignored;
    case ComboResult::handled:
        return KeybindingResult::handled;
    case ComboResult::changed:
        return KeybindingResult::changed;
    case ComboResult::opened:
        return KeybindingResult::opened;
    case ComboResult::overflow:
        return KeybindingResult::overflow;
    case ComboResult::closed:
        return KeybindingResult::closed;
    case ComboResult::dismissed:
        return KeybindingResult::dismissed;
    default:
        return KeybindingResult::handled;
    }
}

}
