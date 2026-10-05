#include "veyra/files/directory_history.hpp"

#include <utility>

namespace veyra {

DirectoryHistory::DirectoryHistory(std::span<std::filesystem::path> storage) noexcept :
    storage_(storage) {
}

DirectoryHistoryResult DirectoryHistory::navigate(std::filesystem::path directory) {
    if (!directory.is_absolute()) {
        return DirectoryHistoryResult::invalid_path;
    }
    if (storage_.empty()) {
        return DirectoryHistoryResult::insufficient_storage;
    }
    if (count_ != 0 && storage_[current_index_] == directory) {
        return DirectoryHistoryResult::complete;
    }

    count_ = current_index_ + 1;
    if (count_ == storage_.size()) {
        for (auto index = std::size_t{1}; index < count_; ++index) {
            storage_[index - 1] = std::move(storage_[index]);
        }
        --count_;
    }

    storage_[count_] = std::move(directory);
    current_index_ = count_;
    ++count_;
    return DirectoryHistoryResult::complete;
}

bool DirectoryHistory::go_back() noexcept {
    if (count_ == 0 || current_index_ == 0) {
        return false;
    }
    --current_index_;
    return true;
}

bool DirectoryHistory::go_forward() noexcept {
    if (current_index_ + 1 >= count_) {
        return false;
    }
    ++current_index_;
    return true;
}

const std::filesystem::path* DirectoryHistory::current() const noexcept {
    if (count_ == 0) {
        return nullptr;
    }
    return &storage_[current_index_];
}

void DirectoryHistory::clear() noexcept {
    count_ = 0;
    current_index_ = 0;
}

}
