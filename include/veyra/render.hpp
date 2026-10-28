#ifndef VEYRA_RENDER_HPP
#define VEYRA_RENDER_HPP

#include "veyra/render/cpu/software_renderer.hpp"
#include "veyra/render/damage/damage_tracker.hpp"
#include "veyra/render/damage/retained_scroll.hpp"
#include "veyra/render/damage/scroll_shift.hpp"
#include "veyra/render/display/display_batches.hpp"
#include "veyra/render/display/display_command_hash.hpp"
#include "veyra/render/display/display_equivalence.hpp"
#include "veyra/render/draw/shadow.hpp"
#include "veyra/render/present/present_choice.hpp"
#include "veyra/render/resource/bounded_image_cache.hpp"
#include "veyra/render/resource/glyph_atlas.hpp"

namespace veyra::render {}
namespace veyra::render::cpu {}
namespace veyra::render::damage {}
namespace veyra::render::display {}
namespace veyra::render::draw {}
namespace veyra::render::gpu {}
namespace veyra::render::gpu::sdl {}
namespace veyra::render::present {}
namespace veyra::render::resource {}

#endif
