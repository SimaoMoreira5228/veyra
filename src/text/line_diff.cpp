#include "veyra/text/line_diff.hpp"

#include <utility>

namespace veyra {
namespace {

struct Middle {
    std::size_t old_begin{};
    std::size_t old_end{};
    std::size_t new_begin{};
    std::size_t new_end{};
};

Middle trim_edges(
    std::span<const std::string_view> old_lines,
    std::span<const std::string_view> new_lines
) noexcept {
    auto middle = Middle{
        .old_begin = 0,
        .old_end = old_lines.size(),
        .new_begin = 0,
        .new_end = new_lines.size(),
    };

    while (middle.old_begin < middle.old_end && middle.new_begin < middle.new_end &&
           old_lines[middle.old_begin] == new_lines[middle.new_begin]) {
        ++middle.old_begin;
        ++middle.new_begin;
    }

    while (middle.old_end > middle.old_begin && middle.new_end > middle.new_begin &&
           old_lines[middle.old_end - 1] == new_lines[middle.new_end - 1]) {
        --middle.old_end;
        --middle.new_end;
    }

    return middle;
}

struct RunWalk {
    std::span<LineChange> output{};
    std::size_t remaining{};
    bool dry{};

    LineChangeKind kind{LineChangeKind::equal};
    bool open{};
    std::size_t old_start{};
    std::size_t old_count{};
    std::size_t new_start{};
    std::size_t new_count{};

    void emit(
        LineChangeKind next,
        std::size_t old_at,
        std::size_t old_lines,
        std::size_t new_at,
        std::size_t new_lines
    ) noexcept {
        if (old_lines == 0 && new_lines == 0) {
            return;
        }

        if (dry) {
            ++remaining;

            return;
        }

        output[--remaining] = LineChange{
            .kind = next,
            .old_start = old_at,
            .old_count = old_lines,
            .new_start = new_at,
            .new_count = new_lines,
        };
    }

    void flush() noexcept {
        if (!open) {
            return;
        }

        open = false;
        emit(kind, old_start, old_count, new_start, new_count);
    }

    void step(LineChangeKind next, std::size_t old_at, std::size_t new_at) noexcept {
        if (!open || kind != next) {
            flush();
            kind = next;
            open = true;
            old_start = old_at;
            old_count = 0;
            new_start = new_at;
            new_count = 0;
        }

        if (next == LineChangeKind::equal) {
            old_start = old_at < old_start ? old_at : old_start;
            ++old_count;
            new_start = new_at < new_start ? new_at : new_start;
            ++new_count;

            return;
        }

        if (next == LineChangeKind::deletion) {
            old_start = old_at < old_start ? old_at : old_start;
            ++old_count;

            return;
        }

        new_start = new_at < new_start ? new_at : new_start;
        ++new_count;
    }
};

std::size_t count_changes(std::span<const LineChange> runs, std::size_t total) noexcept {
    auto changes = std::size_t{};

    for (auto index = std::size_t{}; index < total; ++index) {
        if (runs[index].kind != LineChangeKind::equal) {
            ++changes;
        }
    }

    return changes;
}

LineDiffOutcome finish_runs(std::span<LineChange> output, std::size_t total) noexcept {
    return {
        .result = LineDiffResult::complete,
        .runs = total,
        .change_runs = count_changes(output, total),
    };
}

LineDiffOutcome replace_middle(
    const Middle& middle,
    std::span<const std::string_view> old_lines,
    std::span<const std::string_view> new_lines,
    std::span<LineChange> output
) noexcept {
    auto walk = RunWalk{.output = output, .dry = true};

    walk.emit(LineChangeKind::equal, 0, middle.old_begin, 0, middle.new_begin);
    walk.emit(
        LineChangeKind::deletion,
        middle.old_begin,
        middle.old_end - middle.old_begin,
        middle.new_begin,
        0
    );
    walk.emit(
        LineChangeKind::insertion,
        middle.old_end,
        0,
        middle.new_begin,
        middle.new_end - middle.new_begin
    );
    walk.emit(
        LineChangeKind::equal,
        middle.old_end,
        old_lines.size() - middle.old_end,
        middle.new_end,
        new_lines.size() - middle.new_end
    );

    const auto needed = walk.remaining;

    if (output.size() < needed) {
        return {.result = LineDiffResult::insufficient_output, .runs = needed};
    }

    walk.dry = false;
    walk.remaining = needed;

    walk.emit(
        LineChangeKind::equal,
        middle.old_end,
        old_lines.size() - middle.old_end,
        middle.new_end,
        new_lines.size() - middle.new_end
    );
    walk.emit(
        LineChangeKind::insertion,
        middle.old_end,
        0,
        middle.new_begin,
        middle.new_end - middle.new_begin
    );
    walk.emit(
        LineChangeKind::deletion,
        middle.old_begin,
        middle.old_end - middle.old_begin,
        middle.new_begin,
        0
    );
    walk.emit(LineChangeKind::equal, 0, middle.old_begin, 0, middle.new_begin);

    return {
        .result = LineDiffResult::replaced,
        .runs = needed,
        .change_runs = count_changes(output, needed),
    };
}

std::ptrdiff_t trace_at(
    std::span<const std::ptrdiff_t> trace,
    std::size_t depth,
    std::ptrdiff_t diagonal
) noexcept {
    return trace
        [depth * depth + static_cast<std::size_t>(diagonal + static_cast<std::ptrdiff_t>(depth))];
}

void walk_middle(
    std::span<const std::ptrdiff_t> trace,
    std::size_t distance,
    std::size_t old_count,
    std::size_t new_count,
    std::size_t old_base,
    std::size_t new_base,
    RunWalk& walk
) noexcept {
    auto x = static_cast<std::ptrdiff_t>(old_count);
    auto y = static_cast<std::ptrdiff_t>(new_count);

    for (auto depth = distance; depth >= 1; --depth) {
        const auto signed_depth = static_cast<std::ptrdiff_t>(depth);
        const auto diagonal = x - y;
        std::ptrdiff_t previous_diagonal;

        if (diagonal == -signed_depth ||
            (diagonal != signed_depth &&
             trace_at(trace, depth - 1, diagonal - 1) < trace_at(trace, depth - 1, diagonal + 1))) {
            previous_diagonal = diagonal + 1;
        } else {
            previous_diagonal = diagonal - 1;
        }

        const auto previous_x = trace_at(trace, depth - 1, previous_diagonal);
        const auto previous_y = previous_x - previous_diagonal;

        while (x > previous_x && y > previous_y) {
            walk.step(
                LineChangeKind::equal,
                old_base + static_cast<std::size_t>(x - 1),
                new_base + static_cast<std::size_t>(y - 1)
            );
            --x;
            --y;
        }

        if (previous_diagonal == diagonal + 1) {
            walk.step(
                LineChangeKind::insertion,
                old_base + static_cast<std::size_t>(x),
                new_base + static_cast<std::size_t>(y - 1)
            );
            --y;
        } else {
            walk.step(
                LineChangeKind::deletion,
                old_base + static_cast<std::size_t>(x - 1),
                new_base + static_cast<std::size_t>(y)
            );
            --x;
        }
    }

    walk.flush();
}

}

LineDiffOutcome diff_lines(
    std::span<const std::string_view> old_lines,
    std::span<const std::string_view> new_lines,
    std::span<std::ptrdiff_t> scratch,
    std::span<LineChange> output
) noexcept {
    const auto middle = trim_edges(old_lines, new_lines);
    const auto old_count = middle.old_end - middle.old_begin;
    const auto new_count = middle.new_end - middle.new_begin;

    if (old_count == 0 && new_count == 0) {
        if (middle.old_begin == 0) {
            return {};
        }

        if (output.empty()) {
            return {.result = LineDiffResult::insufficient_output, .runs = 1};
        }

        output[0] = LineChange{
            .kind = LineChangeKind::equal,
            .old_start = 0,
            .old_count = middle.old_begin,
            .new_start = 0,
            .new_count = middle.new_begin,
        };

        return {.runs = 1};
    }

    const auto total_middle = old_count + new_count;

    if (line_diff_scratch_runs(total_middle) > scratch.size()) {
        return replace_middle(middle, old_lines, new_lines, output);
    }

    const auto width = 2 * total_middle + 1;
    auto previous = scratch.subspan(0, width);
    auto current = scratch.subspan(width, width);
    const auto trace = scratch.subspan(2 * width);
    const auto offset = static_cast<std::ptrdiff_t>(total_middle);
    const auto old_size = static_cast<std::ptrdiff_t>(old_count);
    const auto new_size = static_cast<std::ptrdiff_t>(new_count);

    for (auto& cell : previous) {
        cell = 0;
    }

    auto distance = total_middle;

    for (auto depth = std::size_t{}; depth <= total_middle; ++depth) {
        const auto signed_depth = static_cast<std::ptrdiff_t>(depth);
        bool reached = false;

        for (auto diagonal = -signed_depth; diagonal <= signed_depth; diagonal += 2) {
            std::ptrdiff_t x;

            if (diagonal == -signed_depth ||
                (diagonal != signed_depth &&
                 previous[static_cast<std::size_t>(diagonal - 1 + offset)] <
                     previous[static_cast<std::size_t>(diagonal + 1 + offset)])) {
                x = previous[static_cast<std::size_t>(diagonal + 1 + offset)];
            } else {
                x = previous[static_cast<std::size_t>(diagonal - 1 + offset)] + 1;
            }

            auto y = x - diagonal;

            while (x < old_size && y >= 0 && y < new_size &&
                   old_lines[middle.old_begin + static_cast<std::size_t>(x)] ==
                       new_lines[middle.new_begin + static_cast<std::size_t>(y)]) {
                ++x;
                ++y;
            }

            current[static_cast<std::size_t>(diagonal + offset)] = x;
            trace[depth * depth + static_cast<std::size_t>(diagonal + signed_depth)] = x;

            if (x >= old_size && y >= new_size) {
                distance = depth;
                reached = true;

                break;
            }
        }

        if (reached) {
            break;
        }

        std::swap(previous, current);
    }

    auto counter = RunWalk{.output = output, .dry = true};

    counter.emit(
        LineChangeKind::equal,
        middle.old_end,
        old_lines.size() - middle.old_end,
        middle.new_end,
        new_lines.size() - middle.new_end
    );
    walk_middle(trace, distance, old_count, new_count, middle.old_begin, middle.new_begin, counter);
    counter.emit(LineChangeKind::equal, 0, middle.old_begin, 0, middle.new_begin);

    const auto needed = counter.remaining;

    if (output.size() < needed) {
        return {.result = LineDiffResult::insufficient_output, .runs = needed};
    }

    auto writer = RunWalk{.output = output, .remaining = needed};

    writer.emit(
        LineChangeKind::equal,
        middle.old_end,
        old_lines.size() - middle.old_end,
        middle.new_end,
        new_lines.size() - middle.new_end
    );
    walk_middle(trace, distance, old_count, new_count, middle.old_begin, middle.new_begin, writer);
    writer.emit(LineChangeKind::equal, 0, middle.old_begin, 0, middle.new_begin);

    return finish_runs(output, needed);
}

}
