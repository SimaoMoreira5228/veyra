#ifndef VEYRA_RENDER_DISPLAY_COMMAND_HASH_HPP
#define VEYRA_RENDER_DISPLAY_COMMAND_HASH_HPP

#include "veyra/render/cpu/software_renderer.hpp"

#include <cstdint>
#include <span>

namespace veyra {

std::uint64_t hash_display_commands(std::span<const DisplayCommand> commands) noexcept;

}

#endif
