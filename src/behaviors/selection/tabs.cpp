#include "veyra/behaviors/selection/tabs.hpp"

namespace veyra {

TabsController::TabsController() noexcept {
    has_selected_ = false;
    selected_ = 0;
    has_cursor_ = false;
    cursor_ = 0;
}

TabsState TabsController::state() const noexcept {
    return {
        .activation = activation_,
        .has_selected = has_selected_,
        .selected = selected_,
        .has_cursor = has_cursor_,
        .cursor = cursor_,
    };
}

TabsActivation TabsController::activation() const noexcept {
    return activation_;
}

bool TabsController::has_selected() const noexcept {
    return has_selected_;
}

std::uint64_t TabsController::selected() const noexcept {
    return selected_;
}

bool TabsController::has_cursor() const noexcept {
    return has_cursor_;
}

std::uint64_t TabsController::cursor() const noexcept {
    return cursor_;
}

bool TabsController::configure(TabsActivation activation) noexcept {
    if (activation_ == activation) {
        return false;
    }

    activation_ = activation;

    if (has_selected_) {
        has_cursor_ = true;
        cursor_ = selected_;
    } else {
        has_cursor_ = false;
        cursor_ = 0;
    }

    return true;
}

TabsResult TabsController::select(
    std::uint64_t id,
    std::span<const std::uint64_t> members,
    std::span<const std::uint64_t> disabled
) noexcept {
    if (members.empty()) {
        return TabsResult::ignored;
    }

    if (index_of(members, id) == kNotFound) {
        return TabsResult::ignored;
    }

    if (is_disabled(id, disabled)) {
        return TabsResult::ignored;
    }

    if (has_selected_ && has_cursor_ && selected_ == id && cursor_ == id) {
        return TabsResult::handled;
    }

    has_selected_ = true;
    selected_ = id;
    has_cursor_ = true;
    cursor_ = id;

    return TabsResult::changed;
}

TabsResult TabsController::move_cursor(
    TabsMove direction,
    std::span<const std::uint64_t> members,
    std::span<const std::uint64_t> disabled
) noexcept {
    if (members.empty()) {
        return TabsResult::ignored;
    }

    std::size_t start = kNotFound;

    if (has_cursor_) {
        start = index_of(members, cursor_);
    }

    if (start == kNotFound && has_selected_) {
        start = index_of(members, selected_);
    }

    std::size_t target = kNotFound;

    switch (direction) {
    case TabsMove::next:
        if (start != kNotFound) {
            target = step_enabled(members, disabled, start, true);
        } else {
            target = first_enabled(members, disabled);
        }
        break;
    case TabsMove::prev:
        if (start != kNotFound) {
            target = step_enabled(members, disabled, start, false);
        } else {
            target = last_enabled(members, disabled);
        }
        break;
    case TabsMove::first:
        target = first_enabled(members, disabled);
        break;
    case TabsMove::last:
        target = last_enabled(members, disabled);
        break;
    }

    if (target == kNotFound) {
        return TabsResult::ignored;
    }

    const std::uint64_t id = members[target];

    bool cursor_moved = false;
    bool selection_moved = false;

    if (!has_cursor_ || cursor_ != id) {
        has_cursor_ = true;
        cursor_ = id;
        cursor_moved = true;
    }

    if (activation_ == TabsActivation::automatic && (!has_selected_ || selected_ != id)) {
        has_selected_ = true;
        selected_ = id;
        selection_moved = true;
    }

    if (!cursor_moved && !selection_moved) {
        return TabsResult::handled;
    }

    return TabsResult::changed;
}

TabsResult TabsController::activate(
    std::span<const std::uint64_t> members,
    std::span<const std::uint64_t> disabled
) noexcept {
    if (!has_cursor_) {
        return TabsResult::ignored;
    }

    if (index_of(members, cursor_) == kNotFound) {
        return TabsResult::ignored;
    }

    if (is_disabled(cursor_, disabled)) {
        return TabsResult::ignored;
    }

    if (has_selected_ && selected_ == cursor_) {
        return TabsResult::handled;
    }

    has_selected_ = true;
    selected_ = cursor_;

    return TabsResult::changed;
}

TabsClose TabsController::close_request(
    std::uint64_t id,
    std::span<const std::uint64_t> members,
    std::span<const std::uint64_t> disabled
) noexcept {
    TabsClose outcome{};

    outcome.closing = id;

    const std::size_t closing_index = index_of(members, id);

    if (closing_index == kNotFound) {
        return outcome;
    }

    std::size_t fallback_index = kNotFound;

    for (std::size_t i = closing_index + 1; i < members.size(); ++i) {
        if (members[i] == id) {
            continue;
        }

        if (is_disabled(members[i], disabled)) {
            continue;
        }

        fallback_index = i;
        break;
    }

    if (fallback_index == kNotFound) {
        for (std::size_t i = closing_index; i > 0; --i) {
            const std::size_t candidate = i - 1;

            if (members[candidate] == id) {
                continue;
            }

            if (is_disabled(members[candidate], disabled)) {
                continue;
            }

            fallback_index = candidate;
            break;
        }
    }

    if (fallback_index != kNotFound) {
        outcome.has_fallback = true;
        outcome.fallback = members[fallback_index];
    }

    bool moved = false;

    if (has_selected_ && selected_ == id) {
        if (outcome.has_fallback) {
            selected_ = outcome.fallback;
        } else {
            has_selected_ = false;
            selected_ = 0;
        }

        moved = true;
    }

    if (has_cursor_ && cursor_ == id) {
        if (outcome.has_fallback) {
            cursor_ = outcome.fallback;
        } else {
            has_cursor_ = false;
            cursor_ = 0;
        }

        moved = true;
    }

    if (moved) {
        outcome.result = TabsResult::changed;
    } else {
        outcome.result = TabsResult::handled;
    }

    return outcome;
}

bool TabsController::write_tab_semantics(
    std::uint64_t id,
    std::string_view accessible_name,
    bool tab_disabled,
    SemanticNodeData& output
) const noexcept {
    if (accessible_name.empty()) {
        return false;
    }

    std::uint16_t states{};

    if (tab_disabled) {
        states = states | SemanticState::disabled;
    }

    if (has_selected_ && selected_ == id) {
        states = states | SemanticState::selected;
    }

    output = {
        .role = SemanticRole::tab,
        .label = accessible_name,
        .states = states,
        .actions = tab_disabled ? std::uint16_t{} : action_flag(SemanticAction::select),
    };

    return true;
}

bool TabsController::write_list_semantics(
    std::string_view accessible_name,
    SemanticNodeData& output
) const noexcept {
    if (accessible_name.empty()) {
        return false;
    }

    output = {
        .role = SemanticRole::tab_list,
        .label = accessible_name,
        .states = std::uint16_t{},
        .actions = std::uint16_t{},
    };

    return true;
}

bool TabsController::is_disabled(
    std::uint64_t id,
    std::span<const std::uint64_t> disabled
) noexcept {
    for (std::uint64_t candidate : disabled) {
        if (candidate == id) {
            return true;
        }
    }

    return false;
}

std::size_t
TabsController::index_of(std::span<const std::uint64_t> ids, std::uint64_t id) noexcept {
    for (std::size_t i = 0; i < ids.size(); ++i) {
        if (ids[i] == id) {
            return i;
        }
    }

    return kNotFound;
}

std::size_t TabsController::first_enabled(
    std::span<const std::uint64_t> members,
    std::span<const std::uint64_t> disabled
) noexcept {
    for (std::size_t i = 0; i < members.size(); ++i) {
        if (!is_disabled(members[i], disabled)) {
            return i;
        }
    }

    return kNotFound;
}

std::size_t TabsController::last_enabled(
    std::span<const std::uint64_t> members,
    std::span<const std::uint64_t> disabled
) noexcept {
    for (std::size_t i = members.size(); i > 0; --i) {
        if (!is_disabled(members[i - 1], disabled)) {
            return i - 1;
        }
    }

    return kNotFound;
}

std::size_t TabsController::step_enabled(
    std::span<const std::uint64_t> members,
    std::span<const std::uint64_t> disabled,
    std::size_t from,
    bool forward
) noexcept {
    for (std::size_t step = 1; step <= members.size(); ++step) {
        std::size_t candidate = from;

        if (forward) {
            candidate = (from + step) % members.size();
        } else {
            candidate = (from + members.size() - step) % members.size();
        }

        if (is_disabled(members[candidate], disabled)) {
            continue;
        }

        return candidate;
    }

    return kNotFound;
}

}
