#include "veyra/behaviors/combobox.hpp"

namespace veyra {

ComboBoxController::ComboBoxController() noexcept {
    matches_.fill(0);
}

ComboState ComboBoxController::state() const noexcept {
    return {
        .mode = mode_,
        .open = open_,
        .disabled = disabled_,
        .matches = match_count_,
        .has_active = has_active_,
        .active = active_,
        .has_value = has_value_,
        .value = value_,
    };
}

ComboMode ComboBoxController::mode() const noexcept {
    return mode_;
}

bool ComboBoxController::is_open() const noexcept {
    return open_;
}

bool ComboBoxController::disabled() const noexcept {
    return disabled_;
}

bool ComboBoxController::has_value() const noexcept {
    return has_value_;
}

std::uint64_t ComboBoxController::value() const noexcept {
    return value_;
}

ComboActive ComboBoxController::active() const noexcept {
    return {
        .has = has_active_,
        .id = active_,
    };
}

std::size_t ComboBoxController::match_count() const noexcept {
    return match_count_;
}

bool ComboBoxController::contains(std::uint64_t id) const noexcept {
    return find_match(id) != kNotFound;
}

std::size_t ComboBoxController::copy_matches(std::span<std::uint64_t> output) const noexcept {
    std::size_t copied = match_count_;

    if (copied > output.size()) {
        copied = output.size();
    }

    for (std::size_t i = 0; i < copied; ++i) {
        output[i] = matches_[i];
    }

    return copied;
}

bool ComboBoxController::configure(ComboMode mode) noexcept {
    if (mode_ == mode) {
        return false;
    }

    mode_ = mode;

    return true;
}

bool ComboBoxController::set_disabled(bool disabled) noexcept {
    if (disabled_ == disabled) {
        return false;
    }

    disabled_ = disabled;

    return true;
}

bool ComboBoxController::set_value(std::uint64_t id) noexcept {
    if (id == 0) {
        return false;
    }

    if (has_value_ && value_ == id) {
        return false;
    }

    has_value_ = true;
    value_ = id;

    return true;
}

bool ComboBoxController::clear_value() noexcept {
    if (!has_value_) {
        return false;
    }

    has_value_ = false;
    value_ = 0;

    return true;
}

ComboResult
ComboBoxController::open(std::span<const ComboOption> options, std::string_view query) noexcept {
    if (disabled_) {
        return ComboResult::ignored;
    }

    const ComboResult collected = collect(query, options);

    if (collected == ComboResult::overflow) {
        return ComboResult::overflow;
    }

    const bool was_open = open_;
    open_ = true;

    if (was_open && collected == ComboResult::handled) {
        return ComboResult::handled;
    }

    return ComboResult::opened;
}

ComboResult ComboBoxController::close() noexcept {
    if (disabled_) {
        return ComboResult::ignored;
    }

    if (!open_) {
        return ComboResult::handled;
    }

    open_ = false;
    match_count_ = 0;
    has_active_ = false;
    active_ = 0;

    return ComboResult::closed;
}

ComboResult ComboBoxController::apply_filter(
    std::string_view query,
    std::span<const ComboOption> options
) noexcept {
    if (disabled_) {
        return ComboResult::ignored;
    }

    if (!open_) {
        return ComboResult::ignored;
    }

    return collect(query, options);
}

ComboResult ComboBoxController::clear_filter() noexcept {
    if (disabled_) {
        return ComboResult::ignored;
    }

    if (match_count_ == 0 && !has_active_) {
        return ComboResult::handled;
    }

    match_count_ = 0;
    has_active_ = false;
    active_ = 0;

    return ComboResult::changed;
}

ComboResult ComboBoxController::move_active(ComboMove direction) noexcept {
    if (disabled_) {
        return ComboResult::ignored;
    }

    if (!open_) {
        return ComboResult::ignored;
    }

    if (match_count_ == 0) {
        return ComboResult::ignored;
    }

    std::size_t current = kNotFound;

    if (has_active_) {
        current = find_match(active_);
    }

    std::uint64_t target;

    switch (direction) {
    case ComboMove::next:
        target = current == kNotFound ? matches_[0] : matches_[(current + 1) % match_count_];
        break;
    case ComboMove::prev:
        target = current == kNotFound ? matches_[match_count_ - 1]
                                      : matches_[(current + match_count_ - 1) % match_count_];
        break;
    case ComboMove::first:
        target = matches_[0];
        break;
    case ComboMove::last:
        target = matches_[match_count_ - 1];
        break;
    default:
        return ComboResult::ignored;
    }

    if (has_active_ && target == active_) {
        return ComboResult::handled;
    }

    has_active_ = true;
    active_ = target;

    return ComboResult::changed;
}

ComboCommit ComboBoxController::commit_active() noexcept {
    if (disabled_) {
        return {};
    }

    if (!open_) {
        return {};
    }

    if (!has_active_) {
        return {};
    }

    return finish_commit(active_, false);
}

ComboCommit
ComboBoxController::commit_id(std::uint64_t id, std::span<const ComboOption> options) noexcept {
    if (disabled_) {
        return {};
    }

    if (!open_) {
        return {};
    }

    if (id == 0) {
        return {};
    }

    for (const ComboOption& option : options) {
        if (option.id == id) {
            return finish_commit(id, false);
        }
    }

    return {};
}

ComboCommit ComboBoxController::commit_text(std::string_view query) noexcept {
    if (disabled_) {
        return {};
    }

    if (mode_ != ComboMode::free) {
        return {};
    }

    if (!open_) {
        return {};
    }

    if (query.empty()) {
        return {};
    }

    has_value_ = false;
    value_ = 0;
    open_ = false;
    match_count_ = 0;
    has_active_ = false;
    active_ = 0;

    return {
        .result = ComboResult::commit_text,
        .id = 0,
        .free_text = true,
    };
}

ComboResult ComboBoxController::dismiss() noexcept {
    if (disabled_) {
        return ComboResult::ignored;
    }

    if (!open_) {
        return ComboResult::handled;
    }

    open_ = false;
    match_count_ = 0;
    has_active_ = false;
    active_ = 0;

    return ComboResult::dismissed;
}

bool ComboBoxController::write_box_semantics(
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
    if (open_) {
        states = states | SemanticState::expanded;
    }
    if (has_value_) {
        states = states | SemanticState::selected;
    }

    output = {
        .role = SemanticRole::combobox,
        .label = accessible_name,
        .states = states,
        .actions = disabled_ ? std::uint16_t{} : action_flag(SemanticAction::expand),
    };

    return true;
}

bool ComboBoxController::write_list_semantics(
    std::string_view accessible_name,
    SemanticNodeData& output
) const noexcept {
    if (accessible_name.empty()) {
        return false;
    }

    output = {
        .role = SemanticRole::listbox,
        .label = accessible_name,
        .states = {},
        .actions = {},
    };

    return true;
}

bool ComboBoxController::write_item_semantics(
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

    std::uint16_t states{};
    if (disabled_) {
        states = states | SemanticState::disabled;
    }
    if (has_value_ && value_ == id) {
        states = states | SemanticState::selected;
    }
    if (has_active_ && active_ == id) {
        states = states | SemanticState::focused;
    }

    output = {
        .role = SemanticRole::option,
        .label = accessible_name,
        .states = states,
        .actions = disabled_ ? std::uint16_t{} : action_flag(SemanticAction::select),
    };

    return true;
}

bool ComboBoxController::matches_query(std::string_view label, std::string_view query) noexcept {
    if (query.size() > label.size()) {
        return false;
    }

    for (std::size_t start = 0; start + query.size() <= label.size(); ++start) {
        bool hit = true;

        for (std::size_t i = 0; i < query.size(); ++i) {
            if (label[start + i] != query[i]) {
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

std::size_t ComboBoxController::find_match(std::uint64_t id) const noexcept {
    for (std::size_t i = 0; i < match_count_; ++i) {
        if (matches_[i] == id) {
            return i;
        }
    }

    return kNotFound;
}

bool ComboBoxController::same_matches(const std::uint64_t* ids, std::size_t size) const noexcept {
    if (match_count_ != size) {
        return false;
    }

    for (std::size_t i = 0; i < size; ++i) {
        if (matches_[i] != ids[i]) {
            return false;
        }
    }

    return true;
}

ComboResult
ComboBoxController::collect(std::string_view query, std::span<const ComboOption> options) noexcept {
    std::array<std::uint64_t, kCapacity + 1> found{};
    std::size_t found_size = 0;

    for (const ComboOption& option : options) {
        if (option.id == 0) {
            continue;
        }

        if (!matches_query(option.label, query)) {
            continue;
        }

        if (found_size == kCapacity + 1) {
            break;
        }

        found[found_size] = option.id;
        ++found_size;
    }

    if (found_size > kCapacity) {
        return ComboResult::overflow;
    }

    const bool same_set = same_matches(found.data(), found_size);
    const bool same_active =
        (found_size == 0 && !has_active_) || (found_size > 0 && has_active_ && active_ == found[0]);

    for (std::size_t i = 0; i < found_size; ++i) {
        matches_[i] = found[i];
    }
    match_count_ = found_size;

    if (found_size == 0) {
        has_active_ = false;
        active_ = 0;
    } else {
        has_active_ = true;
        active_ = found[0];
    }

    if (same_set && same_active) {
        return ComboResult::handled;
    }

    return ComboResult::changed;
}

ComboCommit ComboBoxController::finish_commit(std::uint64_t id, bool free_text) noexcept {
    has_value_ = true;
    value_ = id;
    open_ = false;
    match_count_ = 0;
    has_active_ = false;
    active_ = 0;

    return {
        .result = ComboResult::commit,
        .id = id,
        .free_text = free_text,
    };
}

}
