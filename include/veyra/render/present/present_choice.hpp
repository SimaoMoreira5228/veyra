#ifndef VEYRA_RENDER_PRESENT_CHOICE_HPP
#define VEYRA_RENDER_PRESENT_CHOICE_HPP

#include <cstdint>
#include <type_traits>

namespace veyra {

enum class PresentPath : std::uint8_t {
    software,
    gpu,
};

struct PresentChoice {
    PresentPath path{PresentPath::software};
    std::uint64_t generation{};
};

static_assert(std::is_trivially_copyable_v<PresentChoice>);

void select_present_path(PresentChoice& choice, PresentPath want) noexcept;

PresentPath notify_presented(PresentChoice& choice, bool gpu_ok) noexcept;

std::uint64_t present_choice_generation(const PresentChoice& choice) noexcept;

}

namespace veyra::render {
using ::veyra::PresentChoice;
using ::veyra::PresentPath;
using ::veyra::notify_presented;
using ::veyra::present_choice_generation;
using ::veyra::select_present_path;
}

namespace veyra::render::present {
using ::veyra::PresentChoice;
using ::veyra::PresentPath;
using ::veyra::notify_presented;
using ::veyra::present_choice_generation;
using ::veyra::select_present_path;
}

#endif
