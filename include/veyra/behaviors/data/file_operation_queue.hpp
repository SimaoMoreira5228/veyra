#ifndef VEYRA_BEHAVIORS_FILE_OPERATION_QUEUE_HPP
#define VEYRA_BEHAVIORS_FILE_OPERATION_QUEUE_HPP

#include "veyra/core/semantics/semantics.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace veyra {

enum class FileOperationKind : std::uint8_t {
    copy,
    move,
    remove,
};

enum class FileOperationState : std::uint8_t {
    queued,
    active,
    paused,
    done,
    failed,
    canceled,
};

enum class FileOperationFault : std::uint8_t {
    error,
    conflict,
};

enum class FileOperationResult : std::uint8_t {
    ignored,
    handled,
    changed,
    started,
    overflow,
};

struct FileOperation {
    std::uint64_t id{};
    FileOperationKind kind{FileOperationKind::copy};
    FileOperationState state{FileOperationState::queued};
    float progress{};
    std::uint32_t attempts{};
    bool conflict{};
};

struct FileOperationClear {
    FileOperationResult result{FileOperationResult::ignored};
    std::size_t removed{};
};

class FileOperationQueue {
  public:
    static constexpr std::size_t kCapacity = 64;

    FileOperationQueue() noexcept = default;

    std::size_t count() const noexcept;
    std::size_t pending_count() const noexcept;
    bool contains(std::uint64_t id) const noexcept;
    bool operation(std::uint64_t id, FileOperation& output) const noexcept;
    std::size_t copy_operations(std::span<FileOperation> output) const noexcept;

    FileOperationResult submit(FileOperationKind kind, std::uint64_t id) noexcept;
    FileOperationResult begin(std::uint64_t id) noexcept;
    FileOperationResult advance(std::uint64_t id, float fraction) noexcept;
    FileOperationResult pause(std::uint64_t id) noexcept;
    FileOperationResult resume(std::uint64_t id) noexcept;
    FileOperationResult fail(std::uint64_t id, FileOperationFault fault) noexcept;
    FileOperationResult retry(std::uint64_t id) noexcept;
    FileOperationResult complete(std::uint64_t id) noexcept;
    FileOperationResult cancel(std::uint64_t id) noexcept;
    FileOperationClear dismiss_finished() noexcept;

    bool
    write_list_semantics(std::string_view accessible_name, SemanticNodeData& output) const noexcept;
    bool write_item_semantics(
        std::uint64_t id,
        std::string_view accessible_name,
        SemanticNodeData& output
    ) const noexcept;

  private:
    static constexpr std::size_t kNotFound = static_cast<std::size_t>(-1);

    std::size_t find(std::uint64_t id) const noexcept;
    static bool finished(FileOperationState state) noexcept;

    std::array<FileOperation, kCapacity> operations_{};
    std::size_t count_{};
};

}

namespace veyra::behaviors {
using ::veyra::FileOperation;
using ::veyra::FileOperationClear;
using ::veyra::FileOperationFault;
using ::veyra::FileOperationKind;
using ::veyra::FileOperationQueue;
using ::veyra::FileOperationResult;
using ::veyra::FileOperationState;
}

namespace veyra::behaviors::data {
using ::veyra::FileOperation;
using ::veyra::FileOperationClear;
using ::veyra::FileOperationFault;
using ::veyra::FileOperationKind;
using ::veyra::FileOperationQueue;
using ::veyra::FileOperationResult;
using ::veyra::FileOperationState;
}

#endif
