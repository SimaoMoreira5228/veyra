#ifndef VEYRA_TEXT_LINE_DIFF_HPP
#define VEYRA_TEXT_LINE_DIFF_HPP

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace veyra {

enum class LineChangeKind : std::uint8_t {
    equal,
    deletion,
    insertion,
};

enum class LineDiffResult : std::uint8_t {
    complete,
    replaced,
    insufficient_output,
};

struct LineChange {
    LineChangeKind kind{LineChangeKind::equal};
    std::size_t old_start{};
    std::size_t old_count{};
    std::size_t new_start{};
    std::size_t new_count{};
};

struct LineDiffOutcome {
    LineDiffResult result{LineDiffResult::complete};
    std::size_t runs{};
    std::size_t change_runs{};
};

constexpr std::size_t line_diff_scratch_runs(std::size_t middle_lines) noexcept {
    return (middle_lines + 1) * (middle_lines + 1) + 2 * (2 * middle_lines + 1);
}

LineDiffOutcome diff_lines(
    std::span<const std::string_view> old_lines,
    std::span<const std::string_view> new_lines,
    std::span<std::ptrdiff_t> scratch,
    std::span<LineChange> output
) noexcept;

}

#endif
