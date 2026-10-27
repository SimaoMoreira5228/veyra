#include "veyra/render/common/present_choice.hpp"

namespace veyra {

void select_present_path(PresentChoice& choice, PresentPath want) noexcept {
    if (choice.path == want) {
        return;
    }

    choice.path = want;
    ++choice.generation;
}

PresentPath notify_presented(PresentChoice& choice, bool gpu_ok) noexcept {
    if (choice.path == PresentPath::gpu && !gpu_ok) {
        choice.path = PresentPath::software;
        ++choice.generation;
    }

    return choice.path;
}

std::uint64_t present_choice_generation(const PresentChoice& choice) noexcept {
    return choice.generation;
}

}
