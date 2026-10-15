#include "veyra/behaviors/radiogroup.hpp"

namespace veyra {

RadioGroupState RadioGroupController::state() const noexcept {
    return {
        .count = count_,
        .selected = selected_index_ == kMissing ? 0 : options_[selected_index_].id,
        .has_selection = selected_index_ != kMissing,
        .focused = focused_,
        .disabled = disabled_,
    };
}

std::uint64_t RadioGroupController::selected() const noexcept {
    return selected_index_ == kMissing ? 0 : options_[selected_index_].id;
}

bool RadioGroupController::has_selection() const noexcept {
    return selected_index_ != kMissing;
}

std::size_t RadioGroupController::count() const noexcept {
    return count_;
}

bool RadioGroupController::focused() const noexcept {
    return focused_;
}

bool RadioGroupController::disabled() const noexcept {
    return disabled_;
}

bool RadioGroupController::configure(const RadioGroupConfig& config) noexcept {
    const auto before = state();

    options_ = config.options.data();
    count_ = config.options.size();
    focused_ = false;

    selected_index_ = kMissing;

    if (config.has_selection || !config.allow_none) {
        const auto wanted = index_of(config.selected);

        if (wanted != kMissing && !options_[wanted].disabled) {
            selected_index_ = wanted;
        } else {
            selected_index_ = first_enabled();
        }
    }

    const auto after = state();

    return before.count != after.count || before.selected != after.selected ||
        before.has_selection != after.has_selection;
}

bool RadioGroupController::set_disabled(bool disabled) noexcept {
    if (disabled_ == disabled) {
        return false;
    }

    disabled_ = disabled;
    focused_ = focused_ && !disabled;

    return true;
}

RadioResult RadioGroupController::set_focused(bool focused) noexcept {
    if (focused_ == focused || (focused && disabled_)) {
        return RadioResult::ignored;
    }

    focused_ = focused;

    return RadioResult::handled;
}

RadioTransition RadioGroupController::select(std::uint64_t id) noexcept {
    if (disabled_) {
        return {};
    }

    const auto index = index_of(id);

    if (index == kMissing || options_[index].disabled) {
        return {};
    }

    return commit(index);
}

RadioTransition RadioGroupController::key_down(RadioKey key) noexcept {
    if (disabled_ || !focused_ || selected_index_ == kMissing && first_enabled() == kMissing) {
        return {};
    }

    const auto origin = selected_index_ == kMissing ? kMissing : selected_index_;

    if (origin == kMissing) {
        switch (key) {
        case RadioKey::left:
        case RadioKey::up:
        case RadioKey::end:
            return commit(last_enabled());
        case RadioKey::right:
        case RadioKey::down:
        case RadioKey::home:
            return commit(first_enabled());
        }

        return {};
    }

    switch (key) {
    case RadioKey::left:
    case RadioKey::up:
        return commit(step_from(origin, false));
    case RadioKey::right:
    case RadioKey::down:
        return commit(step_from(origin, true));
    case RadioKey::home:
        return commit(first_enabled());
    case RadioKey::end:
        return commit(last_enabled());
    }

    return {};
}

bool RadioGroupController::write_semantics(
    std::string_view accessible_name,
    std::uint64_t id,
    SemanticNodeData& output
) const noexcept {
    const auto index = index_of(id);

    if (index == kMissing || accessible_name.empty()) {
        return false;
    }

    std::uint16_t states{};

    if (options_[index].disabled) {
        states = states | SemanticState::disabled;
    }

    if (index == selected_index_) {
        states = states | SemanticState::selected;
    }

    if (focused_ && index == selected_index_) {
        states = states | SemanticState::focused;
    }

    output = SemanticNodeData{
        .role = SemanticRole::radio,
        .label = accessible_name,
        .states = states,
    };

    return true;
}

std::size_t RadioGroupController::enabled_count() const noexcept {
    auto enabled = std::size_t{};

    for (auto index = std::size_t{}; index < count_; ++index) {
        if (!options_[index].disabled) {
            ++enabled;
        }
    }

    return enabled;
}

std::size_t RadioGroupController::index_of(std::uint64_t id) const noexcept {
    for (auto index = std::size_t{}; index < count_; ++index) {
        if (options_[index].id == id) {
            return index;
        }
    }

    return kMissing;
}

std::size_t RadioGroupController::first_enabled() const noexcept {
    for (auto index = std::size_t{}; index < count_; ++index) {
        if (!options_[index].disabled) {
            return index;
        }
    }

    return kMissing;
}

std::size_t RadioGroupController::last_enabled() const noexcept {
    auto last = kMissing;

    for (auto index = std::size_t{}; index < count_; ++index) {
        if (!options_[index].disabled) {
            last = index;
        }
    }

    return last;
}

std::size_t RadioGroupController::step_from(std::size_t index, bool forward) const noexcept {
    if (enabled_count() <= 1) {
        return index;
    }

    auto next = index;

    do {
        next = forward ? next + 1 : next + count_ - 1;

        if (next >= count_) {
            next -= count_;
        }
    } while (options_[next].disabled);

    return next;
}

RadioTransition RadioGroupController::commit(std::size_t index) noexcept {
    if (index == kMissing) {
        return {};
    }

    if (index == selected_index_) {
        return {
            .result = RadioResult::handled,
            .selected = options_[index].id,
            .has_selection = true
        };
    }

    selected_index_ = index;

    return {.result = RadioResult::changed, .selected = options_[index].id, .has_selection = true};
}

}
