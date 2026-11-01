#include "veyra/behaviors/navigation/focus_chain.hpp"

namespace veyra {

FocusChain::FocusChain() noexcept {
    has_focus_ = false;
    focused_ = 0;
    generation_ = 0;
}

std::size_t FocusChain::count() const noexcept {
    return count_;
}

bool FocusChain::empty() const noexcept {
    return count_ == 0;
}

bool FocusChain::contains(std::uint64_t id) const noexcept {
    return index_of(id) != kNotFound;
}

bool FocusChain::is_enabled(std::uint64_t id) const noexcept {
    const std::size_t at = index_of(id);

    if (at == kNotFound) {
        return false;
    }

    return entries_[at].item_enabled;
}

std::uint64_t FocusChain::focused() const noexcept {
    return focused_;
}

bool FocusChain::has_focus() const noexcept {
    return has_focus_;
}

std::uint64_t FocusChain::generation() const noexcept {
    return generation_;
}

bool FocusChain::register_item(std::uint64_t id, bool item_enabled) noexcept {
    if (id == 0) {
        return false;
    }

    const std::size_t at = index_of(id);

    if (at != kNotFound) {
        if (entries_[at].item_enabled == item_enabled) {
            return false;
        }

        entries_[at].item_enabled = item_enabled;

        if (!item_enabled && has_focus_ && focused_ == id) {
            move_focus_to_fallback(at + 1, at);
        }

        ++generation_;

        return true;
    }

    if (count_ == kCapacity) {
        return false;
    }

    entries_[count_] = {.id = id, .item_enabled = item_enabled};
    ++count_;
    ++generation_;

    return true;
}

bool FocusChain::unregister(std::uint64_t id) noexcept {
    if (id == 0) {
        return false;
    }

    const std::size_t at = index_of(id);

    if (at == kNotFound) {
        return false;
    }

    const bool was_focused = has_focus_ && focused_ == id;

    for (std::size_t i = at + 1; i < count_; ++i) {
        entries_[i - 1] = entries_[i];
    }

    --count_;

    if (was_focused) {
        move_focus_to_fallback(at, at);
    }

    ++generation_;

    return true;
}

bool FocusChain::set_enabled(std::uint64_t id, bool on) noexcept {
    const std::size_t at = index_of(id);

    if (at == kNotFound) {
        return false;
    }

    if (entries_[at].item_enabled == on) {
        return false;
    }

    entries_[at].item_enabled = on;

    if (!on && has_focus_ && focused_ == id) {
        move_focus_to_fallback(at + 1, at);
    }

    ++generation_;

    return true;
}

bool FocusChain::focus(std::uint64_t id) noexcept {
    if (id == 0) {
        return false;
    }

    const std::size_t at = index_of(id);

    if (at == kNotFound) {
        return false;
    }

    if (!entries_[at].item_enabled) {
        return false;
    }

    if (has_focus_ && focused_ == id) {
        return false;
    }

    has_focus_ = true;
    focused_ = id;
    ++generation_;

    return true;
}

bool FocusChain::focus_next() noexcept {
    if (count_ == 0) {
        return false;
    }

    if (!has_focus_) {
        return focus_first();
    }

    const std::size_t at = index_of(focused_);

    if (at == kNotFound) {
        return focus_first();
    }

    const std::size_t target = step_enabled(at, true);

    if (target == kNotFound || target == at) {
        return false;
    }

    focused_ = entries_[target].id;
    ++generation_;

    return true;
}

bool FocusChain::focus_prev() noexcept {
    if (count_ == 0) {
        return false;
    }

    if (!has_focus_) {
        return focus_last();
    }

    const std::size_t at = index_of(focused_);

    if (at == kNotFound) {
        return focus_last();
    }

    const std::size_t target = step_enabled(at, false);

    if (target == kNotFound || target == at) {
        return false;
    }

    focused_ = entries_[target].id;
    ++generation_;

    return true;
}

bool FocusChain::focus_first() noexcept {
    const std::size_t first = first_enabled();

    if (first == kNotFound) {
        return false;
    }

    if (has_focus_ && focused_ == entries_[first].id) {
        return false;
    }

    has_focus_ = true;
    focused_ = entries_[first].id;
    ++generation_;

    return true;
}

bool FocusChain::focus_last() noexcept {
    const std::size_t last = last_enabled();

    if (last == kNotFound) {
        return false;
    }

    if (has_focus_ && focused_ == entries_[last].id) {
        return false;
    }

    has_focus_ = true;
    focused_ = entries_[last].id;
    ++generation_;

    return true;
}

FocusRestoreResult FocusChain::restore(std::uint64_t id) noexcept {
    if (id != 0 && is_enabled(id)) {
        focus(id);
        return FocusRestoreResult::restored;
    }

    if (first_enabled() == kNotFound) {
        return FocusRestoreResult::unavailable;
    }

    focus_first();
    return FocusRestoreResult::fallback;
}

std::size_t FocusChain::index_of(std::uint64_t id) const noexcept {
    for (std::size_t i = 0; i < count_; ++i) {
        if (entries_[i].id == id) {
            return i;
        }
    }

    return kNotFound;
}

std::size_t FocusChain::first_enabled() const noexcept {
    for (std::size_t i = 0; i < count_; ++i) {
        if (entries_[i].item_enabled) {
            return i;
        }
    }

    return kNotFound;
}

std::size_t FocusChain::last_enabled() const noexcept {
    for (std::size_t i = count_; i > 0; --i) {
        if (entries_[i - 1].item_enabled) {
            return i - 1;
        }
    }

    return kNotFound;
}

std::size_t FocusChain::step_enabled(std::size_t from, bool forward) const noexcept {
    for (std::size_t step = 1; step <= count_; ++step) {
        std::size_t candidate = from;

        if (forward) {
            candidate = (from + step) % count_;
        } else {
            candidate = (from + count_ - step) % count_;
        }

        if (entries_[candidate].item_enabled) {
            return candidate;
        }
    }

    return kNotFound;
}

void FocusChain::move_focus_to_fallback(
    std::size_t forward_start,
    std::size_t backward_start
) noexcept {
    for (std::size_t i = forward_start; i < count_; ++i) {
        if (entries_[i].item_enabled) {
            focused_ = entries_[i].id;

            return;
        }
    }

    for (std::size_t i = backward_start; i > 0; --i) {
        if (entries_[i - 1].item_enabled) {
            focused_ = entries_[i - 1].id;

            return;
        }
    }

    has_focus_ = false;
    focused_ = 0;
}

}
