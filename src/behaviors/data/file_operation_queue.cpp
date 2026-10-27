#include "veyra/behaviors/data/file_operation_queue.hpp"

#include <cmath>

namespace veyra {

std::size_t FileOperationQueue::count() const noexcept {
    return count_;
}

std::size_t FileOperationQueue::pending_count() const noexcept {
    auto pending = std::size_t{};

    for (auto index = std::size_t{}; index < count_; ++index) {
        if (!finished(operations_[index].state)) {
            ++pending;
        }
    }

    return pending;
}

bool FileOperationQueue::contains(std::uint64_t id) const noexcept {
    return find(id) != kNotFound;
}

bool FileOperationQueue::operation(std::uint64_t id, FileOperation& output) const noexcept {
    const auto index = find(id);

    if (index == kNotFound) {
        return false;
    }

    output = operations_[index];

    return true;
}

std::size_t FileOperationQueue::copy_operations(std::span<FileOperation> output) const noexcept {
    const auto copied = output.size() < count_ ? output.size() : count_;

    for (auto index = std::size_t{}; index < copied; ++index) {
        output[index] = operations_[index];
    }

    return copied;
}

FileOperationResult FileOperationQueue::submit(FileOperationKind kind, std::uint64_t id) noexcept {
    if (id == 0 || find(id) != kNotFound) {
        return FileOperationResult::ignored;
    }

    if (count_ >= kCapacity) {
        return FileOperationResult::overflow;
    }

    operations_[count_] = FileOperation{
        .id = id,
        .kind = kind,
        .state = FileOperationState::queued,
    };
    ++count_;

    return FileOperationResult::changed;
}

FileOperationResult FileOperationQueue::begin(std::uint64_t id) noexcept {
    const auto index = find(id);

    if (index == kNotFound) {
        return FileOperationResult::ignored;
    }

    if (operations_[index].state == FileOperationState::active) {
        return FileOperationResult::handled;
    }

    if (operations_[index].state != FileOperationState::queued) {
        return FileOperationResult::ignored;
    }

    operations_[index].state = FileOperationState::active;

    return FileOperationResult::started;
}

FileOperationResult FileOperationQueue::advance(std::uint64_t id, float fraction) noexcept {
    const auto index = find(id);

    if (index == kNotFound) {
        return FileOperationResult::ignored;
    }

    if (!std::isfinite(fraction) || fraction < 0.0F || fraction > 1.0F) {
        return FileOperationResult::ignored;
    }

    if (operations_[index].state != FileOperationState::active) {
        return FileOperationResult::ignored;
    }

    if (operations_[index].progress == fraction) {
        return FileOperationResult::handled;
    }

    operations_[index].progress = fraction;

    return FileOperationResult::changed;
}

FileOperationResult FileOperationQueue::pause(std::uint64_t id) noexcept {
    const auto index = find(id);

    if (index == kNotFound) {
        return FileOperationResult::ignored;
    }

    if (operations_[index].state == FileOperationState::paused) {
        return FileOperationResult::handled;
    }

    if (operations_[index].state != FileOperationState::active) {
        return FileOperationResult::ignored;
    }

    operations_[index].state = FileOperationState::paused;

    return FileOperationResult::changed;
}

FileOperationResult FileOperationQueue::resume(std::uint64_t id) noexcept {
    const auto index = find(id);

    if (index == kNotFound) {
        return FileOperationResult::ignored;
    }

    if (operations_[index].state == FileOperationState::active) {
        return FileOperationResult::handled;
    }

    if (operations_[index].state != FileOperationState::paused) {
        return FileOperationResult::ignored;
    }

    operations_[index].state = FileOperationState::active;

    return FileOperationResult::changed;
}

FileOperationResult FileOperationQueue::fail(std::uint64_t id, FileOperationFault fault) noexcept {
    const auto index = find(id);

    if (index == kNotFound) {
        return FileOperationResult::ignored;
    }

    const auto state = operations_[index].state;

    if (state == FileOperationState::done || state == FileOperationState::failed ||
        state == FileOperationState::canceled) {
        return FileOperationResult::handled;
    }

    if (state != FileOperationState::active && state != FileOperationState::paused) {
        return FileOperationResult::ignored;
    }

    operations_[index].state = FileOperationState::failed;
    operations_[index].conflict = fault == FileOperationFault::conflict;

    return FileOperationResult::changed;
}

FileOperationResult FileOperationQueue::retry(std::uint64_t id) noexcept {
    const auto index = find(id);

    if (index == kNotFound) {
        return FileOperationResult::ignored;
    }

    const auto state = operations_[index].state;

    if (state != FileOperationState::failed && state != FileOperationState::canceled) {
        return FileOperationResult::handled;
    }

    operations_[index].state = FileOperationState::queued;
    operations_[index].progress = 0.0F;
    operations_[index].conflict = false;
    ++operations_[index].attempts;

    return FileOperationResult::changed;
}

FileOperationResult FileOperationQueue::complete(std::uint64_t id) noexcept {
    const auto index = find(id);

    if (index == kNotFound) {
        return FileOperationResult::ignored;
    }

    const auto state = operations_[index].state;

    if (state == FileOperationState::done) {
        return FileOperationResult::handled;
    }

    if (state != FileOperationState::active && state != FileOperationState::paused) {
        return FileOperationResult::ignored;
    }

    operations_[index].state = FileOperationState::done;
    operations_[index].progress = 1.0F;

    return FileOperationResult::changed;
}

FileOperationResult FileOperationQueue::cancel(std::uint64_t id) noexcept {
    const auto index = find(id);

    if (index == kNotFound) {
        return FileOperationResult::ignored;
    }

    if (finished(operations_[index].state)) {
        return FileOperationResult::handled;
    }

    operations_[index].state = FileOperationState::canceled;

    return FileOperationResult::changed;
}

FileOperationClear FileOperationQueue::dismiss_finished() noexcept {
    auto kept = std::size_t{};
    auto removed = std::size_t{};

    for (auto index = std::size_t{}; index < count_; ++index) {
        if (finished(operations_[index].state)) {
            ++removed;

            continue;
        }

        operations_[kept] = operations_[index];
        ++kept;
    }

    count_ = kept;

    if (removed == 0) {
        return {.result = FileOperationResult::handled};
    }

    return {.result = FileOperationResult::changed, .removed = removed};
}

bool FileOperationQueue::write_list_semantics(
    std::string_view accessible_name,
    SemanticNodeData& output
) const noexcept {
    if (accessible_name.empty()) {
        return false;
    }

    output = {
        .role = SemanticRole::listbox,
        .label = accessible_name,
    };

    return true;
}

bool FileOperationQueue::write_item_semantics(
    std::uint64_t id,
    std::string_view accessible_name,
    SemanticNodeData& output
) const noexcept {
    if (accessible_name.empty() || id == 0) {
        return false;
    }

    const auto index = find(id);

    if (index == kNotFound) {
        return false;
    }

    const auto& operation = operations_[index];

    const char* status = "queued";

    switch (operation.state) {
    case FileOperationState::active:
        status = "running";
        break;
    case FileOperationState::paused:
        status = "paused";
        break;
    case FileOperationState::done:
        status = "done";
        break;
    case FileOperationState::failed:
        status = operation.conflict ? "conflict" : "failed";
        break;
    case FileOperationState::canceled:
        status = "canceled";
        break;
    case FileOperationState::queued:
        break;
    }

    output = {
        .role = SemanticRole::option,
        .label = accessible_name,
        .value = status,
        .actions =
            finished(operation.state) ? action_flag(SemanticAction::dismiss) : std::uint16_t{},
    };

    return true;
}

std::size_t FileOperationQueue::find(std::uint64_t id) const noexcept {
    for (auto index = std::size_t{}; index < count_; ++index) {
        if (operations_[index].id == id) {
            return index;
        }
    }

    return kNotFound;
}

bool FileOperationQueue::finished(FileOperationState state) noexcept {
    return state == FileOperationState::done || state == FileOperationState::failed ||
        state == FileOperationState::canceled;
}

}
