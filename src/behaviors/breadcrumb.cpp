#include "veyra/behaviors/breadcrumb.hpp"

namespace veyra {

BreadcrumbBar::BreadcrumbBar() noexcept = default;

bool BreadcrumbBar::has_cursor() const noexcept {
    return has_cursor_;
}

std::uint64_t BreadcrumbBar::cursor() const noexcept {
    return cursor_;
}

BreadcrumbAction
BreadcrumbBar::select(std::uint64_t id, std::span<const BreadcrumbItem> trail) noexcept {
    if (id == 0) {
        return {.result = BreadcrumbResult::ignored, .id = id};
    }

    if (trail.empty()) {
        return {.result = BreadcrumbResult::ignored, .id = id};
    }

    const std::size_t index = index_of(trail, id);

    if (index == kNotFound) {
        return {.result = BreadcrumbResult::ignored, .id = id};
    }

    if (!is_selectable(trail[index])) {
        return {.result = BreadcrumbResult::ignored, .id = id};
    }

    if (has_cursor_ && cursor_ == id) {
        return {.result = BreadcrumbResult::handled, .id = id};
    }

    has_cursor_ = true;
    cursor_ = id;

    return {.result = BreadcrumbResult::changed, .id = id};
}

BreadcrumbResult BreadcrumbBar::move_cursor(
    BreadcrumbMove direction,
    std::span<const BreadcrumbItem> trail
) noexcept {
    if (trail.empty()) {
        return BreadcrumbResult::ignored;
    }

    std::size_t start = kNotFound;

    if (has_cursor_) {
        start = index_of(trail, cursor_);
    }

    std::size_t target = kNotFound;

    switch (direction) {
    case BreadcrumbMove::next:
        if (start != kNotFound) {
            target = step_selectable(trail, start, true);
        } else {
            target = first_selectable(trail);
        }
        break;
    case BreadcrumbMove::prev:
        if (start != kNotFound) {
            target = step_selectable(trail, start, false);
        } else {
            target = last_selectable(trail);
        }
        break;
    case BreadcrumbMove::first:
        target = first_selectable(trail);
        break;
    case BreadcrumbMove::last:
        target = last_selectable(trail);
        break;
    }

    if (target == kNotFound) {
        return BreadcrumbResult::ignored;
    }

    const std::uint64_t id = trail[target].id;

    if (has_cursor_ && cursor_ == id) {
        return BreadcrumbResult::handled;
    }

    has_cursor_ = true;
    cursor_ = id;

    return BreadcrumbResult::changed;
}

BreadcrumbAction BreadcrumbBar::activate(std::span<const BreadcrumbItem> trail) noexcept {
    if (!has_cursor_) {
        return {};
    }

    const std::size_t index = index_of(trail, cursor_);

    if (index == kNotFound) {
        return {.id = cursor_};
    }

    if (!is_selectable(trail[index])) {
        return {.id = cursor_};
    }

    return {.result = BreadcrumbResult::changed, .id = cursor_};
}

bool BreadcrumbBar::write_crumb_semantics(
    std::uint64_t id,
    std::string_view accessible_name,
    bool crumb_disabled,
    SemanticNodeData& output
) const noexcept {
    if (accessible_name.empty()) {
        return false;
    }

    if (id == 0) {
        return false;
    }

    std::uint16_t states{};

    if (crumb_disabled) {
        states = states | SemanticState::disabled;
    }

    if (has_cursor_ && cursor_ == id) {
        states = states | SemanticState::focused;
    }

    output = {
        .role = SemanticRole::link,
        .label = accessible_name,
        .states = states,
        .actions = crumb_disabled ? std::uint16_t{} : action_flag(SemanticAction::select),
    };

    return true;
}

bool BreadcrumbBar::write_list_semantics(
    std::string_view accessible_name,
    SemanticNodeData& output
) const noexcept {
    if (accessible_name.empty()) {
        return false;
    }

    output = {
        .role = SemanticRole::list,
        .label = accessible_name,
        .states = std::uint16_t{},
        .actions = std::uint16_t{},
    };

    return true;
}

bool BreadcrumbBar::is_selectable(const BreadcrumbItem& item) noexcept {
    if (item.id == 0) {
        return false;
    }

    return !item.disabled;
}

std::size_t
BreadcrumbBar::index_of(std::span<const BreadcrumbItem> trail, std::uint64_t id) noexcept {
    for (std::size_t i = 0; i < trail.size(); ++i) {
        if (trail[i].id == id) {
            return i;
        }
    }

    return kNotFound;
}

std::size_t BreadcrumbBar::first_selectable(std::span<const BreadcrumbItem> trail) noexcept {
    for (std::size_t i = 0; i < trail.size(); ++i) {
        if (is_selectable(trail[i])) {
            return i;
        }
    }

    return kNotFound;
}

std::size_t BreadcrumbBar::last_selectable(std::span<const BreadcrumbItem> trail) noexcept {
    for (std::size_t i = trail.size(); i > 0; --i) {
        if (is_selectable(trail[i - 1])) {
            return i - 1;
        }
    }

    return kNotFound;
}

std::size_t BreadcrumbBar::step_selectable(
    std::span<const BreadcrumbItem> trail,
    std::size_t from,
    bool forward
) noexcept {
    for (std::size_t step = 1; step <= trail.size(); ++step) {
        std::size_t candidate = from;

        if (forward) {
            candidate = (from + step) % trail.size();
        } else {
            candidate = (from + trail.size() - step) % trail.size();
        }

        if (is_selectable(trail[candidate])) {
            return candidate;
        }
    }

    return kNotFound;
}

BreadcrumbOverflow resolve_breadcrumb_overflow(
    std::span<const BreadcrumbItem> trail,
    std::size_t max_visible,
    std::span<std::uint64_t> visible_ids
) noexcept {
    if (max_visible == 0) {
        return {};
    }

    if (visible_ids.size() < trail.size()) {
        return {};
    }

    if (trail.empty()) {
        return {
            .result = BreadcrumbOverflowResult::complete,
            .visible_count = 0,
            .collapsed_count = 0,
        };
    }

    if (trail.size() <= max_visible) {
        for (std::size_t i = 0; i < trail.size(); ++i) {
            visible_ids[i] = trail[i].id;
        }

        return {
            .result = BreadcrumbOverflowResult::complete,
            .visible_count = trail.size(),
            .collapsed_count = 0,
        };
    }

    if (max_visible == 1) {
        visible_ids[0] = trail.back().id;

        return {
            .result = BreadcrumbOverflowResult::collapsed,
            .visible_count = 1,
            .collapsed_count = trail.size() - 1,
        };
    }

    visible_ids[0] = trail[0].id;

    const std::size_t tail = max_visible - 1;
    const std::size_t skip = trail.size() - tail;

    for (std::size_t i = 0; i < tail; ++i) {
        visible_ids[1 + i] = trail[skip + i].id;
    }

    return {
        .result = BreadcrumbOverflowResult::collapsed,
        .visible_count = max_visible,
        .collapsed_count = trail.size() - max_visible,
    };
}

}
