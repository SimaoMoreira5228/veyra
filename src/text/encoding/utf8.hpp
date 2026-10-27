#ifndef VEYRA_TEXT_UTF8_HPP
#define VEYRA_TEXT_UTF8_HPP

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace veyra::detail {

bool next_utf8_code_point(
    std::string_view text,
    std::size_t& byte_offset,
    std::uint32_t& code_point
) noexcept;

bool valid_utf8(std::string_view text) noexcept;

}

#endif
