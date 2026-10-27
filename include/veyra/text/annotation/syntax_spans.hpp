#ifndef VEYRA_TEXT_SYNTAX_SPANS_HPP
#define VEYRA_TEXT_SYNTAX_SPANS_HPP

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace veyra {

enum class SyntaxKind : std::uint8_t {
    plain,
    keyword,
    number,
    string,
    comment,
    directive,
};

enum class SyntaxResult : std::uint8_t {
    complete,
    insufficient_spans,
};

struct SyntaxSpan {
    std::uint32_t start{};
    std::uint32_t length{};
    SyntaxKind kind{SyntaxKind::plain};
};

struct SyntaxLineState {
    bool in_block_comment{};
};

struct SyntaxLineOutcome {
    SyntaxResult result{SyntaxResult::complete};
    SyntaxLineState end{};
    std::size_t spans{};
};

SyntaxLineOutcome
lex_c_line(std::string_view line, SyntaxLineState state, std::span<SyntaxSpan> output) noexcept;

}

#endif
