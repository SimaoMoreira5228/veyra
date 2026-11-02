#ifndef VEYRA_RENDER_FRAME_COMMIT_HPP
#define VEYRA_RENDER_FRAME_COMMIT_HPP

#include "veyra/render/cpu/software_renderer.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace veyra {

// NOTE: commands borrow path, glyph, mask, and image resources. The caller keeps
// them alive until the commit is presented.
struct FrameCommit {
    static constexpr std::size_t kCommandCapacity = 4096;

    std::array<DisplayCommand, kCommandCapacity> commands{};
    std::size_t command_count{};
    std::uint32_t width{};
    std::uint32_t height{};
    std::uint64_t sequence{};
};

enum class FrameCommitResult : std::uint8_t {
    complete,
    insufficient_capacity,
    invalid_size,
};

FrameCommitResult commit_frame(
    std::span<const DisplayCommand> commands,
    std::uint32_t width,
    std::uint32_t height,
    std::uint64_t sequence,
    FrameCommit& output
) noexcept;

}

namespace veyra::render {
using ::veyra::commit_frame;
using ::veyra::FrameCommit;
using ::veyra::FrameCommitResult;
}

namespace veyra::render::present {
using ::veyra::commit_frame;
using ::veyra::FrameCommit;
using ::veyra::FrameCommitResult;
}

#endif
