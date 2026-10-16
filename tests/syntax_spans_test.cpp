#include "veyra/text/syntax_spans.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string_view>

namespace {

void require(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

struct Lexed {
    std::array<veyra::SyntaxSpan, 32> storage{};
    veyra::SyntaxLineOutcome outcome{};
};

Lexed lex_all(std::string_view line, veyra::SyntaxLineState state = {}) {
    auto lexed = Lexed{};
    lexed.outcome = veyra::lex_c_line(line, state, lexed.storage);

    require(
        lexed.outcome.result == veyra::SyntaxResult::complete,
        "the scratch fits the whole line"
    );

    return lexed;
}

void require_tiled(std::string_view line, const Lexed& lexed) {
    auto cursor = std::size_t{};

    for (auto index = std::size_t{}; index < lexed.outcome.spans; ++index) {
        const auto& span = lexed.storage[index];
        require(span.start == cursor, "spans tile the line without gaps");
        cursor += span.length;
    }

    require(cursor == line.size(), "spans cover the full line");
}

void require_kind(
    const Lexed& lexed,
    std::size_t index,
    std::string_view line,
    std::string_view text,
    veyra::SyntaxKind kind
) {
    require(index < lexed.outcome.spans, "the span index exists");
    const auto& span = lexed.storage[index];
    require(
        line.substr(span.start, span.length) == text && span.kind == kind,
        "the span covers the expected text with the expected kind"
    );
}

void check_empty_and_plain_lines() {
    const auto empty = lex_all({}, {});
    require(empty.outcome.spans == 0, "an empty line produces no spans");
    require(!empty.outcome.end.in_block_comment, "an empty line preserves the carry state");

    const auto plain = lex_all("x += 1;", {});
    require(plain.outcome.spans == 4, "code splits into identifier, gap, number, and tail spans");
    require_kind(plain, 0, "x += 1;", "x", veyra::SyntaxKind::plain);
    require_kind(plain, 1, "x += 1;", " += ", veyra::SyntaxKind::plain);
    require_kind(plain, 2, "x += 1;", "1", veyra::SyntaxKind::number);
    require_kind(plain, 3, "x += 1;", ";", veyra::SyntaxKind::plain);
    require_tiled("x += 1;", plain);
}

void check_keywords_and_numbers() {
    constexpr std::string_view line = "constexpr int count = 0xFF + 1e10 + 3.14f + 1'000 + .5;";
    const auto lexed = lex_all(line, {});

    require_kind(lexed, 0, line, "constexpr", veyra::SyntaxKind::keyword);
    require_tiled(line, lexed);

    bool saw_hex = false;
    bool saw_exponent = false;
    bool saw_float = false;
    bool saw_separator = false;
    bool saw_leading_dot = false;

    for (auto index = std::size_t{}; index < lexed.outcome.spans; ++index) {
        const auto& span = lexed.storage[index];
        const auto text = line.substr(span.start, span.length);

        if (text == "0xFF" || text == "1e10" || text == "3.14f" || text == "1'000" ||
            text == ".5") {
            require(
                span.kind == veyra::SyntaxKind::number,
                "hex, exponent, suffix, separator, and leading-dot numbers lex whole"
            );
            saw_hex = saw_hex || text == "0xFF";
            saw_exponent = saw_exponent || text == "1e10";
            saw_float = saw_float || text == "3.14f";
            saw_separator = saw_separator || text == "1'000";
            saw_leading_dot = saw_leading_dot || text == ".5";
        }
    }

    require(
        saw_hex && saw_exponent && saw_float && saw_separator && saw_leading_dot,
        "every number form appears exactly once"
    );

    const auto tricky = lex_all("const_cast<constinit>constexpr", {});
    require_kind(
        tricky,
        0,
        "const_cast<constinit>constexpr",
        "const_cast",
        veyra::SyntaxKind::keyword
    );
    require_kind(
        tricky,
        2,
        "const_cast<constinit>constexpr",
        "constinit",
        veyra::SyntaxKind::keyword
    );
    require_kind(
        tricky,
        4,
        "const_cast<constinit>constexpr",
        "constexpr",
        veyra::SyntaxKind::keyword
    );

    const auto ident = lex_all("abc123", {});
    require_kind(ident, 0, "abc123", "abc123", veyra::SyntaxKind::plain);

    const auto glued = lex_all("123abc", {});
    require_kind(glued, 0, "123abc", "123abc", veyra::SyntaxKind::number);
}

void check_strings_and_chars() {
    constexpr std::string_view line = R"("a\"b" + 'c' + '\'' + "unt) = \";)";
    const auto lexed = lex_all(line, {});

    require_kind(lexed, 0, line, "\"a\\\"b\"", veyra::SyntaxKind::string);
    require_tiled(line, lexed);

    bool saw_char = false;
    bool saw_escaped_quote = false;

    for (auto index = std::size_t{}; index < lexed.outcome.spans; ++index) {
        const auto& span = lexed.storage[index];

        if (line.substr(span.start, span.length) == "'c'") {
            saw_char = span.kind == veyra::SyntaxKind::string;
        }

        if (line.substr(span.start, span.length) == "'\\''") {
            saw_escaped_quote = span.kind == veyra::SyntaxKind::string;
        }
    }

    require(saw_char, "a char literal lexes as a string span");
    require(saw_escaped_quote, "an escaped quote stays inside the char literal");

    const auto open = lex_all("\"oops", {});
    require(
        open.outcome.spans == 1 && open.storage[0].kind == veyra::SyntaxKind::string &&
            !open.outcome.end.in_block_comment,
        "an unterminated string runs to the line end without carrying"
    );
}

void check_line_comments() {
    constexpr std::string_view line = "int x = 1; // done";
    const auto lexed = lex_all(line, {});

    require(lexed.outcome.spans == 7, "gaps, symbols, and the comment each get a span");
    require_kind(lexed, 0, line, "int", veyra::SyntaxKind::keyword);
    require_kind(lexed, 6, line, "// done", veyra::SyntaxKind::comment);
    require_tiled(line, lexed);
}

void check_block_comments_carry() {
    auto storage = std::array<veyra::SyntaxSpan, 32>{};
    auto outcome = veyra::lex_c_line("int a = /* start", {}, storage);
    require(
        outcome.result == veyra::SyntaxResult::complete && outcome.end.in_block_comment,
        "an unterminated block comment carries to the next line"
    );

    outcome = veyra::lex_c_line("still * / done */ int b = 2;", outcome.end, storage);
    require(
        outcome.result == veyra::SyntaxResult::complete && !outcome.end.in_block_comment &&
            outcome.spans == 8,
        "the continuation closes the comment and code resumes after it"
    );

    constexpr std::string_view continued = "still * / done */ int b = 2;";
    require(
        storage[0].kind == veyra::SyntaxKind::comment &&
            continued.substr(storage[0].start, storage[0].length) == "still * / done */",
        "the carried comment covers up to its terminator"
    );
    require(
        storage[1].kind == veyra::SyntaxKind::plain &&
            continued.substr(storage[1].start, storage[1].length) == " ",
        "code after the terminator lexes normally"
    );
    require(
        storage[2].kind == veyra::SyntaxKind::keyword &&
            continued.substr(storage[2].start, storage[2].length) == "int",
        "keywords still highlight after a carried comment"
    );
}

void check_inline_block_comment() {
    constexpr std::string_view line = "a /* hi */ b";
    const auto lexed = lex_all(line, {});

    require(lexed.outcome.spans == 5, "an inline comment splits the line around itself");
    require_kind(lexed, 2, line, "/* hi */", veyra::SyntaxKind::comment);
    require_tiled(line, lexed);

    constexpr std::string_view empty_comment = "/**/";
    const auto empty = lex_all(empty_comment, {});
    require(
        empty.outcome.spans == 1 && empty.storage[0].kind == veyra::SyntaxKind::comment,
        "an empty block comment is one comment span"
    );

    constexpr std::string_view slash_star = "/*/";
    const auto open = lex_all(slash_star, {});
    require(
        open.outcome.end.in_block_comment,
        "a lone terminator start without an end carries forward"
    );
}

void check_directives() {
    constexpr std::string_view line = "  #include <vec> // why";
    const auto lexed = lex_all(line, {});

    require(lexed.outcome.spans == 2, "leading space plus directive is two spans");
    require_kind(lexed, 0, line, "  ", veyra::SyntaxKind::plain);
    require_kind(lexed, 1, line, "#include <vec> // why", veyra::SyntaxKind::directive);
    require_tiled(line, lexed);

    constexpr std::string_view code_hash = "int x = 0; # nope";
    const auto code = lex_all(code_hash, {});
    require(
        code.storage[code.outcome.spans - 1].kind != veyra::SyntaxKind::directive,
        "a hash after code is not a directive"
    );
    require_tiled(code_hash, code);
}

void check_insufficient_buffer_retries() {
    constexpr std::string_view line = "int answer = 42; // life";
    auto tiny = std::array<veyra::SyntaxSpan, 1>{};
    const auto short_outcome = veyra::lex_c_line(line, {}, tiny);
    require(
        short_outcome.result == veyra::SyntaxResult::insufficient_spans,
        "a one-span buffer cannot hold a decorated line"
    );
    require(
        !short_outcome.end.in_block_comment,
        "the failed call still reports a usable carry state"
    );

    auto roomy = std::array<veyra::SyntaxSpan, 32>{};
    const auto full_outcome = veyra::lex_c_line(line, {}, roomy);
    require(full_outcome.result == veyra::SyntaxResult::complete, "the retry with room succeeds");
    require_tiled(line, Lexed{.storage = roomy, .outcome = full_outcome});
}

void check_utf8_stays_plain() {
    constexpr std::string_view line = "auto 名 = \"値\"; // 日本語";
    const auto lexed = lex_all(line, {});

    require_tiled(line, lexed);

    for (auto index = std::size_t{}; index < lexed.outcome.spans; ++index) {
        const auto& span = lexed.storage[index];
        const auto text = line.substr(span.start, span.length);

        if (text == "auto") {
            require(span.kind == veyra::SyntaxKind::keyword, "keywords still hit in UTF-8 soup");
        } else if (text == "\"値\"") {
            require(span.kind == veyra::SyntaxKind::string, "strings span whole UTF-8 runs");
        } else if (text == "// 日本語") {
            require(span.kind == veyra::SyntaxKind::comment, "comments run to the byte end");
        } else {
            require(
                span.kind == veyra::SyntaxKind::plain,
                "CJK identifiers fall back to plain, never to fragments"
            );
        }
    }
}

}

int main() {
    try {
        check_empty_and_plain_lines();
        check_keywords_and_numbers();
        check_strings_and_chars();
        check_line_comments();
        check_block_comments_carry();
        check_inline_block_comment();
        check_directives();
        check_insufficient_buffer_retries();
        check_utf8_stays_plain();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}