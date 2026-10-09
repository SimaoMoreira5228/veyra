#ifndef VEYRA_BEHAVIORS_EDITABLE_TEXT_HPP
#define VEYRA_BEHAVIORS_EDITABLE_TEXT_HPP

#include "veyra/core/semantics.hpp"
#include "veyra/text/text_editor_buffer.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace veyra {

enum class EditableTextKind : std::uint8_t {
    text,
    password,
    search,
};

enum class EditableTextResult : std::uint8_t {
    ignored,
    handled,
    changed,
};

struct EditableTextConfig {
    EditableTextKind kind{EditableTextKind::text};
};

struct EditableTextEdit {
    EditableTextResult result{EditableTextResult::ignored};
    TextEditResult edit{TextEditResult::invalid_state};
};

struct EditableTextState {
    EditableTextKind kind{EditableTextKind::text};
    bool focused{};
    bool disabled{};
    bool readonly{};
    bool invalid{};
    bool reveal_password{};
    bool composing{};
};

class EditableTextController {
  public:
    explicit EditableTextController(TextEditorBuffer& buffer) noexcept;
    EditableTextController(const EditableTextController&) = delete;
    EditableTextController& operator=(const EditableTextController&) = delete;

    TextEditorBuffer& buffer() noexcept;

    EditableTextState state() const noexcept;
    EditableTextKind kind() const noexcept;
    bool focused() const noexcept;
    bool disabled() const noexcept;
    bool readonly() const noexcept;
    bool invalid() const noexcept;
    bool reveal_password() const noexcept;
    bool composing() const noexcept;
    TextSelection selection() const noexcept;

    bool configure(const EditableTextConfig& config) noexcept;
    bool set_kind(EditableTextKind kind) noexcept;
    bool set_disabled(bool disabled) noexcept;
    bool set_readonly(bool readonly) noexcept;
    bool set_invalid(bool invalid) noexcept;
    bool set_reveal_password(bool reveal) noexcept;
    EditableTextResult set_focused(bool focused) noexcept;

    EditableTextEdit insert(std::string_view text) noexcept;
    EditableTextEdit paste(std::string_view text) noexcept;
    EditableTextEdit erase_backward() noexcept;
    EditableTextEdit erase_forward() noexcept;
    EditableTextEdit set_cursor(std::size_t byte_offset, bool extend_selection = false) noexcept;
    EditableTextResult select_all() noexcept;
    EditableTextEdit clear() noexcept;

    TextEditResult copy_selection(std::span<char> output, std::size_t& length) const noexcept;
    EditableTextEdit cut_selection(std::span<char> output, std::size_t& length) noexcept;

    EditableTextEdit update_composition(
        std::span<char> storage,
        std::string_view text,
        std::size_t cursor,
        TextSelection selection
    ) noexcept;
    EditableTextEdit commit_composition() noexcept;
    EditableTextResult cancel_composition() noexcept;

    EditableTextResult submit_search() noexcept;

    bool write_semantics(
        std::string_view accessible_name,
        std::string_view value,
        SemanticNodeData& output
    ) const noexcept;

  private:
    struct Snapshot {
        std::size_t size{};
        std::size_t cursor{};
        TextSelection selection{};
    };

    Snapshot snapshot() const noexcept;
    EditableTextEdit finish(const Snapshot& before, TextEditResult edit, bool arm_search) noexcept;
    bool mutable_state() const noexcept;

    TextEditorBuffer* buffer_;
    EditableTextKind kind_{EditableTextKind::text};
    bool focused_{};
    bool disabled_{};
    bool readonly_{};
    bool invalid_{};
    bool reveal_password_{};
    bool search_pending_{};
};

}

#endif
