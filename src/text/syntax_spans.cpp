#include "veyra/text/syntax_spans.hpp"

#include <algorithm>
#include <string_view>

namespace veyra {
namespace {

constexpr std::string_view kKeywords[] = {
    "_Alignas",
    "_Alignof",
    "_Atomic",
    "_Bool",
    "_Complex",
    "_Generic",
    "_Imaginary",
    "_Noreturn",
    "_Static_assert",
    "_Thread_local",
    "alignas",
    "alignof",
    "asm",
    "auto",
    "bool",
    "break",
    "case",
    "catch",
    "char",
    "char16_t",
    "char32_t",
    "char8_t",
    "class",
    "co_await",
    "co_return",
    "co_yield",
    "concept",
    "const",
    "const_cast",
    "consteval",
    "constexpr",
    "constinit",
    "continue",
    "decltype",
    "default",
    "delete",
    "do",
    "double",
    "dynamic_cast",
    "else",
    "enum",
    "explicit",
    "export",
    "extern",
    "false",
    "float",
    "for",
    "friend",
    "goto",
    "if",
    "inline",
    "int",
    "long",
    "mutable",
    "namespace",
    "new",
    "noexcept",
    "nullptr",
    "operator",
    "private",
    "protected",
    "public",
    "register",
    "reinterpret_cast",
    "requires",
    "restrict",
    "return",
    "short",
    "signed",
    "sizeof",
    "static",
    "static_assert",
    "static_cast",
    "struct",
    "switch",
    "template",
    "this",
    "thread_local",
    "throw",
    "true",
    "try",
    "typedef",
    "typename",
    "typeof",
    "typeof_unqual",
    "union",
    "unsigned",
    "using",
    "virtual",
    "void",
    "volatile",
    "wchar_t",
    "while",
};

bool is_keyword(std::string_view word) noexcept {
    return std::binary_search(std::begin(kKeywords), std::end(kKeywords), word);
}

static_assert(std::is_sorted(std::begin(kKeywords), std::end(kKeywords)));

bool is_identifier_start(unsigned char byte) noexcept {
    return byte == '_' || (byte >= 'A' && byte <= 'Z') || (byte >= 'a' && byte <= 'z');
}

bool is_identifier_byte(unsigned char byte) noexcept {
    return is_identifier_start(byte) || (byte >= '0' && byte <= '9');
}

bool is_number_byte(unsigned char byte) noexcept {
    return (byte >= '0' && byte <= '9') || (byte >= 'A' && byte <= 'Z') ||
        (byte >= 'a' && byte <= 'z') || byte == '_' || byte == '.' || byte == '\'';
}

struct Emitter {
    std::span<SyntaxSpan> output{};
    std::size_t count{};

    bool emit(std::size_t start, std::size_t length, SyntaxKind kind) noexcept {
        if (length == 0) {
            return true;
        }

        if (count >= output.size()) {
            return false;
        }

        output[count++] = SyntaxSpan{
            .start = static_cast<std::uint32_t>(start),
            .length = static_cast<std::uint32_t>(length),
            .kind = kind,
        };

        return true;
    }
};

bool emit_quoted(
    std::string_view line,
    std::size_t& position,
    Emitter& spans,
    unsigned char quote
) noexcept {
    const auto start = position;
    ++position;

    while (position < line.size()) {
        const auto byte = static_cast<unsigned char>(line[position]);

        if (byte == '\\') {
            position += (position + 1 < line.size()) ? 2 : 1;

            continue;
        }

        ++position;

        if (byte == quote) {
            break;
        }
    }

    return spans.emit(start, position - start, SyntaxKind::string);
}

bool emit_directive(std::string_view line, std::size_t& position, Emitter& spans) noexcept {
    const auto start = position;
    position = line.size();

    return spans.emit(start, position - start, SyntaxKind::directive);
}

bool emit_line_comment(std::string_view line, std::size_t& position, Emitter& spans) noexcept {
    const auto start = position;
    position = line.size();

    return spans.emit(start, position - start, SyntaxKind::comment);
}

}

SyntaxLineOutcome
lex_c_line(std::string_view line, SyntaxLineState state, std::span<SyntaxSpan> output) noexcept {
    auto spans = Emitter{.output = output};
    auto position = std::size_t{};
    auto plain_start = std::size_t{};
    auto end = state;
    bool directive_seen = false;

    const auto flush_plain = [&](std::size_t end_at) noexcept {
        if (end_at <= plain_start) {
            return true;
        }

        const bool flushed = spans.emit(plain_start, end_at - plain_start, SyntaxKind::plain);

        if (flushed) {
            plain_start = end_at;
        }

        return flushed;
    };

    if (end.in_block_comment) {
        while (position + 1 < line.size() &&
               !(line[position] == '*' && line[position + 1] == '/')) {
            ++position;
        }

        if (position + 1 >= line.size()) {
            if (!spans.emit(0, line.size(), SyntaxKind::comment)) {
                return {
                    .result = SyntaxResult::insufficient_spans,
                    .end = state,
                    .spans = spans.count
                };
            }

            return {.end = end, .spans = spans.count};
        }

        position += 2;
        end.in_block_comment = false;

        if (!spans.emit(0, position, SyntaxKind::comment)) {
            return {.result = SyntaxResult::insufficient_spans, .end = state, .spans = spans.count};
        }

        plain_start = position;
    }

    while (position < line.size()) {
        const auto byte = static_cast<unsigned char>(line[position]);

        if (!directive_seen && byte == '#') {
            bool only_space = true;

            for (auto scan = std::size_t{}; scan < position; ++scan) {
                const auto earlier = static_cast<unsigned char>(line[scan]);

                if (earlier != ' ' && earlier != '\t') {
                    only_space = false;

                    break;
                }
            }

            directive_seen = true;

            if (only_space) {
                if (!flush_plain(position)) {
                    return {
                        .result = SyntaxResult::insufficient_spans,
                        .end = state,
                        .spans = spans.count
                    };
                }

                if (!emit_directive(line, position, spans)) {
                    return {
                        .result = SyntaxResult::insufficient_spans,
                        .end = state,
                        .spans = spans.count
                    };
                }

                plain_start = position;

                continue;
            }
        }

        if (byte == '/' && position + 1 < line.size() && line[position + 1] == '/') {
            if (!flush_plain(position)) {
                return {
                    .result = SyntaxResult::insufficient_spans,
                    .end = state,
                    .spans = spans.count
                };
            }

            if (!emit_line_comment(line, position, spans)) {
                return {
                    .result = SyntaxResult::insufficient_spans,
                    .end = state,
                    .spans = spans.count
                };
            }

            plain_start = position;

            continue;
        }

        if (byte == '/' && position + 1 < line.size() && line[position + 1] == '*') {
            if (!flush_plain(position)) {
                return {
                    .result = SyntaxResult::insufficient_spans,
                    .end = state,
                    .spans = spans.count
                };
            }

            const auto start = position;
            position += 2;

            while (position + 1 < line.size() &&
                   !(line[position] == '*' && line[position + 1] == '/')) {
                ++position;
            }

            if (position + 1 >= line.size()) {
                end.in_block_comment = true;
                position = line.size();

                if (!spans.emit(start, position - start, SyntaxKind::comment)) {
                    return {
                        .result = SyntaxResult::insufficient_spans,
                        .end = state,
                        .spans = spans.count
                    };
                }

                plain_start = position;

                break;
            }

            position += 2;

            if (!spans.emit(start, position - start, SyntaxKind::comment)) {
                return {
                    .result = SyntaxResult::insufficient_spans,
                    .end = state,
                    .spans = spans.count
                };
            }

            plain_start = position;

            continue;
        }

        if (byte == '"' || byte == '\'') {
            if (!flush_plain(position)) {
                return {
                    .result = SyntaxResult::insufficient_spans,
                    .end = state,
                    .spans = spans.count
                };
            }

            if (!emit_quoted(line, position, spans, byte)) {
                return {
                    .result = SyntaxResult::insufficient_spans,
                    .end = state,
                    .spans = spans.count
                };
            }

            plain_start = position;

            continue;
        }

        const bool dot_number = byte == '.' && position + 1 < line.size() &&
            line[position + 1] >= '0' && line[position + 1] <= '9';

        if ((byte >= '0' && byte <= '9') || dot_number) {
            if (!flush_plain(position)) {
                return {
                    .result = SyntaxResult::insufficient_spans,
                    .end = state,
                    .spans = spans.count
                };
            }

            const auto start = position;

            while (position < line.size() &&
                   is_number_byte(static_cast<unsigned char>(line[position]))) {
                ++position;
            }

            if (!spans.emit(start, position - start, SyntaxKind::number)) {
                return {
                    .result = SyntaxResult::insufficient_spans,
                    .end = state,
                    .spans = spans.count
                };
            }

            plain_start = position;

            continue;
        }

        if (is_identifier_start(byte)) {
            if (!flush_plain(position)) {
                return {
                    .result = SyntaxResult::insufficient_spans,
                    .end = state,
                    .spans = spans.count
                };
            }

            const auto start = position;

            while (position < line.size() &&
                   is_identifier_byte(static_cast<unsigned char>(line[position]))) {
                ++position;
            }

            const auto word = line.substr(start, position - start);
            const auto kind = is_keyword(word) ? SyntaxKind::keyword : SyntaxKind::plain;

            if (!spans.emit(start, position - start, kind)) {
                return {
                    .result = SyntaxResult::insufficient_spans,
                    .end = state,
                    .spans = spans.count
                };
            }

            plain_start = position;

            continue;
        }

        ++position;
    }

    if (!flush_plain(line.size())) {
        return {.result = SyntaxResult::insufficient_spans, .end = state, .spans = spans.count};
    }

    return {.end = end, .spans = spans.count};
}

}
