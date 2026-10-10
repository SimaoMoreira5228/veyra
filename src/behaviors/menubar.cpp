#include "veyra/behaviors/menubar.hpp"

namespace veyra {

MenubarController::MenubarController() noexcept {
    in_strip_ = false;
    has_focus_ = false;
    focus_ = 0;
    has_open_ = false;
    open_ = 0;
}

MenubarFocus MenubarController::state() const noexcept {
    return {
        .in_strip = in_strip_,
        .has_focus = has_focus_,
        .focus = focus_,
        .has_open = has_open_,
        .open = open_,
    };
}

bool MenubarController::in_strip() const noexcept {
    return in_strip_;
}

bool MenubarController::has_focus() const noexcept {
    return has_focus_;
}

std::uint64_t MenubarController::focus() const noexcept {
    return focus_;
}

bool MenubarController::has_open() const noexcept {
    return has_open_;
}

std::uint64_t MenubarController::open() const noexcept {
    return open_;
}

MenubarAction MenubarController::enter_strip(std::span<const MenubarTitle> titles) noexcept {
    if (in_strip_) {
        MenubarAction outcome{};

        outcome.result = MenubarResult::handled;
        outcome.id = focus_;

        return outcome;
    }

    if (has_focus_) {
        const MenubarTitle* kept = find(titles, focus_);

        if (kept != nullptr && is_selectable(*kept)) {
            in_strip_ = true;

            MenubarAction outcome{};

            outcome.result = MenubarResult::changed;
            outcome.id = focus_;

            return outcome;
        }
    }

    const std::size_t first = first_selectable(titles);

    if (first == kNotFound) {
        return {};
    }

    in_strip_ = true;
    has_focus_ = true;
    focus_ = titles[first].id;

    MenubarAction outcome{};

    outcome.result = MenubarResult::changed;
    outcome.id = focus_;

    return outcome;
}

MenubarAction MenubarController::drill() const noexcept {
    if (!in_strip_) {
        return {};
    }

    if (!has_open_) {
        return {};
    }

    MenubarAction outcome{};

    outcome.result = MenubarResult::drill;
    outcome.id = open_;

    return outcome;
}

MenubarAction MenubarController::escape() noexcept {
    if (has_open_) {
        const std::uint64_t closed = open_;

        has_open_ = false;
        open_ = 0;

        MenubarAction outcome{};

        outcome.result = MenubarResult::back_to_strip;
        outcome.id = closed;

        return outcome;
    }

    if (in_strip_) {
        in_strip_ = false;

        MenubarAction outcome{};

        outcome.result = MenubarResult::exited;
        outcome.id = 0;

        return outcome;
    }

    return {};
}

MenubarAction MenubarController::dismiss_menu() noexcept {
    if (!has_open_) {
        return {};
    }

    const std::uint64_t closed = open_;

    has_open_ = false;
    open_ = 0;

    MenubarAction outcome{};

    outcome.result = MenubarResult::back_to_strip;
    outcome.id = closed;

    return outcome;
}

MenubarSwitch
MenubarController::move(MenubarMove direction, std::span<const MenubarTitle> titles) noexcept {
    if (!in_strip_) {
        return {};
    }

    if (titles.empty()) {
        return {};
    }

    std::size_t start = kNotFound;

    if (has_focus_) {
        start = index_of(titles, focus_);
    }

    if (start != kNotFound && !is_selectable(titles[start])) {
        start = kNotFound;
    }

    std::size_t target = kNotFound;

    switch (direction) {
    case MenubarMove::next:
        if (start != kNotFound) {
            target = step_selectable(titles, start, true);
        } else {
            target = first_selectable(titles);
        }
        break;
    case MenubarMove::prev:
        if (start != kNotFound) {
            target = step_selectable(titles, start, false);
        } else {
            target = last_selectable(titles);
        }
        break;
    case MenubarMove::first:
        target = first_selectable(titles);
        break;
    case MenubarMove::last:
        target = last_selectable(titles);
        break;
    }

    if (target == kNotFound) {
        return {};
    }

    const std::uint64_t id = titles[target].id;

    if (has_open_) {
        if (has_focus_ && focus_ == id && open_ == id) {
            return {
                .result = MenubarResult::handled,
                .closed = 0,
                .opened = 0,
            };
        }

        const std::uint64_t closed = open_;

        has_focus_ = true;
        focus_ = id;
        open_ = id;

        return {
            .result = MenubarResult::switched,
            .closed = closed,
            .opened = id,
        };
    }

    if (has_focus_ && focus_ == id) {
        return {
            .result = MenubarResult::handled,
            .closed = 0,
            .opened = 0,
        };
    }

    has_focus_ = true;
    focus_ = id;

    return {
        .result = MenubarResult::changed,
        .closed = 0,
        .opened = id,
    };
}

MenubarSwitch MenubarController::open_current(std::span<const MenubarTitle> titles) noexcept {
    if (!in_strip_) {
        return {};
    }

    if (!has_focus_) {
        return {};
    }

    const MenubarTitle* current = find(titles, focus_);

    if (current == nullptr) {
        return {};
    }

    if (!is_selectable(*current)) {
        return {};
    }

    if (has_open_) {
        return {
            .result = MenubarResult::handled,
            .closed = 0,
            .opened = 0,
        };
    }

    has_open_ = true;
    open_ = focus_;

    return {
        .result = MenubarResult::opened,
        .closed = 0,
        .opened = focus_,
    };
}

MenubarSwitch
MenubarController::click(std::uint64_t id, std::span<const MenubarTitle> titles) noexcept {
    if (id == 0) {
        return {};
    }

    const MenubarTitle* target = find(titles, id);

    if (target == nullptr) {
        return {};
    }

    if (!is_selectable(*target)) {
        return {};
    }

    if (has_open_ && open_ == id) {
        has_open_ = false;
        open_ = 0;
        in_strip_ = true;
        has_focus_ = true;
        focus_ = id;

        return {
            .result = MenubarResult::back_to_strip,
            .closed = id,
            .opened = 0,
        };
    }

    if (has_open_) {
        const std::uint64_t closed = open_;

        in_strip_ = true;
        has_focus_ = true;
        focus_ = id;
        open_ = id;

        return {
            .result = MenubarResult::switched,
            .closed = closed,
            .opened = id,
        };
    }

    in_strip_ = true;
    has_focus_ = true;
    focus_ = id;
    has_open_ = true;
    open_ = id;

    return {
        .result = MenubarResult::opened,
        .closed = 0,
        .opened = id,
    };
}

MenubarSwitch MenubarController::exit_strip() noexcept {
    if (!in_strip_) {
        return {};
    }

    const std::uint64_t closed = has_open_ ? open_ : 0;

    has_open_ = false;
    open_ = 0;
    in_strip_ = false;

    return {
        .result = MenubarResult::exited,
        .closed = closed,
        .opened = 0,
    };
}

bool MenubarController::write_bar_semantics(
    std::string_view accessible_name,
    SemanticNodeData& output
) const noexcept {
    if (accessible_name.empty()) {
        return false;
    }

    output = {
        .role = SemanticRole::menubar,
        .label = accessible_name,
        .states = std::uint16_t{},
        .actions = std::uint16_t{},
    };

    return true;
}

bool MenubarController::write_title_semantics(
    const MenubarTitle& title,
    std::string_view accessible_name,
    SemanticNodeData& output
) const noexcept {
    if (accessible_name.empty()) {
        return false;
    }

    if (title.id == 0) {
        return false;
    }

    std::uint16_t states{};

    if (title.disabled) {
        states = states | SemanticState::disabled;
    }

    if (has_focus_ && focus_ == title.id) {
        states = states | SemanticState::focused;
    }

    if (has_open_ && open_ == title.id) {
        states = states | SemanticState::expanded;
    }

    std::uint16_t actions{};

    if (!title.disabled) {
        actions = action_flag(SemanticAction::expand);
    }

    output = {
        .role = SemanticRole::menu_item,
        .label = accessible_name,
        .states = states,
        .actions = actions,
    };

    return true;
}

bool MenubarController::is_selectable(const MenubarTitle& title) noexcept {
    if (title.id == 0) {
        return false;
    }

    if (title.disabled) {
        return false;
    }

    return true;
}

std::size_t
MenubarController::index_of(std::span<const MenubarTitle> titles, std::uint64_t id) noexcept {
    for (std::size_t i = 0; i < titles.size(); ++i) {
        if (titles[i].id == id) {
            return i;
        }
    }

    return kNotFound;
}

const MenubarTitle*
MenubarController::find(std::span<const MenubarTitle> titles, std::uint64_t id) noexcept {
    const std::size_t index = index_of(titles, id);

    if (index == kNotFound) {
        return nullptr;
    }

    return &titles[index];
}

std::size_t MenubarController::first_selectable(std::span<const MenubarTitle> titles) noexcept {
    for (std::size_t i = 0; i < titles.size(); ++i) {
        if (is_selectable(titles[i])) {
            return i;
        }
    }

    return kNotFound;
}

std::size_t MenubarController::last_selectable(std::span<const MenubarTitle> titles) noexcept {
    for (std::size_t i = titles.size(); i > 0; --i) {
        if (is_selectable(titles[i - 1])) {
            return i - 1;
        }
    }

    return kNotFound;
}

std::size_t MenubarController::step_selectable(
    std::span<const MenubarTitle> titles,
    std::size_t from,
    bool forward
) noexcept {
    for (std::size_t step = 1; step <= titles.size(); ++step) {
        std::size_t candidate = from;

        if (forward) {
            candidate = (from + step) % titles.size();
        } else {
            candidate = (from + titles.size() - step) % titles.size();
        }

        if (is_selectable(titles[candidate])) {
            return candidate;
        }
    }

    return kNotFound;
}

}
