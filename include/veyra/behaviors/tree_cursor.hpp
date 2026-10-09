#ifndef VEYRA_BEHAVIORS_TREE_CURSOR_HPP
#define VEYRA_BEHAVIORS_TREE_CURSOR_HPP

#include <cstddef>
#include <cstdint>
#include <span>

namespace veyra {

enum class TreeCursorMove : std::uint8_t {
    up,
    down,
    home,
    end,
    left,
    right,
};

enum class TreeCursorResult : std::uint8_t {
    ignored,
    handled,
    moved,
    expand,
    collapse,
};

struct TreeCursorRow {
    std::uint64_t id{};
    std::uint32_t depth{};
    bool has_children{};
    bool expanded{};
};

struct TreeCursorOutcome {
    TreeCursorResult result{TreeCursorResult::ignored};
    std::uint64_t id{};
};

inline TreeCursorOutcome resolve_tree_cursor(
    std::span<const TreeCursorRow> rows,
    std::uint64_t focused,
    TreeCursorMove move
) noexcept {
    if (rows.empty()) {
        return {};
    }

    for (std::size_t i = 1; i < rows.size(); ++i) {
        if (rows[i].depth > rows[i - 1].depth + 1) {
            return {};
        }
    }

    constexpr std::size_t kNotFound = static_cast<std::size_t>(-1);
    std::size_t current = kNotFound;

    for (std::size_t i = 0; i < rows.size(); ++i) {
        if (rows[i].id == focused) {
            current = i;
            break;
        }
    }

    if (current == kNotFound) {
        return {};
    }

    const TreeCursorRow& row = rows[current];

    switch (move) {
    case TreeCursorMove::up:
        if (current == 0) {
            return {
                .result = TreeCursorResult::handled,
                .id = focused,
            };
        }

        return {
            .result = TreeCursorResult::moved,
            .id = rows[current - 1].id,
        };
    case TreeCursorMove::down:
        if (current + 1 == rows.size()) {
            return {
                .result = TreeCursorResult::handled,
                .id = focused,
            };
        }

        return {
            .result = TreeCursorResult::moved,
            .id = rows[current + 1].id,
        };
    case TreeCursorMove::home:
        if (current == 0) {
            return {
                .result = TreeCursorResult::handled,
                .id = focused,
            };
        }

        return {
            .result = TreeCursorResult::moved,
            .id = rows.front().id,
        };
    case TreeCursorMove::end:
        if (current + 1 == rows.size()) {
            return {
                .result = TreeCursorResult::handled,
                .id = focused,
            };
        }

        return {
            .result = TreeCursorResult::moved,
            .id = rows.back().id,
        };
    case TreeCursorMove::right:
        if (!row.has_children) {
            return {
                .result = TreeCursorResult::handled,
                .id = focused,
            };
        }

        if (!row.expanded) {
            return {
                .result = TreeCursorResult::expand,
                .id = focused,
            };
        }

        if (current + 1 < rows.size() && rows[current + 1].depth == row.depth + 1) {
            return {
                .result = TreeCursorResult::moved,
                .id = rows[current + 1].id,
            };
        }

        return {
            .result = TreeCursorResult::handled,
            .id = focused,
        };
    case TreeCursorMove::left:
        if (row.has_children && row.expanded) {
            return {
                .result = TreeCursorResult::collapse,
                .id = focused,
            };
        }

        for (std::size_t i = current; i > 0; --i) {
            if (rows[i - 1].depth < row.depth) {
                return {
                    .result = TreeCursorResult::moved,
                    .id = rows[i - 1].id,
                };
            }
        }

        return {
            .result = TreeCursorResult::handled,
            .id = focused,
        };
    }

    return {};
}

}

#endif
