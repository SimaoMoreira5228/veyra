#ifndef VEYRA_STYLE_COLOR_HPP
#define VEYRA_STYLE_COLOR_HPP

#include <cstdint>

namespace veyra {

struct Srgba8 {
    std::uint8_t red{};
    std::uint8_t green{};
    std::uint8_t blue{};
    std::uint8_t alpha{255};

    constexpr bool operator==(const Srgba8&) const noexcept = default;
};

}

namespace veyra::foundation {
using ::veyra::Srgba8;
}

#endif
