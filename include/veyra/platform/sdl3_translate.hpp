#ifndef VEYRA_PLATFORM_SDL3_TRANSLATE_HPP
#define VEYRA_PLATFORM_SDL3_TRANSLATE_HPP

#include "veyra/platform/window.hpp"

#include <SDL3/SDL.h>

#include <cstdint>

namespace veyra {

bool translate_sdl_event(const SDL_Event& in, WindowId window, WindowEvent& out) noexcept;

}

#endif
