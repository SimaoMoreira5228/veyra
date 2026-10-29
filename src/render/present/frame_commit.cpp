#include "veyra/render/present/frame_commit.hpp"

#include <algorithm>

namespace veyra {

FrameCommitResult commit_frame(
    std::span<const DisplayCommand> commands,
    std::uint32_t width,
    std::uint32_t height,
    std::uint64_t sequence,
    FrameCommit& output
) noexcept {
    if (width == 0 || height == 0) {
        return FrameCommitResult::invalid_size;
    }
    if (commands.size() > output.commands.size()) {
        return FrameCommitResult::insufficient_capacity;
    }

    std::copy(commands.begin(), commands.end(), output.commands.begin());
    output.command_count = commands.size();
    output.width = width;
    output.height = height;
    output.sequence = sequence;
    return FrameCommitResult::complete;
}

}
