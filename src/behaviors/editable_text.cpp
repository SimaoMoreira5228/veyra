#include "veyra/behaviors/editable_text.hpp"

namespace veyra {

EditableTextController::EditableTextController(TextEditorBuffer& buffer) noexcept :
    buffer_(&buffer) {
}

TextEditorBuffer& EditableTextController::buffer() noexcept {
    return *buffer_;
}

EditableTextState EditableTextController::state() const noexcept {
    return {
        .kind = kind_,
        .focused = focused_,
        .disabled = disabled_,
        .readonly = readonly_,
        .invalid = invalid_,
        .reveal_password = reveal_password_,
        .composing = composing(),
    };
}

EditableTextKind EditableTextController::kind() const noexcept {
    return kind_;
}

bool EditableTextController::focused() const noexcept {
    return focused_;
}

bool EditableTextController::disabled() const noexcept {
    return disabled_;
}

bool EditableTextController::readonly() const noexcept {
    return readonly_;
}

bool EditableTextController::invalid() const noexcept {
    return invalid_;
}

bool EditableTextController::reveal_password() const noexcept {
    return reveal_password_;
}

bool EditableTextController::composing() const noexcept {
    return buffer_->has_composition();
}

TextSelection EditableTextController::selection() const noexcept {
    return buffer_->selection();
}

bool EditableTextController::configure(const EditableTextConfig& config) noexcept {
    if (kind_ == config.kind) {
        return false;
    }

    kind_ = config.kind;
    reveal_password_ = false;
    search_pending_ = false;

    return true;
}

bool EditableTextController::set_kind(EditableTextKind kind) noexcept {
    return configure({.kind = kind});
}

bool EditableTextController::set_disabled(bool disabled) noexcept {
    if (disabled_ == disabled) {
        return false;
    }

    disabled_ = disabled;
    if (disabled_) {
        focused_ = false;
        buffer_->cancel_composition();
    }

    return true;
}

bool EditableTextController::set_readonly(bool readonly) noexcept {
    if (readonly_ == readonly) {
        return false;
    }

    readonly_ = readonly;
    if (readonly_) {
        buffer_->cancel_composition();
    }

    return true;
}

bool EditableTextController::set_invalid(bool invalid) noexcept {
    if (invalid_ == invalid) {
        return false;
    }

    invalid_ = invalid;

    return true;
}

bool EditableTextController::set_reveal_password(bool reveal) noexcept {
    if (reveal_password_ == reveal) {
        return false;
    }

    reveal_password_ = reveal;

    return true;
}

EditableTextResult EditableTextController::set_focused(bool focused) noexcept {
    if (focused_ == focused || (focused && disabled_)) {
        return EditableTextResult::ignored;
    }

    focused_ = focused;
    if (!focused_) {
        buffer_->cancel_composition();
    }

    return EditableTextResult::handled;
}

EditableTextEdit EditableTextController::insert(std::string_view text) noexcept {
    if (!mutable_state()) {
        return {};
    }

    const auto before = snapshot();

    return finish(before, buffer_->insert(text), true);
}

EditableTextEdit EditableTextController::paste(std::string_view text) noexcept {
    if (!mutable_state()) {
        return {};
    }

    const auto before = snapshot();

    return finish(before, buffer_->insert(text), true);
}

EditableTextEdit EditableTextController::erase_backward() noexcept {
    if (!mutable_state()) {
        return {};
    }

    const auto before = snapshot();

    return finish(before, buffer_->erase_backward(), true);
}

EditableTextEdit EditableTextController::erase_forward() noexcept {
    if (!mutable_state()) {
        return {};
    }

    const auto before = snapshot();

    return finish(before, buffer_->erase_forward(), true);
}

EditableTextEdit
EditableTextController::set_cursor(std::size_t byte_offset, bool extend_selection) noexcept {
    if (disabled_) {
        return {};
    }

    const auto before = snapshot();

    return finish(before, buffer_->set_cursor(byte_offset, extend_selection), false);
}

EditableTextResult EditableTextController::select_all() noexcept {
    if (disabled_) {
        return EditableTextResult::ignored;
    }

    const auto before = buffer_->selection();
    buffer_->select_all();
    const auto after = buffer_->selection();
    if (before.start == after.start && before.length == after.length) {
        return EditableTextResult::handled;
    }

    return EditableTextResult::changed;
}

EditableTextEdit EditableTextController::clear() noexcept {
    if (!mutable_state()) {
        return {};
    }

    const auto before = snapshot();
    buffer_->select_all();

    return finish(before, buffer_->insert({}), true);
}

TextEditResult
EditableTextController::copy_selection(std::span<char> output, std::size_t& length) const noexcept {
    return buffer_->copy_selection(output, length);
}

EditableTextEdit
EditableTextController::cut_selection(std::span<char> output, std::size_t& length) noexcept {
    if (!mutable_state()) {
        return {};
    }

    const auto copy_result = buffer_->copy_selection(output, length);
    if (copy_result != TextEditResult::complete) {
        return {.result = EditableTextResult::handled, .edit = copy_result};
    }

    if (length == 0) {
        return {.result = EditableTextResult::handled, .edit = TextEditResult::complete};
    }

    const auto before = snapshot();

    return finish(before, buffer_->insert({}), true);
}

EditableTextEdit EditableTextController::update_composition(
    std::span<char> storage,
    std::string_view text,
    std::size_t cursor,
    TextSelection selection
) noexcept {
    if (disabled_ || readonly_ || !focused_) {
        return {};
    }

    const auto before = snapshot();
    const auto edit = buffer_->update_composition(storage, text, cursor, selection);

    return finish(before, edit, false);
}

EditableTextEdit EditableTextController::commit_composition() noexcept {
    if (disabled_ || readonly_ || !focused_) {
        return {};
    }

    if (!buffer_->has_composition()) {
        return {.result = EditableTextResult::handled, .edit = TextEditResult::complete};
    }

    const auto before = snapshot();

    return finish(before, buffer_->commit_composition(), true);
}

EditableTextResult EditableTextController::cancel_composition() noexcept {
    if (!buffer_->has_composition()) {
        return EditableTextResult::handled;
    }

    buffer_->cancel_composition();

    return EditableTextResult::changed;
}

EditableTextResult EditableTextController::submit_search() noexcept {
    if (kind_ != EditableTextKind::search || disabled_ || !focused_) {
        return EditableTextResult::ignored;
    }

    if (!search_pending_) {
        return EditableTextResult::handled;
    }

    search_pending_ = false;

    return EditableTextResult::changed;
}

bool EditableTextController::write_semantics(
    std::string_view accessible_name,
    std::string_view value,
    SemanticNodeData& output
) const noexcept {
    if (accessible_name.empty()) {
        return false;
    }

    const bool suppress_value = kind_ == EditableTextKind::password && !reveal_password_;

    std::uint16_t states{};
    if (disabled_) {
        states = states | SemanticState::disabled;
    }
    if (readonly_) {
        states = states | SemanticState::read_only;
    }
    if (invalid_) {
        states = states | SemanticState::invalid;
    }

    std::uint16_t actions{};
    if (!disabled_ && !readonly_) {
        actions = actions | SemanticAction::set_value;
    }
    if (kind_ == EditableTextKind::search && !disabled_ && !readonly_) {
        actions = actions | SemanticAction::dismiss;
    }

    output = {
        .role = SemanticRole::text_input,
        .label = accessible_name,
        .value = suppress_value ? std::string_view{} : value,
        .states = states,
        .actions = actions,
    };

    return true;
}

EditableTextController::Snapshot EditableTextController::snapshot() const noexcept {
    return {
        .size = buffer_->size(),
        .cursor = buffer_->cursor(),
        .selection = buffer_->selection(),
    };
}

EditableTextEdit EditableTextController::finish(
    const Snapshot& before,
    TextEditResult edit,
    bool arm_search
) noexcept {
    if (edit != TextEditResult::complete) {
        return {.result = EditableTextResult::handled, .edit = edit};
    }

    const auto after = snapshot();
    if (before.size == after.size && before.cursor == after.cursor &&
        before.selection.start == after.selection.start &&
        before.selection.length == after.selection.length) {
        return {.result = EditableTextResult::handled, .edit = edit};
    }

    if (arm_search) {
        search_pending_ = true;
    }

    return {.result = EditableTextResult::changed, .edit = edit};
}

bool EditableTextController::mutable_state() const noexcept {
    return !disabled_ && !readonly_;
}

}
