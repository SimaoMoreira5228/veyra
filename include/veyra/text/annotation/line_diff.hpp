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

constexpr std::size_t kNoDiffLine = static_cast<std::size_t>(-1);

struct DiffRow {
    LineChangeKind kind{LineChangeKind::equal};
    std::size_t old_line{kNoDiffLine};
    std::size_t new_line{kNoDiffLine};
};

inline DiffRow diff_row_at(std::span<const LineChange> runs, std::size_t display_row) noexcept {
    auto row = std::size_t{};

    for (const auto& run : runs) {
        const auto height = run.old_count > run.new_count ? run.old_count : run.new_count;

        if (display_row - row < height) {
            const auto offset = display_row - row;

            if (run.kind == LineChangeKind::equal) {
                return {
                    .kind = LineChangeKind::equal,
                    .old_line = run.old_start + offset,
                    .new_line = run.new_start + offset,
                };
            }

            if (run.kind == LineChangeKind::deletion) {
                return {
                    .kind = LineChangeKind::deletion,
                    .old_line = run.old_start + offset,
                };
            }

            return {
                .kind = LineChangeKind::insertion,
                .new_line = run.new_start + offset,
            };
        }

        row += height;
    }

    return {};
}

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
