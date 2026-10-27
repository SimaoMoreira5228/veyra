#include "veyra/text/annotation/line_diff.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace {

void require(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

struct Diffed {
    std::vector<std::ptrdiff_t> scratch;
    std::array<veyra::LineChange, 64> storage{};
    veyra::LineDiffOutcome outcome{};
};

Diffed
diff_all(std::span<const std::string_view> old_lines, std::span<const std::string_view> new_lines) {
    auto diffed = Diffed{};
    const auto middle = old_lines.size() + new_lines.size();
    diffed.scratch.resize(veyra::line_diff_scratch_runs(middle) + 1, -7);
    diffed.outcome = veyra::diff_lines(old_lines, new_lines, diffed.scratch, diffed.storage);

    require(
        diffed.outcome.result == veyra::LineDiffResult::complete,
        "roomy scratch takes the exact path"
    );

    return diffed;
}

void require_tiled(
    std::span<const std::string_view> old_lines,
    std::span<const std::string_view> new_lines,
    const Diffed& diffed
) {
    auto old_cursor = std::size_t{};
    auto new_cursor = std::size_t{};

    require(diffed.outcome.runs <= diffed.storage.size(), "reported runs fit the buffer");

    for (auto index = std::size_t{}; index < diffed.outcome.runs; ++index) {
        const auto& run = diffed.storage[index];

        if (run.kind == veyra::LineChangeKind::equal) {
            require(run.old_count == run.new_count, "equal runs cover both sides evenly");
        } else if (run.kind == veyra::LineChangeKind::deletion) {
            require(run.new_count == 0, "deletions touch only the old side");
        } else {
            require(
                run.old_count == 0 && run.kind == veyra::LineChangeKind::insertion,
                "insertions touch only the new side"
            );
        }

        require(
            run.old_start == old_cursor && run.new_start == new_cursor,
            "runs tile both files without gaps or overlaps"
        );

        for (auto offset = std::size_t{}; offset < run.old_count; ++offset) {
            require(
                run.old_start + offset < old_lines.size(),
                "old ranges stay inside the old file"
            );
        }

        for (auto offset = std::size_t{}; offset < run.new_count; ++offset) {
            require(
                run.new_start + offset < new_lines.size(),
                "new ranges stay inside the new file"
            );
        }

        if (run.kind == veyra::LineChangeKind::equal) {
            for (auto offset = std::size_t{}; offset < run.old_count; ++offset) {
                require(
                    old_lines[run.old_start + offset] == new_lines[run.new_start + offset],
                    "equal runs only cover identical lines"
                );
            }
        }

        old_cursor += run.old_count;
        new_cursor += run.new_count;
    }

    require(old_cursor == old_lines.size(), "old coverage is complete");
    require(new_cursor == new_lines.size(), "new coverage is complete");
}

void check_identical_files() {
    const std::array<std::string_view, 3> lines{"a", "b", "c"};
    const auto diffed = diff_all(lines, lines);

    require(diffed.outcome.runs == 1, "identical files diff to one equal run");
    require(diffed.outcome.change_runs == 0, "identical files report no changes");
    require_tiled(lines, lines, diffed);

    const std::span<const std::string_view> empty{};
    auto tiny = std::array<veyra::LineChange, 4>{};
    auto scratch = std::array<std::ptrdiff_t, 8>{};
    const auto outcome = veyra::diff_lines(empty, empty, scratch, tiny);
    require(
        outcome.result == veyra::LineDiffResult::complete && outcome.runs == 0,
        "two empty files diff to zero runs"
    );
}

void check_pure_insertion_and_deletion() {
    const std::span<const std::string_view> empty{};
    const std::array<std::string_view, 2> added{"x", "y"};
    const auto inserted = diff_all(empty, added);

    require(
        inserted.outcome.runs == 1 && inserted.outcome.change_runs == 1,
        "insertion into emptiness is one insertion run"
    );
    require_tiled(empty, added, inserted);

    const auto deleted = diff_all(added, empty);
    require(
        deleted.outcome.runs == 1 && deleted.storage[0].kind == veyra::LineChangeKind::deletion,
        "deletion to emptiness is one deletion run"
    );
    require_tiled(added, empty, deleted);
}

void check_single_substitution() {
    const std::array<std::string_view, 4> old_lines{"int a;", "int b;", "int c;", "int d;"};
    const std::array<std::string_view, 4> new_lines{"int a;", "int B;", "int c;", "int d;"};
    const auto diffed = diff_all(old_lines, new_lines);

    require_tiled(old_lines, new_lines, diffed);
    require(diffed.outcome.change_runs == 2, "one changed line is a delete plus an insert");

    bool saw_delete = false;
    bool saw_insert = false;

    for (auto index = std::size_t{}; index < diffed.outcome.runs; ++index) {
        const auto& run = diffed.storage[index];

        if (run.kind == veyra::LineChangeKind::deletion) {
            saw_delete = run.old_count == 1 && old_lines[run.old_start] == "int b;";
        }

        if (run.kind == veyra::LineChangeKind::insertion) {
            saw_insert = run.new_count == 1 && new_lines[run.new_start] == "int B;";
        }
    }

    require(saw_delete && saw_insert, "the substitution names both sides exactly");
}

void check_scattered_hunks() {
    const std::array<std::string_view, 7> old_lines{"1", "2", "3", "4", "5", "6", "7"};
    const std::array<std::string_view, 7> new_lines{"1", "two", "3", "4", "five", "6", "7"};
    const auto diffed = diff_all(old_lines, new_lines);

    require_tiled(old_lines, new_lines, diffed);

    auto changes = std::size_t{};

    for (auto index = std::size_t{}; index < diffed.outcome.runs; ++index) {
        if (diffed.storage[index].kind != veyra::LineChangeKind::equal) {
            ++changes;
        }
    }

    require(changes == diffed.outcome.change_runs, "reported changes match the runs");
    require(changes == 4, "two scattered edits are four change runs");
}

void check_reordered_block_collapses() {
    const std::array<std::string_view, 4> old_lines{"a", "b", "c", "d"};
    const std::array<std::string_view, 4> new_lines{"c", "d", "a", "b"};
    const auto diffed = diff_all(old_lines, new_lines);

    require_tiled(old_lines, new_lines, diffed);
    require(
        diffed.outcome.change_runs > 0,
        "a rotation reports changes instead of pretending nothing moved"
    );
}

void check_output_too_small() {
    const std::array<std::string_view, 4> old_lines{"a", "b", "c", "d"};
    const std::array<std::string_view, 3> new_lines{"a", "B", "d"};

    auto full = diff_all(old_lines, new_lines);
    require(full.outcome.runs > 1, "the fixture needs more than one run");

    auto tiny = std::array<veyra::LineChange, 1>{};
    auto scratch = std::vector<std::ptrdiff_t>(
        veyra::line_diff_scratch_runs(old_lines.size() + new_lines.size()),
        0
    );
    const auto short_outcome = veyra::diff_lines(old_lines, new_lines, scratch, tiny);
    require(
        short_outcome.result == veyra::LineDiffResult::insufficient_output &&
            short_outcome.runs == full.outcome.runs,
        "a short buffer reports how many runs fit the exact script"
    );
}

void check_small_scratch_replaces() {
    const std::array<std::string_view, 6> old_lines{"a", "b", "c", "d", "e", "f"};
    const std::array<std::string_view, 6> new_lines{"a", "b", "C", "d", "e", "g"};

    auto tiny_scratch = std::array<std::ptrdiff_t, 4>{};
    auto output = std::array<veyra::LineChange, 8>{};
    const auto outcome = veyra::diff_lines(old_lines, new_lines, tiny_scratch, output);

    require(
        outcome.result == veyra::LineDiffResult::replaced,
        "starved scratch falls back to a whole-middle replacement"
    );

    auto old_cursor = std::size_t{};
    auto new_cursor = std::size_t{};

    for (auto index = std::size_t{}; index < outcome.runs; ++index) {
        const auto& run = output[index];
        require(
            run.old_start == old_cursor && run.new_start == new_cursor,
            "the replacement still tiles without gaps"
        );
        old_cursor += run.old_count;
        new_cursor += run.new_count;
    }

    require(
        old_cursor == old_lines.size() && new_cursor == new_lines.size(),
        "the replacement covers both files end to end"
    );
    require(outcome.change_runs == 2, "the replacement names one delete and one insert");
}

void check_exact_fit_takes_the_exact_path() {
    const std::array<std::string_view, 3> old_lines{"a", "b", "c"};
    const std::array<std::string_view, 3> new_lines{"a", "B", "c"};

    auto scratch = std::vector<std::ptrdiff_t>(veyra::line_diff_scratch_runs(2), 0);
    auto output = std::array<veyra::LineChange, 8>{};
    const auto outcome = veyra::diff_lines(old_lines, new_lines, scratch, output);

    require(
        outcome.result == veyra::LineDiffResult::complete,
        "scratch sized for the trimmed middle takes the exact path"
    );
}

std::size_t split_words(std::string_view line, std::span<std::string_view> output) {
    auto count = std::size_t{};
    auto start = std::size_t{};

    for (auto index = std::size_t{}; index <= line.size(); ++index) {
        if (index == line.size() || line[index] == ' ') {
            if (count < output.size()) {
                output[count] = line.substr(start, index - start);
                ++count;
            }

            start = index + 1;
        }
    }

    return count;
}

void check_word_level_diff() {
    constexpr std::string_view old_line = "the quick brown fox";
    constexpr std::string_view new_line = "the quick red fox jumped";

    auto old_words = std::array<std::string_view, 8>{};
    auto new_words = std::array<std::string_view, 8>{};
    const auto old_count = split_words(old_line, old_words);
    const auto new_count = split_words(new_line, new_words);

    auto scratch =
        std::vector<std::ptrdiff_t>(veyra::line_diff_scratch_runs(old_count + new_count), 0);
    auto output = std::array<veyra::LineChange, 8>{};
    const auto outcome = veyra::diff_lines(
        std::span<const std::string_view>{old_words.data(), old_count},
        std::span<const std::string_view>{new_words.data(), new_count},
        scratch,
        output
    );

    require(
        outcome.result == veyra::LineDiffResult::complete,
        "word slices diff through the same entry point"
    );
    require(outcome.change_runs == 3, "one swap plus one append is three change runs");

    auto old_cursor = std::size_t{};
    auto new_cursor = std::size_t{};

    for (auto index = std::size_t{}; index < outcome.runs; ++index) {
        const auto& run = output[index];
        require(
            run.old_start == old_cursor && run.new_start == new_cursor,
            "word runs tile without gaps"
        );
        old_cursor += run.old_count;
        new_cursor += run.new_count;
    }

    require(old_cursor == old_count, "every old word is covered");
    require(new_cursor == new_count, "every new word is covered");
}

void check_display_rows_map_runs() {
    const std::array<std::string_view, 4> old_lines{"a", "b", "c", "d"};
    const std::array<std::string_view, 4> new_lines{"a", "B", "C", "d"};
    const auto diffed = diff_all(old_lines, new_lines);

    require(diffed.outcome.runs == 4, "a two-line swap merges into paired runs");

    const auto first = veyra::diff_row_at({diffed.storage.data(), diffed.outcome.runs}, 0);
    require(
        first.kind == veyra::LineChangeKind::equal && first.old_line == 0 && first.new_line == 0,
        "row zero maps to the shared first line"
    );

    const auto deleted = veyra::diff_row_at({diffed.storage.data(), diffed.outcome.runs}, 1);
    require(
        deleted.kind == veyra::LineChangeKind::deletion && deleted.old_line == 1 &&
            deleted.new_line == veyra::kNoDiffLine,
        "a deletion row names only its old line"
    );

    const auto inserted = veyra::diff_row_at({diffed.storage.data(), diffed.outcome.runs}, 3);
    require(
        inserted.kind == veyra::LineChangeKind::insertion &&
            inserted.old_line == veyra::kNoDiffLine && inserted.new_line == 1,
        "an insertion row names only its new line"
    );

    const auto second_insert = veyra::diff_row_at({diffed.storage.data(), diffed.outcome.runs}, 4);
    require(
        second_insert.kind == veyra::LineChangeKind::insertion && second_insert.new_line == 2,
        "stacked insertions keep their own line numbers"
    );

    const auto last = veyra::diff_row_at({diffed.storage.data(), diffed.outcome.runs}, 5);
    require(
        last.kind == veyra::LineChangeKind::equal && last.old_line == 3 && last.new_line == 3,
        "the tail row maps to the shared last line"
    );

    const auto past_end = veyra::diff_row_at({diffed.storage.data(), diffed.outcome.runs}, 6);
    require(
        past_end.old_line == veyra::kNoDiffLine && past_end.new_line == veyra::kNoDiffLine,
        "rows past the end name no line on either side"
    );
}

void check_diff_row_at_empty_runs() {
    const auto row = veyra::diff_row_at({}, 0);
    require(
        row.old_line == veyra::kNoDiffLine && row.new_line == veyra::kNoDiffLine,
        "empty runs name no line on either side"
    );
}

}

int main() {
    try {
        check_identical_files();
        check_pure_insertion_and_deletion();
        check_single_substitution();
        check_scattered_hunks();
        check_reordered_block_collapses();
        check_output_too_small();
        check_small_scratch_replaces();
        check_exact_fit_takes_the_exact_path();
        check_word_level_diff();
        check_display_rows_map_runs();
        check_diff_row_at_empty_runs();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
