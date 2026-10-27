#ifndef VEYRA_FILES_DIRECTORY_HISTORY_HPP
#define VEYRA_FILES_DIRECTORY_HISTORY_HPP

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>

namespace veyra {

enum class DirectoryHistoryResult : std::uint8_t {
    complete,
    invalid_path,
    insufficient_storage,
};

class DirectoryHistory {
  public:
    explicit DirectoryHistory(std::span<std::filesystem::path> storage) noexcept;

    DirectoryHistoryResult navigate(std::filesystem::path directory);
    bool can_go_back() const noexcept;
    bool can_go_forward() const noexcept;
    bool go_back() noexcept;
    bool go_forward() noexcept;
    const std::filesystem::path* current() const noexcept;
    void clear() noexcept;

  private:
    std::span<std::filesystem::path> storage_;
    std::size_t count_{};
    std::size_t current_index_{};
};

}

#endif
