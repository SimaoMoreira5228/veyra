#include "utf8.hpp"

namespace veyra::detail {

bool next_utf8_code_point(
    std::string_view text,
    std::size_t& byte_offset,
    std::uint32_t& code_point
) noexcept {
    if (byte_offset >= text.size()) {
        return false;
    }

    const auto* bytes = reinterpret_cast<const std::uint8_t*>(text.data());
    const auto first = bytes[byte_offset];
    if (first <= 0x7fU) {
        code_point = first;
        ++byte_offset;
        return true;
    }

    std::size_t sequence_length = 0;
    if (first >= 0xc2U && first <= 0xdfU) {
        sequence_length = 2;
        code_point = first & 0x1fU;
    } else if (first >= 0xe0U && first <= 0xefU) {
        sequence_length = 3;
        code_point = first & 0x0fU;
    } else if (first >= 0xf0U && first <= 0xf4U) {
        sequence_length = 4;
        code_point = first & 0x07U;
    } else {
        return false;
    }
    if (sequence_length > text.size() - byte_offset) {
        return false;
    }

    for (std::size_t index = 1; index < sequence_length; ++index) {
        const auto byte = bytes[byte_offset + index];
        if ((byte & 0xc0U) != 0x80U) {
            return false;
        }
        code_point = (code_point << 6U) | (byte & 0x3fU);
    }
    if ((sequence_length == 2 && code_point < 0x80U) ||
        (sequence_length == 3 && code_point < 0x800U) ||
        (sequence_length == 4 && code_point < 0x10000U) || code_point > 0x10ffffU ||
        (code_point >= 0xd800U && code_point <= 0xdfffU)) {
        return false;
    }
    byte_offset += sequence_length;
    return true;
}

bool valid_utf8(std::string_view text) noexcept {
    std::size_t byte_offset = 0;
    while (byte_offset < text.size()) {
        std::uint32_t code_point = 0;
        if (!next_utf8_code_point(text, byte_offset, code_point)) {
            return false;
        }
    }
    return true;
}

}
