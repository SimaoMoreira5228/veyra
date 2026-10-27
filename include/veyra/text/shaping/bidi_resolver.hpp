#ifndef VEYRA_TEXT_BIDI_RESOLVER_HPP
#define VEYRA_TEXT_BIDI_RESOLVER_HPP

#include "veyra/text/shaping/direction.hpp"

#include <unicode/ubidi.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string_view>
#include <vector>

namespace veyra {

enum class TextBidiResult : std::uint8_t {
    complete,
    invalid_utf8,
    input_limit_exceeded,
    multiple_paragraphs,
    invalid_direction,
    invalid_line_range,
    insufficient_output,
    initialization_failed,
};

enum class TextBidiParagraphDirection : std::uint8_t {
    left_to_right,
    right_to_left,
    automatic_left_to_right,
    automatic_right_to_left,
};

struct TextBidiRun {
    std::size_t byte_offset{};
    std::size_t byte_length{};
    TextDirection direction{TextDirection::left_to_right};
};

class TextBidiResolver {
  public:
    explicit TextBidiResolver(std::size_t maximum_input_bytes) noexcept;
    ~TextBidiResolver();

    TextBidiResolver(const TextBidiResolver&) = delete;
    TextBidiResolver& operator=(const TextBidiResolver&) = delete;
    TextBidiResolver(TextBidiResolver&&) = delete;
    TextBidiResolver& operator=(TextBidiResolver&&) = delete;

    TextBidiResult
    set_paragraph(std::string_view utf8, TextBidiParagraphDirection direction) noexcept;

    TextBidiResult visual_runs_for_line(
        std::size_t byte_offset,
        std::size_t byte_length,
        std::span<TextBidiRun> output,
        std::size_t& run_count
    ) noexcept;

  private:
    struct BiDiDeleter {
        void operator()(UBiDi* bidi) const noexcept;
    };

    std::vector<UChar> utf16_;
    std::vector<std::uint32_t> utf8_offsets_;
    std::unique_ptr<UBiDi, BiDiDeleter> paragraph_;
    std::unique_ptr<UBiDi, BiDiDeleter> line_;
    std::size_t maximum_input_bytes_{};
    std::size_t input_bytes_{};
    std::size_t utf16_length_{};
    bool paragraph_ready_{};
};

}

#endif
