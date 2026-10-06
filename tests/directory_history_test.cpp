#include "veyra/files/directory_history.hpp"

#include <array>
#include <filesystem>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

void require(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

std::filesystem::path directory(std::string_view name) {
    return std::filesystem::temp_directory_path() / name;
}

void check_navigation_and_forward_branch() {
    auto storage = std::array<std::filesystem::path, 4>{};
    auto history = veyra::DirectoryHistory(storage);
    const auto first = directory("veyra-history-first");
    const auto second = directory("veyra-history-second");
    const auto third = directory("veyra-history-third");
    const auto branch = directory("veyra-history-branch");

    require(history.current() == nullptr, "empty history has no current directory");
    require(
        history.navigate(first) == veyra::DirectoryHistoryResult::complete,
        "first absolute directory enters history"
    );
    require(
        history.navigate(second) == veyra::DirectoryHistoryResult::complete,
        "second directory enters history"
    );
    require(
        history.navigate(third) == veyra::DirectoryHistoryResult::complete,
        "third directory enters history"
    );
    require(history.go_back(), "back moves to the previous directory");
    require(
        history.current() != nullptr && *history.current() == second,
        "back selects the previous path"
    );
    require(history.go_forward(), "forward returns to the next directory");
    require(
        history.current() != nullptr && *history.current() == third,
        "forward selects the next path"
    );
    require(history.go_back(), "back returns before branching");

    require(history.go_back(), "back can move to the first directory");
    require(
        history.navigate(branch) == veyra::DirectoryHistoryResult::complete,
        "new navigation starts a branch"
    );
    require(!history.go_forward(), "branching discards the old forward path");
    require(history.current() != nullptr && *history.current() == branch, "branch is current");

    history.clear();
    require(history.current() == nullptr, "clear removes the current path");
    require(!history.go_back() && !history.go_forward(), "cleared history cannot navigate");
}

void check_capacity_and_invalid_inputs() {
    auto storage = std::array<std::filesystem::path, 2>{};
    auto history = veyra::DirectoryHistory(storage);
    const auto first = directory("veyra-history-oldest");
    const auto second = directory("veyra-history-middle");
    const auto third = directory("veyra-history-newest");

    require(
        history.navigate("relative") == veyra::DirectoryHistoryResult::invalid_path,
        "relative paths are rejected without changing history"
    );
    require(history.current() == nullptr, "invalid input leaves history empty");

    require(
        history.navigate(first) == veyra::DirectoryHistoryResult::complete,
        "first path enters bounded history"
    );
    require(
        history.navigate(second) == veyra::DirectoryHistoryResult::complete,
        "second path enters bounded history"
    );
    require(
        history.navigate(third) == veyra::DirectoryHistoryResult::complete,
        "new path enters full history"
    );
    require(
        history.current() != nullptr && *history.current() == third,
        "newest path remains current"
    );
    require(history.go_back(), "back remains available after capacity eviction");
    require(
        history.current() != nullptr && *history.current() == second,
        "oldest path is evicted first"
    );
    require(!history.go_back(), "evicted oldest path cannot be reached");

    auto empty_history = veyra::DirectoryHistory(std::span<std::filesystem::path>{});
    require(
        empty_history.navigate(first) == veyra::DirectoryHistoryResult::insufficient_storage,
        "zero-capacity history rejects navigation"
    );

    auto single_path_storage = std::array<std::filesystem::path, 1>{};
    auto single_path_history = veyra::DirectoryHistory(single_path_storage);
    require(
        single_path_history.navigate(first) == veyra::DirectoryHistoryResult::complete,
        "one-slot history accepts its first path"
    );
    require(
        single_path_history.navigate(second) == veyra::DirectoryHistoryResult::complete,
        "one-slot history replaces its oldest path"
    );
    require(
        single_path_history.current() != nullptr && *single_path_history.current() == second &&
            !single_path_history.go_back(),
        "one-slot history retains only its latest path"
    );
}

}

int main() {
    try {
        check_navigation_and_forward_branch();
        check_capacity_and_invalid_inputs();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
