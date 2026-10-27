#ifndef VEYRA_TEXT_LINE_BREAKER_HPP
#define VEYRA_TEXT_LINE_BREAKER_HPP

#include <unicode/ubrk.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string_view>

namespace veyra {

enum class LineBreakResult : std::uint8_t {
    complete,
    invalid_utf8,
    input_too_large,
    insufficient_output,
    initialization_failed,
};

enum class TextLineBreakKind : std::uint8_t {
    soft,
    hard,
};

struct TextLineBreak {
    std::size_t byte_offset{};
    TextLineBreakKind kind{TextLineBreakKind::soft};
};

class TextLineBreaker {
  public:
    explicit TextLineBreaker(const char* locale = "root") noexcept;
    ~TextLineBreaker();

    TextLineBreaker(const TextLineBreaker&) = delete;
    TextLineBreaker& operator=(const TextLineBreaker&) = delete;
    TextLineBreaker(TextLineBreaker&&) = delete;
    TextLineBreaker& operator=(TextLineBreaker&&) = delete;

    LineBreakResult find_breaks(
        std::string_view utf8,
        std::span<TextLineBreak> output,
        std::size_t& break_count
    ) noexcept;

  private:
    struct BreakIteratorDeleter {
        void operator()(UBreakIterator* iterator) const noexcept;
    };

    std::unique_ptr<UBreakIterator, BreakIteratorDeleter> iterator_;
};

}

#endif
