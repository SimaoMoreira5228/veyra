#include "veyra/behaviors/command_palette.hpp"

namespace veyra {

CommandPalette::CommandPalette() noexcept {
    filtered_.fill(0);
}

PaletteState CommandPalette::state() const noexcept {
    return {
        .expanded = expanded_,
        .disabled = !enabled_,
        .loop = loop_,
        .items = item_count_,
        .matches = filtered_count_,
        .total = total_matches_,
        .overflow = overflow_,
        .has_active = has_active_,
        .active = active_,
        .generation = generation_,
    };
}

bool CommandPalette::expanded() const noexcept {
    return expanded_;
}

bool CommandPalette::disabled() const noexcept {
    return !enabled_;
}

bool CommandPalette::loops() const noexcept {
    return loop_;
}

std::size_t CommandPalette::item_count() const noexcept {
    return item_count_;
}

std::size_t CommandPalette::match_count() const noexcept {
    return filtered_count_;
}

std::size_t CommandPalette::total_matches() const noexcept {
    return total_matches_;
}

bool CommandPalette::overflowed() const noexcept {
    return overflow_;
}

bool CommandPalette::has_results() const noexcept {
    return filtered_count_ > 0;
}

PaletteActive CommandPalette::active() const noexcept {
    return {
        .has = has_active_,
        .id = active_,
    };
}

std::uint64_t CommandPalette::generation() const noexcept {
    return generation_;
}

std::span<const char> CommandPalette::query() const noexcept {
    return query_;
}

bool CommandPalette::contains(std::uint64_t id) const noexcept {
    return find_match(id) != kNotFound;
}

bool CommandPalette::is_enabled(std::uint64_t id) const noexcept {
    const std::size_t match = find_match(id);

    return match != kNotFound && filtered_enabled_[match];
}

std::size_t CommandPalette::copy_matches(std::span<std::uint64_t> output) const noexcept {
    std::size_t copied = filtered_count_;

    if (copied > output.size()) {
        copied = output.size();
    }

    for (std::size_t i = 0; i < copied; ++i) {
        output[i] = filtered_[i];
    }

    return copied;
}

std::size_t CommandPalette::copy_groups(std::span<std::uint64_t> output) const noexcept {
    std::size_t copied = 0;

    for (std::size_t i = 0; i < filtered_count_; ++i) {
        bool seen = false;

        for (std::size_t j = 0; j < copied; ++j) {
            if (output[j] == filtered_groups_[i]) {
                seen = true;

                break;
            }
        }

        if (seen) {
            continue;
        }

        if (copied == output.size()) {
            break;
        }

        output[copied] = filtered_groups_[i];
        ++copied;
    }

    return copied;
}

bool CommandPalette::set_loop(bool loop) noexcept {
    if (loop_ == loop) {
        return false;
    }

    loop_ = loop;

    return true;
}

bool CommandPalette::set_enabled(bool enabled) noexcept {
    if (enabled_ == enabled) {
        return false;
    }

    enabled_ = enabled;

    return true;
}

bool CommandPalette::set_expanded(bool expanded) noexcept {
    if (!enabled_) {
        return false;
    }

    if (expanded_ == expanded) {
        return false;
    }

    expanded_ = expanded;

    return true;
}

PaletteResult CommandPalette::set_items(std::span<const PaletteItem> items) noexcept {
    if (!enabled_) {
        return PaletteResult::ignored;
    }

    if (items.size() > kItemCapacity) {
        return PaletteResult::overflow;
    }

    for (std::size_t i = 0; i < items.size(); ++i) {
        items_[i] = items[i];
    }
    item_count_ = items.size();
    filtered_count_ = 0;
    total_matches_ = 0;
    overflow_ = false;
    has_active_ = false;
    active_ = 0;

    return PaletteResult::changed;
}

PaletteResult CommandPalette::set_query(std::span<const char> query) noexcept {
    if (!enabled_) {
        return PaletteResult::ignored;
    }

    if (same_query(query, query_)) {
        return PaletteResult::handled;
    }

    query_ = query;
    ++generation_;

    return PaletteResult::changed;
}

std::uint64_t CommandPalette::invalidate() noexcept {
    if (!enabled_) {
        return generation_;
    }

    ++generation_;

    return generation_;
}

PaletteResult CommandPalette::filter(
    std::span<const char> query,
    std::span<const PaletteItem> items,
    std::span<const std::string_view> titles,
    std::span<const std::string_view> keywords,
    std::span<std::uint64_t> output_ids,
    std::size_t& output_count
) noexcept {
    output_count = 0;

    if (titles.size() != items.size() || keywords.size() != items.size()) {
        return PaletteResult::ignored;
    }

    const std::size_t total = collect_matches(query, items, titles, keywords, output_ids);
    output_count = total;

    if (total > output_ids.size()) {
        return PaletteResult::overflow;
    }

    return PaletteResult::changed;
}

PaletteResult CommandPalette::apply_filter(
    std::span<const std::string_view> titles,
    std::span<const std::string_view> keywords
) noexcept {
    if (!enabled_) {
        return PaletteResult::ignored;
    }

    if (titles.size() != item_count_ || keywords.size() != item_count_) {
        return PaletteResult::ignored;
    }

    std::array<std::uint64_t, kResultCapacity> next{};
    std::array<bool, kResultCapacity> next_enabled{};
    std::array<std::uint64_t, kResultCapacity> next_groups{};
    const std::size_t total = collect_matches(
        query_,
        std::span<const PaletteItem>{items_.data(), item_count_},
        titles,
        keywords,
        std::span<std::uint64_t>{next.data(), next.size()},
        std::span<bool>{next_enabled.data(), next_enabled.size()},
        std::span<std::uint64_t>{next_groups.data(), next_groups.size()}
    );

    std::size_t filled = total;

    if (filled > next.size()) {
        filled = next.size();
    }

    const bool overflow = total > next.size();
    bool has_next = false;
    std::uint64_t next_active = 0;

    for (std::size_t i = 0; i < filled; ++i) {
        if (next_enabled[i]) {
            has_next = true;
            next_active = next[i];

            break;
        }
    }

    const bool same =
        same_window(next.data(), next_enabled.data(), filled, has_next, next_active, overflow);

    for (std::size_t i = 0; i < filled; ++i) {
        filtered_[i] = next[i];
        filtered_enabled_[i] = next_enabled[i];
        filtered_groups_[i] = next_groups[i];
    }
    filtered_count_ = filled;
    total_matches_ = total;
    overflow_ = overflow;
    has_active_ = has_next;
    active_ = next_active;

    if (overflow) {
        return PaletteResult::overflow;
    }

    if (same) {
        return PaletteResult::handled;
    }

    return PaletteResult::changed;
}

PaletteResult CommandPalette::move(PaletteMove direction) noexcept {
    if (!enabled_) {
        return PaletteResult::ignored;
    }

    if (direction != PaletteMove::next && direction != PaletteMove::prev &&
        direction != PaletteMove::first && direction != PaletteMove::last) {
        return PaletteResult::ignored;
    }

    if (filtered_count_ == 0 || enabled_total() == 0) {
        return PaletteResult::ignored;
    }

    const std::size_t first = scan_enabled(filtered_count_ - 1, true);
    const std::size_t last = scan_enabled(0, false);

    if (!has_active_) {
        has_active_ = true;
        active_ = (direction == PaletteMove::prev || direction == PaletteMove::last)
            ? filtered_[last]
            : filtered_[first];

        return PaletteResult::changed;
    }

    const std::size_t current = find_match(active_);

    if (current == kNotFound) {
        return PaletteResult::ignored;
    }

    std::size_t target = kNotFound;

    switch (direction) {
    case PaletteMove::next:
        target = scan_enabled(current, true);

        if (!loop_ && target != kNotFound && target < current) {
            target = kNotFound;
        }
        break;
    case PaletteMove::prev:
        target = scan_enabled(current, false);

        if (!loop_ && target != kNotFound && target > current) {
            target = kNotFound;
        }
        break;
    case PaletteMove::first:
        target = first;
        break;
    case PaletteMove::last:
        target = last;
        break;
    default:
        return PaletteResult::ignored;
    }

    if (target == kNotFound || target == current) {
        return PaletteResult::handled;
    }

    has_active_ = true;
    active_ = filtered_[target];

    return PaletteResult::changed;
}

PaletteResult CommandPalette::move_next() noexcept {
    return move(PaletteMove::next);
}

PaletteResult CommandPalette::move_prev() noexcept {
    return move(PaletteMove::prev);
}

PaletteResult CommandPalette::move_first() noexcept {
    return move(PaletteMove::first);
}

PaletteResult CommandPalette::move_last() noexcept {
    return move(PaletteMove::last);
}

std::uint64_t CommandPalette::activate() const noexcept {
    if (!enabled_ || !has_active_) {
        return 0;
    }

    const std::size_t index = find_item(active_);

    if (index == kNotFound) {
        return 0;
    }

    if (items_[index].disabled) {
        return 0;
    }

    return active_;
}

bool CommandPalette::write_box_semantics(
    std::string_view accessible_name,
    SemanticNodeData& output
) const noexcept {
    if (accessible_name.empty()) {
        return false;
    }

    std::uint16_t states{};

    if (!enabled_) {
        states = states | SemanticState::disabled;
    }

    if (expanded_) {
        states = states | SemanticState::expanded;
    }

    output = {
        .role = SemanticRole::combobox,
        .label = accessible_name,
        .states = states,
        .actions = enabled_ ? action_flag(SemanticAction::expand) : std::uint16_t{},
    };

    return true;
}

bool CommandPalette::write_item_semantics(
    std::uint64_t id,
    std::string_view accessible_name,
    SemanticNodeData& output
) const noexcept {
    if (accessible_name.empty()) {
        return false;
    }

    if (id == 0) {
        return false;
    }

    bool item_disabled = false;
    const std::size_t index = find_item(id);

    if (index != kNotFound) {
        item_disabled = items_[index].disabled;
    }

    std::uint16_t states{};

    if (!enabled_ || item_disabled) {
        states = states | SemanticState::disabled;
    }

    if (has_active_ && active_ == id) {
        states = states | SemanticState::focused;
    }

    output = {
        .role = SemanticRole::option,
        .label = accessible_name,
        .states = states,
        .actions =
            (!enabled_ || item_disabled) ? std::uint16_t{} : action_flag(SemanticAction::select),
    };

    return true;
}

bool CommandPalette::same_query(std::span<const char> left, std::span<const char> right) noexcept {
    if (left.size() != right.size()) {
        return false;
    }

    for (std::size_t i = 0; i < left.size(); ++i) {
        if (left[i] != right[i]) {
            return false;
        }
    }

    return true;
}

bool CommandPalette::starts_with(std::string_view text, std::span<const char> query) noexcept {
    if (text.size() < query.size()) {
        return false;
    }

    for (std::size_t i = 0; i < query.size(); ++i) {
        if (text[i] != query[i]) {
            return false;
        }
    }

    return true;
}

bool CommandPalette::word_prefix_match(
    std::string_view text,
    std::span<const char> query
) noexcept {
    if (query.empty() || text.size() < query.size()) {
        return false;
    }

    for (std::size_t pos = 1; pos + query.size() <= text.size(); ++pos) {
        const char boundary = text[pos - 1];

        if (boundary != ' ' && boundary != '/' && boundary != '-' && boundary != '_' &&
            boundary != '.') {
            continue;
        }

        bool hit = true;

        for (std::size_t i = 0; i < query.size(); ++i) {
            if (text[pos + i] != query[i]) {
                hit = false;

                break;
            }
        }

        if (hit) {
            return true;
        }
    }

    return false;
}

bool CommandPalette::subsequence_match(
    std::string_view text,
    std::span<const char> query
) noexcept {
    if (text.size() < query.size()) {
        return false;
    }

    std::size_t matched = 0;

    for (char value : text) {
        if (value == query[matched]) {
            ++matched;

            if (matched == query.size()) {
                return true;
            }
        }
    }

    return false;
}

int CommandPalette::rank_text(std::string_view text, std::span<const char> query) noexcept {
    if (text.size() < query.size()) {
        return kNoRank;
    }

    if (starts_with(text, query)) {
        return 0;
    }

    if (word_prefix_match(text, query)) {
        return 1;
    }

    if (subsequence_match(text, query)) {
        return 2;
    }

    return kNoRank;
}

int CommandPalette::rank_item(
    std::string_view title,
    std::string_view keywords,
    std::span<const char> query
) noexcept {
    if (query.empty()) {
        return 0;
    }

    int best = rank_text(title, query);

    if (!keywords.empty()) {
        const int keyword_rank = rank_text(keywords, query);

        if (keyword_rank != kNoRank && (best == kNoRank || keyword_rank < best)) {
            best = keyword_rank;
        }
    }

    return best;
}

std::size_t CommandPalette::collect_matches(
    std::span<const char> query,
    std::span<const PaletteItem> items,
    std::span<const std::string_view> titles,
    std::span<const std::string_view> keywords,
    std::span<std::uint64_t> ids,
    std::span<bool> enabled,
    std::span<std::uint64_t> groups
) noexcept {
    std::size_t total = 0;

    for (std::size_t i = 0; i < items.size(); ++i) {
        if (items[i].id == 0) {
            continue;
        }

        if (rank_item(titles[i], keywords[i], query) != kNoRank) {
            ++total;
        }
    }

    std::size_t filled = 0;

    for (int tier = 0; tier <= 2 && filled < ids.size(); ++tier) {
        for (std::size_t i = 0; i < items.size() && filled < ids.size(); ++i) {
            if (items[i].id == 0) {
                continue;
            }

            if (rank_item(titles[i], keywords[i], query) != tier) {
                continue;
            }

            ids[filled] = items[i].id;

            if (filled < enabled.size()) {
                enabled[filled] = !items[i].disabled;
            }

            if (filled < groups.size()) {
                groups[filled] = items[i].group_id;
            }

            ++filled;
        }
    }

    return total;
}

std::size_t CommandPalette::find_item(std::uint64_t id) const noexcept {
    for (std::size_t i = 0; i < item_count_; ++i) {
        if (items_[i].id == id) {
            return i;
        }
    }

    return kNotFound;
}

std::size_t CommandPalette::find_match(std::uint64_t id) const noexcept {
    for (std::size_t i = 0; i < filtered_count_; ++i) {
        if (filtered_[i] == id) {
            return i;
        }
    }

    return kNotFound;
}

std::size_t CommandPalette::enabled_total() const noexcept {
    std::size_t total = 0;

    for (std::size_t i = 0; i < filtered_count_; ++i) {
        if (filtered_enabled_[i]) {
            ++total;
        }
    }

    return total;
}

std::size_t CommandPalette::scan_enabled(std::size_t from, bool forward) const noexcept {
    for (std::size_t step = 1; step <= filtered_count_; ++step) {
        const std::size_t index = forward ? (from + step) % filtered_count_
                                          : (from + filtered_count_ - step) % filtered_count_;

        if (filtered_enabled_[index]) {
            return index;
        }
    }

    return kNotFound;
}

bool CommandPalette::same_window(
    const std::uint64_t* ids,
    const bool* enabled,
    std::size_t size,
    bool has_active,
    std::uint64_t active,
    bool overflow
) const noexcept {
    if (filtered_count_ != size) {
        return false;
    }

    if (has_active_ != has_active) {
        return false;
    }

    if (has_active && active_ != active) {
        return false;
    }

    if (overflow_ != overflow) {
        return false;
    }

    for (std::size_t i = 0; i < size; ++i) {
        if (filtered_[i] != ids[i] || filtered_enabled_[i] != enabled[i]) {
            return false;
        }
    }

    return true;
}

}
