#ifndef VEYRA_BEHAVIORS_TREE_CURSOR_HPP
#define VEYRA_BEHAVIORS_TREE_CURSOR_HPP

#include "veyra/behaviors/selection/selection.hpp"

#include <array>
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
    needs_children,
};

struct TreeCursorRow {
    std::uint64_t id{};
    std::uint64_t parent{};
    std::uint32_t depth{};
    bool has_children{};
    bool expanded{};
    bool children_known{true};
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

class TreeExpansion {
  public:
    static constexpr std::size_t kCapacity = 256;

    std::size_t count() const noexcept {
        return count_;
    }

    bool empty() const noexcept {
        return count_ == 0 && pending_count_ == 0;
    }

    bool is_expanded(std::uint64_t id) const noexcept {
        return find(id) != kNotFound;
    }

    std::size_t copy_expanded(std::span<std::uint64_t> output) const noexcept {
        std::size_t copied = count_;

        if (copied > output.size()) {
            copied = output.size();
        }

        for (std::size_t i = 0; i < copied; ++i) {
            output[i] = ids_[i];
        }

        return copied;
    }

    TreeCursorResult expand(std::uint64_t id) noexcept {
        if (id == 0) {
            return TreeCursorResult::ignored;
        }

        if (is_expanded(id)) {
            return TreeCursorResult::handled;
        }

        if (count_ == kCapacity) {
            return TreeCursorResult::ignored;
        }

        drop_pending(id);
        ids_[count_] = id;
        ++count_;

        return TreeCursorResult::expand;
    }

    TreeCursorResult expand(std::uint64_t id, std::span<const TreeCursorRow> rows) noexcept {
        const TreeCursorRow* row = find_row(id, rows);

        if (row == nullptr) {
            return TreeCursorResult::ignored;
        }

        if (!row->has_children) {
            return TreeCursorResult::handled;
        }

        if (is_expanded(id)) {
            return TreeCursorResult::handled;
        }

        if (!row->children_known) {
            if (find_pending(id) != kNotFound) {
                return TreeCursorResult::needs_children;
            }

            if (pending_count_ == kCapacity) {
                return TreeCursorResult::ignored;
            }

            pending_[pending_count_] = id;
            ++pending_count_;

            return TreeCursorResult::needs_children;
        }

        return expand(id);
    }

    TreeCursorResult collapse(std::uint64_t id) noexcept {
        const std::size_t index = find(id);
        const std::size_t waiting = find_pending(id);
        bool removed = false;

        if (index != kNotFound) {
            remove_at(index);
            removed = true;
        }

        if (waiting != kNotFound) {
            remove_pending_at(waiting);
            removed = true;
        }

        if (!removed) {
            return TreeCursorResult::handled;
        }

        return TreeCursorResult::collapse;
    }

    TreeCursorResult toggle(std::uint64_t id) noexcept {
        if (id == 0) {
            return TreeCursorResult::ignored;
        }

        if (is_expanded(id)) {
            return collapse(id);
        }

        return expand(id);
    }

    TreeCursorResult expand_all(std::span<const std::uint64_t> ids) noexcept {
        std::array<std::uint64_t, kCapacity + 1> unique{};
        std::size_t unique_size = 0;

        for (std::uint64_t id : ids) {
            if (id == 0) {
                continue;
            }

            bool duplicate = false;

            for (std::size_t i = 0; i < unique_size; ++i) {
                if (unique[i] == id) {
                    duplicate = true;
                    break;
                }
            }

            if (duplicate) {
                continue;
            }

            if (unique_size == kCapacity + 1) {
                break;
            }

            unique[unique_size] = id;
            ++unique_size;
        }

        if (unique_size > kCapacity) {
            return TreeCursorResult::ignored;
        }

        if (unique_size == 0) {
            return TreeCursorResult::handled;
        }

        std::size_t fresh = 0;

        for (std::size_t i = 0; i < unique_size; ++i) {
            if (find(unique[i]) == kNotFound) {
                ++fresh;
            }
        }

        if (fresh == 0) {
            return TreeCursorResult::handled;
        }

        if (count_ + fresh > kCapacity) {
            return TreeCursorResult::ignored;
        }

        for (std::size_t i = 0; i < unique_size; ++i) {
            if (find(unique[i]) == kNotFound) {
                drop_pending(unique[i]);
                ids_[count_] = unique[i];
                ++count_;
            }
        }

        return TreeCursorResult::expand;
    }

    TreeCursorResult collapse_all() noexcept {
        if (count_ == 0 && pending_count_ == 0) {
            return TreeCursorResult::handled;
        }

        count_ = 0;
        pending_count_ = 0;

        return TreeCursorResult::collapse;
    }

    TreeCursorResult confirm_children(std::uint64_t id) noexcept {
        const std::size_t waiting = find_pending(id);

        if (waiting == kNotFound) {
            return TreeCursorResult::handled;
        }

        if (is_expanded(id)) {
            remove_pending_at(waiting);

            return TreeCursorResult::handled;
        }

        if (count_ == kCapacity) {
            return TreeCursorResult::ignored;
        }

        remove_pending_at(waiting);
        ids_[count_] = id;
        ++count_;

        return TreeCursorResult::expand;
    }

  private:
    static constexpr std::size_t kNotFound = static_cast<std::size_t>(-1);

    static const TreeCursorRow*
    find_row(std::uint64_t id, std::span<const TreeCursorRow> rows) noexcept {
        if (id == 0) {
            return nullptr;
        }

        for (const auto& row : rows) {
            if (row.id == id) {
                return &row;
            }
        }

        return nullptr;
    }

    std::size_t find(std::uint64_t id) const noexcept {
        for (std::size_t i = 0; i < count_; ++i) {
            if (ids_[i] == id) {
                return i;
            }
        }

        return kNotFound;
    }

    std::size_t find_pending(std::uint64_t id) const noexcept {
        for (std::size_t i = 0; i < pending_count_; ++i) {
            if (pending_[i] == id) {
                return i;
            }
        }

        return kNotFound;
    }

    void remove_at(std::size_t index) noexcept {
        ids_[index] = ids_[count_ - 1];
        ids_[count_ - 1] = 0;
        --count_;
    }

    void remove_pending_at(std::size_t index) noexcept {
        pending_[index] = pending_[pending_count_ - 1];
        pending_[pending_count_ - 1] = 0;
        --pending_count_;
    }

    void drop_pending(std::uint64_t id) noexcept {
        const std::size_t waiting = find_pending(id);

        if (waiting != kNotFound) {
            remove_pending_at(waiting);
        }
    }

    std::array<std::uint64_t, kCapacity> ids_{};
    std::size_t count_{};
    std::array<std::uint64_t, kCapacity> pending_{};
    std::size_t pending_count_{};
};

inline std::size_t
tree_path_length(std::uint64_t id, std::span<const TreeCursorRow> rows) noexcept {
    if (id == 0 || rows.empty()) {
        return 0;
    }

    std::uint64_t current = id;

    for (std::size_t step = 0; step < rows.size(); ++step) {
        const TreeCursorRow* row = nullptr;

        for (const auto& candidate : rows) {
            if (candidate.id == current) {
                row = &candidate;
                break;
            }
        }

        if (row == nullptr) {
            return 0;
        }

        if (row->parent == 0) {
            return step + 1;
        }

        current = row->parent;
    }

    return 0;
}

inline std::size_t copy_tree_path(
    std::uint64_t id,
    std::span<const TreeCursorRow> rows,
    std::span<std::uint64_t> output
) noexcept {
    const std::size_t length = tree_path_length(id, rows);

    if (length == 0 || output.size() < length) {
        return 0;
    }

    std::uint64_t current = id;

    for (std::size_t index = 0; index < length; ++index) {
        output[length - 1 - index] = current;

        for (const auto& row : rows) {
            if (row.id == current) {
                current = row.parent;
                break;
            }
        }
    }

    return length;
}

inline SelectionResult select_tree_subtree(
    std::uint64_t id,
    std::span<const TreeCursorRow> rows,
    SelectionController& selection
) noexcept {
    if (id == 0) {
        return SelectionResult::ignored;
    }

    bool known = false;

    for (const auto& row : rows) {
        if (row.id == id) {
            known = true;
            break;
        }
    }

    if (!known) {
        return SelectionResult::ignored;
    }

    if (selection.mode() == SelectionMode::single) {
        return selection.select(id);
    }

    std::array<std::uint64_t, SelectionController::kCapacity + 1> gathered{};
    std::size_t gathered_size = 0;

    gathered[gathered_size] = id;
    ++gathered_size;

    for (const auto& row : rows) {
        if (row.id == 0 || row.id == id) {
            continue;
        }

        std::uint64_t current = row.parent;
        bool descendant = false;

        for (std::size_t step = 0; step < rows.size(); ++step) {
            if (current == 0) {
                break;
            }

            if (current == id) {
                descendant = true;
                break;
            }

            std::uint64_t next = 0;
            bool located = false;

            for (const auto& candidate : rows) {
                if (candidate.id == current) {
                    next = candidate.parent;
                    located = true;
                    break;
                }
            }

            if (!located) {
                break;
            }

            current = next;
        }

        if (!descendant) {
            continue;
        }

        bool duplicate = false;

        for (std::size_t i = 0; i < gathered_size; ++i) {
            if (gathered[i] == row.id) {
                duplicate = true;
                break;
            }
        }

        if (duplicate) {
            continue;
        }

        if (gathered_size == SelectionController::kCapacity + 1) {
            break;
        }

        gathered[gathered_size] = row.id;
        ++gathered_size;
    }

    if (gathered_size > SelectionController::kCapacity) {
        return SelectionResult::overflow;
    }

    return selection.select_all(std::span<const std::uint64_t>{gathered.data(), gathered_size});
}

}

#endif
