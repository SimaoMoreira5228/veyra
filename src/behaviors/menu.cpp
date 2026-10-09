#include "veyra/behaviors/menu.hpp"

namespace veyra {

MenuController::MenuController() noexcept {
    depth_ = 0;
}

bool MenuController::empty() const noexcept {
    return depth_ == 0;
}

std::size_t MenuController::depth() const noexcept {
    return depth_;
}

std::uint64_t MenuController::menu_at(std::size_t index) const noexcept {
    if (index >= depth_) {
        return 0;
    }

    return levels_[index].menu;
}

MenuActive MenuController::active() const noexcept {
    return active_at(depth_ == 0 ? 0 : depth_ - 1);
}

MenuActive MenuController::active_at(std::size_t index) const noexcept {
    if (index >= depth_) {
        return {};
    }

    const auto& level = levels_[index];

    return {
        .has = level.has_active,
        .id = level.active,
    };
}

MenuResult MenuController::open_root(std::uint64_t menu, std::span<const MenuItem> items) noexcept {
    if (menu == 0) {
        return MenuResult::ignored;
    }

    depth_ = 1;
    levels_[0].menu = menu;
    levels_[0].has_active = false;
    levels_[0].active = 0;

    const std::size_t first = first_selectable(items);
    if (first != kNotFound) {
        levels_[0].has_active = true;
        levels_[0].active = items[first].id;
    }

    return MenuResult::opened;
}

MenuResult MenuController::open_root_at(
    std::uint64_t menu,
    std::span<const MenuItem> items,
    std::uint64_t anchor
) noexcept {
    if (menu == 0) {
        return MenuResult::ignored;
    }

    depth_ = 1;
    levels_[0].menu = menu;
    levels_[0].has_active = false;
    levels_[0].active = 0;

    const MenuItem* pinned = find(items, anchor);
    if (pinned != nullptr && is_selectable(*pinned)) {
        levels_[0].has_active = true;
        levels_[0].active = anchor;
    } else {
        const std::size_t first = first_selectable(items);
        if (first != kNotFound) {
            levels_[0].has_active = true;
            levels_[0].active = items[first].id;
        }
    }

    return MenuResult::opened;
}

MenuResult MenuController::open_submenu(
    std::uint64_t id,
    std::span<const MenuItem> current,
    std::span<const MenuItem> children
) noexcept {
    if (depth_ == 0) {
        return MenuResult::ignored;
    }

    const MenuItem* item = find(current, id);
    if (item == nullptr) {
        return MenuResult::ignored;
    }

    return drill_locked(*item, children);
}

MenuClose MenuController::close_level() noexcept {
    if (depth_ == 0) {
        return {};
    }

    --depth_;
    const std::uint64_t closed = levels_[depth_].menu;
    levels_[depth_] = {};

    if (depth_ == 0) {
        return {
            .result = MenuResult::dismissed,
            .closed = closed,
        };
    }

    return {
        .result = MenuResult::closed,
        .closed = closed,
    };
}

MenuReset MenuController::close_all() noexcept {
    MenuReset outcome{};

    outcome.closed = depth_;
    depth_ = 0;
    levels_ = {};

    return outcome;
}

MenuResult MenuController::move(MenuMove direction, std::span<const MenuItem> current) noexcept {
    if (depth_ == 0) {
        return MenuResult::ignored;
    }

    if (current.empty()) {
        return MenuResult::ignored;
    }

    auto& level = levels_[depth_ - 1];
    std::size_t start = kNotFound;

    if (level.has_active) {
        start = index_of(current, level.active);
    }

    std::size_t target = kNotFound;

    switch (direction) {
    case MenuMove::next:
        if (start != kNotFound) {
            target = step_selectable(current, start, true);
        } else {
            target = first_selectable(current);
        }
        break;
    case MenuMove::prev:
        if (start != kNotFound) {
            target = step_selectable(current, start, false);
        } else {
            target = last_selectable(current);
        }
        break;
    case MenuMove::first:
        target = first_selectable(current);
        break;
    case MenuMove::last:
        target = last_selectable(current);
        break;
    }

    if (target == kNotFound) {
        return MenuResult::ignored;
    }

    const std::uint64_t id = current[target].id;

    if (level.has_active && level.active == id) {
        return MenuResult::handled;
    }

    level.has_active = true;
    level.active = id;

    return MenuResult::changed;
}

MenuResult MenuController::hover(std::uint64_t id, std::span<const MenuItem> current) noexcept {
    if (depth_ == 0) {
        return MenuResult::ignored;
    }

    const MenuItem* item = find(current, id);
    if (item == nullptr) {
        return MenuResult::ignored;
    }

    if (!is_selectable(*item)) {
        return MenuResult::ignored;
    }

    auto& level = levels_[depth_ - 1];

    if (level.has_active && level.active == id) {
        return MenuResult::handled;
    }

    level.has_active = true;
    level.active = id;

    return MenuResult::changed;
}

MenuAction MenuController::press_right(
    std::span<const MenuItem> current,
    std::span<const MenuItem> children
) noexcept {
    if (depth_ == 0) {
        return {};
    }

    const auto& level = levels_[depth_ - 1];
    if (!level.has_active) {
        return {};
    }

    const MenuItem* item = find(current, level.active);
    if (item == nullptr) {
        return {};
    }

    if (item->kind != MenuItemKind::submenu) {
        return {
            .result = MenuResult::handled,
            .id = item->id,
        };
    }

    const MenuResult drilled = drill_locked(*item, children);
    if (drilled == MenuResult::opened) {
        return {
            .result = MenuResult::opened,
            .id = item->id,
        };
    }

    return {};
}

MenuAction MenuController::activate(
    std::span<const MenuItem> current,
    std::span<const MenuItem> children
) noexcept {
    if (depth_ == 0) {
        return {};
    }

    const auto& level = levels_[depth_ - 1];
    if (!level.has_active) {
        return {};
    }

    const MenuItem* item = find(current, level.active);
    if (item == nullptr) {
        return {};
    }

    if (!is_selectable(*item)) {
        return {};
    }

    switch (item->kind) {
    case MenuItemKind::command:
        return {
            .result = MenuResult::command,
            .id = item->id,
        };
    case MenuItemKind::checkable:
        return {
            .result = MenuResult::toggle,
            .id = item->id,
        };
    case MenuItemKind::radio:
        return {
            .result = MenuResult::radio,
            .id = item->id,
        };
    case MenuItemKind::submenu: {
        const MenuResult drilled = drill_locked(*item, children);
        if (drilled == MenuResult::opened) {
            return {
                .result = MenuResult::opened,
                .id = item->id,
            };
        }

        return {};
    }
    case MenuItemKind::separator:
        return {};
    }

    return {};
}

MenuAction MenuController::outside_press() const noexcept {
    if (depth_ == 0) {
        return {};
    }

    return {
        .result = MenuResult::dismissed,
        .id = levels_[depth_ - 1].menu,
    };
}

bool MenuController::write_menu_semantics(
    std::string_view accessible_name,
    SemanticNodeData& output
) const noexcept {
    if (accessible_name.empty()) {
        return false;
    }

    output = {
        .role = SemanticRole::menu,
        .label = accessible_name,
        .states = std::uint16_t{},
        .actions = std::uint16_t{},
    };

    return true;
}

bool MenuController::write_item_semantics(
    const MenuItem& item,
    std::string_view accessible_name,
    bool checked,
    bool expanded,
    SemanticNodeData& output
) const noexcept {
    if (accessible_name.empty()) {
        return false;
    }

    if (item.kind == MenuItemKind::separator) {
        return false;
    }

    std::uint16_t states{};

    if (item.disabled) {
        states = states | SemanticState::disabled;
    }

    if ((item.kind == MenuItemKind::checkable || item.kind == MenuItemKind::radio) && checked) {
        states = states | SemanticState::checked;
    }

    if (item.kind == MenuItemKind::submenu && expanded) {
        states = states | SemanticState::expanded;
    }

    if (!empty()) {
        const auto top = active();
        if (top.has && top.id == item.id) {
            states = states | SemanticState::focused;
        }
    }

    std::uint16_t actions{};

    if (!item.disabled) {
        if (item.kind == MenuItemKind::submenu) {
            actions = action_flag(SemanticAction::expand);
        } else {
            actions = action_flag(SemanticAction::activate);
        }
    }

    output = {
        .role = SemanticRole::menu_item,
        .label = accessible_name,
        .states = states,
        .actions = actions,
    };

    return true;
}

bool MenuController::is_selectable(const MenuItem& item) noexcept {
    if (item.disabled) {
        return false;
    }

    if (item.kind == MenuItemKind::separator) {
        return false;
    }

    return true;
}

std::size_t MenuController::index_of(std::span<const MenuItem> items, std::uint64_t id) noexcept {
    for (std::size_t i = 0; i < items.size(); ++i) {
        if (items[i].id == id) {
            return i;
        }
    }

    return kNotFound;
}

const MenuItem* MenuController::find(std::span<const MenuItem> items, std::uint64_t id) noexcept {
    const std::size_t index = index_of(items, id);
    if (index == kNotFound) {
        return nullptr;
    }

    return &items[index];
}

std::size_t MenuController::first_selectable(std::span<const MenuItem> items) noexcept {
    for (std::size_t i = 0; i < items.size(); ++i) {
        if (is_selectable(items[i])) {
            return i;
        }
    }

    return kNotFound;
}

std::size_t MenuController::last_selectable(std::span<const MenuItem> items) noexcept {
    for (std::size_t i = items.size(); i > 0; --i) {
        if (is_selectable(items[i - 1])) {
            return i - 1;
        }
    }

    return kNotFound;
}

std::size_t MenuController::step_selectable(
    std::span<const MenuItem> items,
    std::size_t from,
    bool forward
) noexcept {
    for (std::size_t step = 1; step <= items.size(); ++step) {
        std::size_t candidate = from;

        if (forward) {
            candidate = (from + step) % items.size();
        } else {
            candidate = (from + items.size() - step) % items.size();
        }

        if (is_selectable(items[candidate])) {
            return candidate;
        }
    }

    return kNotFound;
}

MenuResult
MenuController::drill_locked(const MenuItem& item, std::span<const MenuItem> children) noexcept {
    if (item.kind != MenuItemKind::submenu) {
        return MenuResult::ignored;
    }

    if (item.disabled) {
        return MenuResult::ignored;
    }

    if (!item.has_children) {
        return MenuResult::ignored;
    }

    if (depth_ == 0) {
        return MenuResult::ignored;
    }

    if (depth_ == kCapacity) {
        return MenuResult::overflow;
    }

    levels_[depth_].menu = item.id;
    levels_[depth_].has_active = false;
    levels_[depth_].active = 0;

    const std::size_t first = first_selectable(children);
    if (first != kNotFound) {
        levels_[depth_].has_active = true;
        levels_[depth_].active = children[first].id;
    }

    ++depth_;

    return MenuResult::opened;
}

}
