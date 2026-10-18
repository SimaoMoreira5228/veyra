#include "veyra/behaviors/selection_typeahead.hpp"

namespace veyra {

SelectionTypeahead::SelectionTypeahead() noexcept = default;

std::uint64_t SelectionTypeahead::now_ms() const noexcept {
    return last_ms_;
}

std::uint32_t SelectionTypeahead::timeout_ms() const noexcept {
    return timeout_ms_;
}

std::size_t SelectionTypeahead::prefix_length() const noexcept {
    return length_;
}

std::size_t SelectionTypeahead::copy_prefix(std::span<char> output) const noexcept {
    std::size_t copied = length_;

    if (copied > output.size()) {
        copied = output.size();
    }

    for (std::size_t i = 0; i < copied; ++i) {
        output[i] = prefix_[i];
    }

    return copied;
}

bool SelectionTypeahead::configure(std::uint32_t timeout_ms) noexcept {
    if (timeout_ms_ == timeout_ms) {
        return false;
    }

    timeout_ms_ = timeout_ms;
    length_ = 0;
    has_last_ = false;
    last_ms_ = 0;

    return true;
}

void SelectionTypeahead::reset() noexcept {
    length_ = 0;
    has_last_ = false;
    last_ms_ = 0;
}

SelectionResult SelectionTypeahead::advance(
    char key,
    std::uint64_t now_ms,
    SelectionController& selection,
    std::span<const std::uint64_t> ids,
    std::span<const std::string_view> labels
) noexcept {
    if (selection.disabled()) {
        return SelectionResult::ignored;
    }

    if (ids.size() != labels.size()) {
        return SelectionResult::ignored;
    }

    if (ids.empty()) {
        return SelectionResult::handled;
    }

    bool expired = true;

    if (has_last_) {
        expired = now_ms - last_ms_ > timeout_ms_;
    }

    last_ms_ = now_ms;
    has_last_ = true;

    if (expired) {
        length_ = 0;
    }

    if (length_ == kPrefixCapacity) {
        length_ = 0;
    }

    prefix_[length_] = key;
    ++length_;

    std::size_t match = kPrefixCapacity + 1;

    if (all_same(prefix_.data(), length_, key) && length_ > 1) {
        match = cycle_match(key, selection, ids, labels);
    }

    if (match == kPrefixCapacity + 1) {
        match = first_match(prefix_.data(), length_, ids, labels);
    }

    if (match == ids.size()) {
        return SelectionResult::handled;
    }

    selection.set_current(ids[match]);

    return selection.select(ids[match]);
}

bool SelectionTypeahead::starts_with(
    std::string_view label,
    const char* prefix,
    std::size_t size
) noexcept {
    if (label.size() < size) {
        return false;
    }

    for (std::size_t i = 0; i < size; ++i) {
        if (label[i] != prefix[i]) {
            return false;
        }
    }

    return true;
}

bool SelectionTypeahead::all_same(const char* prefix, std::size_t size, char key) noexcept {
    for (std::size_t i = 0; i < size; ++i) {
        if (prefix[i] != key) {
            return false;
        }
    }

    return true;
}

std::size_t SelectionTypeahead::first_match(
    const char* prefix,
    std::size_t size,
    std::span<const std::uint64_t> ids,
    std::span<const std::string_view> labels
) const noexcept {
    for (std::size_t i = 0; i < ids.size(); ++i) {
        if (starts_with(labels[i], prefix, size)) {
            return i;
        }
    }

    return ids.size();
}

std::size_t SelectionTypeahead::cycle_match(
    char key,
    const SelectionController& selection,
    std::span<const std::uint64_t> ids,
    std::span<const std::string_view> labels
) const noexcept {
    std::size_t start = 0;

    if (selection.has_current()) {
        const std::uint64_t current = selection.current();

        for (std::size_t i = 0; i < ids.size(); ++i) {
            if (ids[i] == current) {
                start = i + 1;
                break;
            }
        }
    }

    for (std::size_t step = 0; step < ids.size(); ++step) {
        const std::size_t index = (start + step) % ids.size();

        if (starts_with(labels[index], &key, 1)) {
            return index;
        }
    }

    return ids.size();
}

}
