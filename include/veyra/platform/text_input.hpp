#ifndef VEYRA_PLATFORM_TEXT_INPUT_HPP
#define VEYRA_PLATFORM_TEXT_INPUT_HPP

#include "veyra/layout/core/geometry.hpp"
#include "veyra/platform/window.hpp"
#include "veyra/text/editing/text_editor_buffer.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <span>
#include <string_view>

namespace veyra {

struct TextCommitApplied {
    TextEditResult edit{TextEditResult::complete};
    bool truncated{};
};

inline void apply_text_commit(
    TextEditorBuffer& buffer,
    const WindowTextCommit& commit,
    TextCommitApplied& applied
) noexcept {
    const std::size_t length = std::min<std::size_t>(commit.length, kWindowTextCommitCapacity);
    const std::string_view text(commit.text.data(), length);

    applied.truncated = commit.truncated;
    applied.edit = buffer.insert(text);
}

struct TextEditingApplied {
    TextEditResult edit{TextEditResult::complete};
    bool truncated{};
};

inline void apply_text_editing(
    TextEditorBuffer& buffer,
    std::span<char> storage,
    const WindowTextEditing& editing,
    TextEditingApplied& applied
) noexcept {
    const std::size_t length = std::min<std::size_t>(editing.length, kWindowTextPreeditCapacity);
    const std::string_view text(editing.text.data(), length);

    applied.truncated = editing.truncated;

    if (length == 0) {
        buffer.cancel_composition();
        applied.edit = TextEditResult::complete;

        return;
    }

    std::size_t cursor_byte = 0;

    if (editing.cursor >= 0) {
        if (utf8_byte_offset(text, static_cast<std::size_t>(editing.cursor), cursor_byte) !=
            TextEditResult::complete) {
            applied.edit = TextEditResult::invalid_cursor;

            return;
        }
    }

    TextSelection selection{cursor_byte, 0};

    if (editing.selection_length > 0) {
        if (editing.cursor < 0) {
            applied.edit = TextEditResult::invalid_cursor;

            return;
        }

        const std::size_t end_character = static_cast<std::size_t>(editing.cursor) +
            static_cast<std::size_t>(editing.selection_length);
        std::size_t end_byte = 0;

        if (utf8_byte_offset(text, end_character, end_byte) != TextEditResult::complete) {
            applied.edit = TextEditResult::invalid_cursor;

            return;
        }

        selection = {cursor_byte, end_byte - cursor_byte};
    }

    applied.edit = buffer.update_composition(storage, text, cursor_byte, selection);
}

inline TextInputArea
text_area_for_caret(LayoutRect caret_rect_px, std::size_t cursor_byte) noexcept {
    TextInputArea area{};

    if (!std::isfinite(caret_rect_px.x) || !std::isfinite(caret_rect_px.y) ||
        !std::isfinite(caret_rect_px.width) || !std::isfinite(caret_rect_px.height)) {
        return area;
    }

    const double clamp = static_cast<double>(std::numeric_limits<int>::max());
    const auto rounded_x = std::lround(static_cast<double>(caret_rect_px.x));
    const auto rounded_y = std::lround(static_cast<double>(caret_rect_px.y));
    const auto rounded_width = std::lround(static_cast<double>(caret_rect_px.width));
    const auto rounded_height = std::lround(static_cast<double>(caret_rect_px.height));

    area.x = rounded_x < std::numeric_limits<int>::min()
        ? std::numeric_limits<int>::min()
        : static_cast<int>(std::min<double>(static_cast<double>(rounded_x), clamp));
    area.y = rounded_y < std::numeric_limits<int>::min()
        ? std::numeric_limits<int>::min()
        : static_cast<int>(std::min<double>(static_cast<double>(rounded_y), clamp));
    area.width = rounded_width < 1
        ? 1
        : static_cast<int>(std::min<double>(static_cast<double>(rounded_width), clamp));
    area.height = rounded_height < 1
        ? 1
        : static_cast<int>(std::min<double>(static_cast<double>(rounded_height), clamp));
    area.cursor = cursor_byte > static_cast<std::size_t>(std::numeric_limits<int>::max())
        ? std::numeric_limits<int>::max()
        : static_cast<int>(cursor_byte);

    return area;
}

}

#endif
