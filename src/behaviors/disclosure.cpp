#include "veyra/behaviors/disclosure.hpp"

namespace veyra {

DisclosureState DisclosureController::state() const noexcept {
    return {
        .count = count_,
        .disabled = disabled_,
    };
}

bool DisclosureController::disabled() const noexcept {
    return disabled_;
}

bool DisclosureController::set_disabled(bool disabled) noexcept {
    if (disabled_ == disabled) {
        return false;
    }

    disabled_ = disabled;

    return true;
}

std::size_t DisclosureController::count() const noexcept {
    return count_;
}

bool DisclosureController::empty() const noexcept {
    return count_ == 0;
}

bool DisclosureController::contains(std::uint64_t id) const noexcept {
    return find(id) != kNotFound;
}

std::size_t DisclosureController::copy_expanded(std::span<std::uint64_t> output) const noexcept {
    std::size_t copied = count_;

    if (copied > output.size()) {
        copied = output.size();
    }

    for (std::size_t i = 0; i < copied; ++i) {
        output[i] = ids_[i];
    }

    return copied;
}

bool DisclosureController::write_disclosure_semantics(
    std::uint64_t id,
    std::string_view accessible_name,
    SemanticNodeData& output
) const noexcept {
    if (accessible_name.empty()) {
        return false;
    }

    const bool expanded = contains(id);

    std::uint16_t states{};
    if (disabled_) {
        states = states | SemanticState::disabled;
    }
    if (expanded) {
        states = states | SemanticState::expanded;
    }

    std::uint16_t actions{};
    if (!disabled_) {
        if (expanded) {
            actions = action_flag(SemanticAction::collapse);
        } else {
            actions = action_flag(SemanticAction::expand);
        }
    }

    output = {
        .role = SemanticRole::list_item,
        .label = accessible_name,
        .states = states,
        .actions = actions,
    };

    return true;
}

DisclosureResult DisclosureController::expand(std::uint64_t id) noexcept {
    if (disabled_) {
        return DisclosureResult::ignored;
    }

    if (contains(id)) {
        return DisclosureResult::handled;
    }

    if (count_ == kCapacity) {
        return DisclosureResult::overflow;
    }

    ids_[count_] = id;
    ++count_;

    return DisclosureResult::changed;
}

DisclosureResult DisclosureController::collapse(std::uint64_t id) noexcept {
    if (disabled_) {
        return DisclosureResult::ignored;
    }

    const std::size_t index = find(id);

    if (index == kNotFound) {
        return DisclosureResult::handled;
    }

    remove_at(index);

    return DisclosureResult::changed;
}

DisclosureResult DisclosureController::toggle_item(std::uint64_t id) noexcept {
    if (disabled_) {
        return DisclosureResult::ignored;
    }

    if (contains(id)) {
        return collapse(id);
    }

    return expand(id);
}

DisclosureResult DisclosureController::collapse_subtree(
    std::uint64_t id,
    std::span<const std::uint64_t> descendants
) noexcept {
    if (disabled_) {
        return DisclosureResult::ignored;
    }

    bool removed = false;

    const std::size_t self = find(id);

    if (self != kNotFound) {
        remove_at(self);
        removed = true;
    }

    for (std::uint64_t descendant : descendants) {
        const std::size_t index = find(descendant);

        if (index == kNotFound) {
            continue;
        }

        remove_at(index);
        removed = true;
    }

    if (!removed) {
        return DisclosureResult::handled;
    }

    return DisclosureResult::changed;
}

DisclosureResult DisclosureController::expand_all(std::span<const std::uint64_t> ids) noexcept {
    if (disabled_) {
        return DisclosureResult::ignored;
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
        return DisclosureResult::overflow;
    }

    if (unique_size == 0) {
        return DisclosureResult::handled;
    }

    std::size_t fresh = 0;

    for (std::size_t i = 0; i < unique_size; ++i) {
        if (find(unique[i]) == kNotFound) {
            ++fresh;
        }
    }

    if (fresh == 0) {
        return DisclosureResult::handled;
    }

    if (count_ + fresh > kCapacity) {
        return DisclosureResult::overflow;
    }

    for (std::size_t i = 0; i < unique_size; ++i) {
        if (find(unique[i]) == kNotFound) {
            ids_[count_] = unique[i];
            ++count_;
        }
    }

    return DisclosureResult::changed;
}

DisclosureResult DisclosureController::clear() noexcept {
    if (disabled_) {
        return DisclosureResult::ignored;
    }

    if (count_ == 0) {
        return DisclosureResult::handled;
    }

    count_ = 0;

    return DisclosureResult::changed;
}

std::size_t DisclosureController::find(std::uint64_t id) const noexcept {
    for (std::size_t i = 0; i < count_; ++i) {
        if (ids_[i] == id) {
            return i;
        }
    }

    return kNotFound;
}

void DisclosureController::remove_at(std::size_t index) noexcept {
    ids_[index] = ids_[count_ - 1];
    ids_[count_ - 1] = 0;
    --count_;
}

}
