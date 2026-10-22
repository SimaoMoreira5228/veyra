#include "veyra/behaviors/icon_view.hpp"

namespace veyra {

bool IconRegistry::empty() const noexcept {
    return size_ == 0;
}

std::size_t IconRegistry::size() const noexcept {
    return size_;
}

std::uint64_t IconRegistry::generation() const noexcept {
    return generation_;
}

std::size_t IconRegistry::index_of(std::uint64_t id) const noexcept {
    if (id == 0) {
        return kNotFound;
    }

    for (auto cursor = std::size_t{}; cursor < size_; ++cursor) {
        if (records_[cursor].id == id) {
            return cursor;
        }
    }

    return kNotFound;
}

bool IconRegistry::contains(std::uint64_t id) const noexcept {
    return index_of(id) != kNotFound;
}

bool IconRegistry::pending(std::uint64_t id) const noexcept {
    const auto index = index_of(id);

    if (index == kNotFound) {
        return false;
    }

    return records_[index].light == nullptr && records_[index].dark == nullptr;
}

bool IconRegistry::needs_image(std::uint64_t id) const noexcept {
    return pending(id);
}

bool IconRegistry::record_for(std::uint64_t id, IconRecord& output) const noexcept {
    const auto index = index_of(id);

    if (index == kNotFound) {
        return false;
    }

    output = records_[index];

    return true;
}

bool IconRegistry::image_for(
    std::uint64_t id,
    bool dark_ui,
    const SoftwareImage*& output
) const noexcept {
    const auto index = index_of(id);

    if (index == kNotFound) {
        return false;
    }

    const auto& record = records_[index];
    const auto* preferred = dark_ui ? record.dark : record.light;
    const auto* fallback = dark_ui ? record.light : record.dark;

    output = preferred != nullptr ? preferred : fallback;

    return true;
}

bool IconRegistry::register_icon(
    std::uint64_t id,
    std::string_view description_key,
    bool tintable
) noexcept {
    if (id == 0) {
        return false;
    }

    if (index_of(id) != kNotFound) {
        return false;
    }

    if (size_ == kCapacity) {
        return false;
    }

    records_[size_] = {
        .id = id,
        .description_key = description_key,
        .tintable = tintable,
    };
    ++size_;
    ++generation_;

    return true;
}

bool IconRegistry::set_image(
    std::uint64_t id,
    const SoftwareImage* light,
    const SoftwareImage* dark
) noexcept {
    const auto index = index_of(id);

    if (index == kNotFound) {
        return false;
    }

    auto& record = records_[index];

    if (record.light == light && record.dark == dark) {
        return false;
    }

    record.light = light;
    record.dark = dark;
    ++generation_;

    return true;
}

bool IconRegistry::set_badge(std::uint64_t id, std::uint32_t count) noexcept {
    const auto index = index_of(id);

    if (index == kNotFound) {
        return false;
    }

    auto& record = records_[index];

    if (record.badge_count == count) {
        return false;
    }

    record.badge_count = count;
    ++generation_;

    return true;
}

bool IconRegistry::set_tintable(std::uint64_t id, bool tintable) noexcept {
    const auto index = index_of(id);

    if (index == kNotFound) {
        return false;
    }

    auto& record = records_[index];

    if (record.tintable == tintable) {
        return false;
    }

    record.tintable = tintable;
    ++generation_;

    return true;
}

bool IconRegistry::unregister(std::uint64_t id) noexcept {
    const auto index = index_of(id);

    if (index == kNotFound) {
        return false;
    }

    for (auto cursor = index + 1; cursor < size_; ++cursor) {
        records_[cursor - 1] = records_[cursor];
    }

    --size_;
    ++generation_;

    return true;
}

bool IconRegistry::clear() noexcept {
    if (size_ == 0) {
        return false;
    }

    size_ = 0;
    ++generation_;

    return true;
}

bool IconRegistry::write_icon_semantics(
    std::uint64_t id,
    std::string_view label,
    SemanticNodeData& output
) const noexcept {
    if (label.empty()) {
        return false;
    }

    if (!contains(id)) {
        return false;
    }

    auto states = std::uint16_t{};

    if (pending(id)) {
        states = states | SemanticState::busy;
    }

    output = {
        .role = SemanticRole::image,
        .label = label,
        .states = states,
        .actions = std::uint16_t{},
    };

    return true;
}

}
