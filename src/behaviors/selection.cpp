#include "veyra/behaviors/selection.hpp"

namespace veyra {

SelectionState SelectionController::state() const noexcept {
    return {
        .mode = mode_,
        .allow_empty = allow_empty_,
        .count = count_,
        .disabled = disabled_,
    };
}

SelectionMode SelectionController::mode() const noexcept {
    return mode_;
}

bool SelectionController::allows_empty() const noexcept {
    return allow_empty_;
}

bool SelectionController::disabled() const noexcept {
    return disabled_;
}

bool SelectionController::configure(const SelectionConfig& config) noexcept {
    if (mode_ == config.mode && allow_empty_ == config.allow_empty) {
        return false;
    }

    mode_ = config.mode;
    allow_empty_ = config.allow_empty;
    count_ = 0;
    exclude_ = false;
    has_anchor_ = false;
    anchor_ = 0;
    has_current_ = false;
    current_ = 0;

    return true;
}

bool SelectionController::set_disabled(bool disabled) noexcept {
    if (disabled_ == disabled) {
        return false;
    }

    disabled_ = disabled;

    return true;
}

std::size_t SelectionController::count() const noexcept {
    return count_;
}

bool SelectionController::empty() const noexcept {
    if (exclude_) {
        return false;
    }

    return count_ == 0;
}

bool SelectionController::contains(std::uint64_t id) const noexcept {
    const bool held = find(id) != kNotFound;

    if (exclude_) {
        return !held;
    }

    return held;
}

bool SelectionController::exclude_all() const noexcept {
    return exclude_;
}

bool SelectionController::has_anchor() const noexcept {
    return has_anchor_;
}

std::uint64_t SelectionController::anchor() const noexcept {
    return anchor_;
}

bool SelectionController::has_current() const noexcept {
    return has_current_;
}

std::uint64_t SelectionController::current() const noexcept {
    return current_;
}

std::size_t SelectionController::copy_selected(std::span<std::uint64_t> output) const noexcept {
    if (exclude_) {
        return 0;
    }

    std::size_t copied = count_;

    if (copied > output.size()) {
        copied = output.size();
    }

    for (std::size_t i = 0; i < copied; ++i) {
        output[i] = ids_[i];
    }

    return copied;
}

bool SelectionController::write_item_semantics(
    std::uint64_t id,
    std::string_view accessible_name,
    SemanticNodeData& output
) const noexcept {
    if (accessible_name.empty()) {
        return false;
    }

    std::uint16_t states{};
    if (disabled_) {
        states = states | SemanticState::disabled;
    }
    if (contains(id)) {
        states = states | SemanticState::selected;
    }

    output = {
        .role = SemanticRole::list_item,
        .label = accessible_name,
        .states = states,
        .actions = disabled_ ? std::uint16_t{} : action_flag(SemanticAction::select),
    };

    return true;
}

SelectionResult SelectionController::select(std::uint64_t id) noexcept {
    if (disabled_) {
        return SelectionResult::ignored;
    }

    if (exclude_) {
        const std::size_t index = find(id);

        if (index == kNotFound) {
            return SelectionResult::handled;
        }

        remove_at(index);
        has_anchor_ = true;
        anchor_ = id;

        return SelectionResult::changed;
    }

    if (mode_ == SelectionMode::single) {
        if (count_ == 1 && ids_[0] == id) {
            has_anchor_ = true;
            anchor_ = id;

            return SelectionResult::handled;
        }

        ids_[0] = id;
        count_ = 1;
        has_anchor_ = true;
        anchor_ = id;

        return SelectionResult::changed;
    }

    if (contains(id)) {
        has_anchor_ = true;
        anchor_ = id;

        return SelectionResult::handled;
    }

    if (count_ == kCapacity) {
        return SelectionResult::overflow;
    }

    ids_[count_] = id;
    ++count_;
    has_anchor_ = true;
    anchor_ = id;

    return SelectionResult::changed;
}

SelectionResult SelectionController::deselect(std::uint64_t id) noexcept {
    if (disabled_) {
        return SelectionResult::ignored;
    }

    if (exclude_) {
        if (find(id) != kNotFound) {
            return SelectionResult::handled;
        }

        if (count_ == kCapacity) {
            return SelectionResult::overflow;
        }

        ids_[count_] = id;
        ++count_;
        has_anchor_ = true;
        anchor_ = id;

        return SelectionResult::changed;
    }

    const std::size_t index = find(id);

    if (index == kNotFound) {
        return SelectionResult::handled;
    }

    if (mode_ == SelectionMode::single && !allow_empty_) {
        return SelectionResult::ignored;
    }

    remove_at(index);
    if (has_anchor_ && anchor_ == id) {
        has_anchor_ = false;
        anchor_ = 0;
    }

    return SelectionResult::changed;
}

SelectionResult SelectionController::toggle_item(std::uint64_t id) noexcept {
    if (disabled_) {
        return SelectionResult::ignored;
    }

    if (contains(id)) {
        return deselect(id);
    }

    return select(id);
}

SelectionResult SelectionController::select_current() noexcept {
    if (disabled_) {
        return SelectionResult::ignored;
    }

    if (!has_current_) {
        return SelectionResult::ignored;
    }

    if (mode_ == SelectionMode::single) {
        return select(current_);
    }

    return toggle_item(current_);
}

SelectionResult SelectionController::clear() noexcept {
    if (disabled_) {
        return SelectionResult::ignored;
    }

    if (exclude_) {
        if (count_ == 0) {
            return SelectionResult::handled;
        }

        count_ = 0;
        has_anchor_ = false;
        anchor_ = 0;

        return SelectionResult::changed;
    }

    if (count_ == 0) {
        return SelectionResult::handled;
    }

    if (mode_ == SelectionMode::single && !allow_empty_) {
        return SelectionResult::ignored;
    }

    count_ = 0;
    has_anchor_ = false;
    anchor_ = 0;

    return SelectionResult::changed;
}

SelectionResult
SelectionController::extend_range(std::span<const std::uint64_t> order, std::uint64_t to) noexcept {
    if (disabled_) {
        return SelectionResult::ignored;
    }

    if (exclude_) {
        return SelectionResult::overflow;
    }

    std::size_t to_index = kNotFound;

    for (std::size_t i = 0; i < order.size(); ++i) {
        if (order[i] == to) {
            to_index = i;
            break;
        }
    }

    if (to_index == kNotFound) {
        return SelectionResult::ignored;
    }

    std::size_t anchor_index = kNotFound;

    if (has_anchor_) {
        for (std::size_t i = 0; i < order.size(); ++i) {
            if (order[i] == anchor_) {
                anchor_index = i;
                break;
            }
        }
    }

    if (anchor_index == kNotFound) {
        has_current_ = true;
        current_ = to;

        return select(to);
    }

    if (mode_ == SelectionMode::single) {
        has_current_ = true;
        current_ = to;

        return select(to);
    }

    std::size_t low = anchor_index;

    if (to_index < low) {
        low = to_index;
    }

    std::size_t high = anchor_index;

    if (to_index > high) {
        high = to_index;
    }

    std::array<std::uint64_t, kCapacity + 1> range{};
    std::size_t range_size = 0;

    for (std::size_t i = low; i <= high; ++i) {
        bool duplicate = false;

        for (std::size_t j = 0; j < range_size; ++j) {
            if (range[j] == order[i]) {
                duplicate = true;
                break;
            }
        }

        if (duplicate) {
            continue;
        }

        if (range_size == kCapacity + 1) {
            break;
        }

        range[range_size] = order[i];
        ++range_size;
    }

    if (range_size > kCapacity) {
        return SelectionResult::overflow;
    }

    has_current_ = true;
    current_ = to;

    if (same_set(range.data(), range_size)) {
        return SelectionResult::handled;
    }

    for (std::size_t i = 0; i < range_size; ++i) {
        ids_[i] = range[i];
    }
    count_ = range_size;

    return SelectionResult::changed;
}

SelectionResult SelectionController::select_all(std::span<const std::uint64_t> ids) noexcept {
    if (disabled_) {
        return SelectionResult::ignored;
    }

    if (mode_ == SelectionMode::single) {
        return SelectionResult::ignored;
    }

    std::array<std::uint64_t, kCapacity + 1> unique{};
    std::size_t unique_size = 0;

    for (std::uint64_t id : ids) {
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
        return SelectionResult::overflow;
    }

    if (unique_size == 0) {
        return SelectionResult::handled;
    }

    if (!exclude_ && same_set(unique.data(), unique_size)) {
        return SelectionResult::handled;
    }

    exclude_ = false;
    has_anchor_ = true;
    anchor_ = unique[0];

    for (std::size_t i = 0; i < unique_size; ++i) {
        ids_[i] = unique[i];
    }
    count_ = unique_size;

    return SelectionResult::changed;
}

bool SelectionController::set_current(std::uint64_t id) noexcept {
    if (has_current_ && current_ == id) {
        return false;
    }

    has_current_ = true;
    current_ = id;

    return true;
}

bool SelectionController::clear_current() noexcept {
    if (!has_current_) {
        return false;
    }

    has_current_ = false;
    current_ = 0;

    return true;
}

bool SelectionController::set_exclude_all(bool exclude) noexcept {
    if (exclude_ == exclude) {
        return false;
    }

    exclude_ = exclude;
    count_ = 0;
    has_anchor_ = false;
    anchor_ = 0;

    return true;
}

std::size_t SelectionController::find(std::uint64_t id) const noexcept {
    for (std::size_t i = 0; i < count_; ++i) {
        if (ids_[i] == id) {
            return i;
        }
    }

    return kNotFound;
}

bool SelectionController::same_set(const std::uint64_t* ids, std::size_t size) const noexcept {
    if (count_ != size) {
        return false;
    }

    for (std::size_t i = 0; i < size; ++i) {
        if (find(ids[i]) == kNotFound) {
            return false;
        }
    }

    return true;
}

void SelectionController::remove_at(std::size_t index) noexcept {
    ids_[index] = ids_[count_ - 1];
    ids_[count_ - 1] = 0;
    --count_;
}

RadioGroup::RadioGroup() noexcept {
    controller_.configure({.mode = SelectionMode::single, .allow_empty = false});
}

bool RadioGroup::allows_empty() const noexcept {
    return controller_.allows_empty();
}

bool RadioGroup::disabled() const noexcept {
    return controller_.disabled();
}

bool RadioGroup::has_selection() const noexcept {
    return controller_.count() == 1;
}

std::uint64_t RadioGroup::selected() const noexcept {
    std::uint64_t id{};

    controller_.copy_selected(std::span<std::uint64_t, 1>{&id, 1});

    return id;
}

bool RadioGroup::configure(bool allow_empty) noexcept {
    return controller_.configure({.mode = SelectionMode::single, .allow_empty = allow_empty});
}

bool RadioGroup::set_disabled(bool disabled) noexcept {
    return controller_.set_disabled(disabled);
}

bool RadioGroup::contains(std::uint64_t id) const noexcept {
    return controller_.contains(id);
}

SelectionResult
RadioGroup::select(std::uint64_t id, std::span<const std::uint64_t> members) noexcept {
    for (std::uint64_t member : members) {
        if (member == id) {
            return controller_.select(id);
        }
    }

    return SelectionResult::ignored;
}

SelectionResult RadioGroup::clear() noexcept {
    return controller_.clear();
}

SelectionResult
RadioGroup::move(RadioMove direction, std::span<const std::uint64_t> members) noexcept {
    if (members.empty()) {
        return SelectionResult::ignored;
    }

    constexpr std::size_t kNoMember = static_cast<std::size_t>(-1);
    std::size_t current = kNoMember;

    if (has_selection()) {
        const std::uint64_t active = selected();

        for (std::size_t i = 0; i < members.size(); ++i) {
            if (members[i] == active) {
                current = i;
                break;
            }
        }
    }

    std::uint64_t target;

    switch (direction) {
    case RadioMove::next:
        if (current != kNoMember) {
            target = members[(current + 1) % members.size()];
            break;
        }
        target = members.front();
        break;
    case RadioMove::prev:
        if (current != kNoMember) {
            target = members[(current + members.size() - 1) % members.size()];
            break;
        }
        target = members.back();
        break;
    case RadioMove::first:
        target = members.front();
        break;
    case RadioMove::last:
        target = members.back();
        break;
    default:
        return SelectionResult::ignored;
    }

    return select(target, members);
}

bool RadioGroup::write_semantics(
    std::uint64_t id,
    std::string_view accessible_name,
    SemanticNodeData& output
) const noexcept {
    if (accessible_name.empty()) {
        return false;
    }

    std::uint16_t states{};
    if (controller_.disabled()) {
        states = states | SemanticState::disabled;
    }
    if (contains(id)) {
        states = states | SemanticState::checked;
    }

    output = {
        .role = SemanticRole::radio,
        .label = accessible_name,
        .states = states,
        .actions = controller_.disabled() ? std::uint16_t{} : action_flag(SemanticAction::activate),
    };

    return true;
}

}
