#ifndef VEYRA_BEHAVIORS_SELECTION_TYPEAHEAD_HPP
#define VEYRA_BEHAVIORS_SELECTION_TYPEAHEAD_HPP

#include "veyra/behaviors/selection/selection.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace veyra {

class SelectionTypeahead {
  public:
    static constexpr std::size_t kPrefixCapacity = 32;

    SelectionTypeahead() noexcept;

    std::uint64_t now_ms() const noexcept;
    std::uint32_t timeout_ms() const noexcept;
    std::size_t prefix_length() const noexcept;
    std::size_t copy_prefix(std::span<char> output) const noexcept;

    bool configure(std::uint32_t timeout_ms) noexcept;
    void reset() noexcept;

    SelectionResult advance(
        char key,
        std::uint64_t now_ms,
        SelectionController& selection,
        std::span<const std::uint64_t> ids,
        std::span<const std::string_view> labels
    ) noexcept;

  private:
    static bool starts_with(std::string_view label, const char* prefix, std::size_t size) noexcept;

    static bool all_same(const char* prefix, std::size_t size, char key) noexcept;

    std::size_t first_match(
        const char* prefix,
        std::size_t size,
        std::span<const std::uint64_t> ids,
        std::span<const std::string_view> labels
    ) const noexcept;

    std::size_t cycle_match(
        char key,
        const SelectionController& selection,
        std::span<const std::uint64_t> ids,
        std::span<const std::string_view> labels
    ) const noexcept;

    std::array<char, kPrefixCapacity> prefix_{};
    std::size_t length_{};
    std::uint64_t last_ms_{};
    std::uint32_t timeout_ms_{500};
    bool has_last_{};
};

}

namespace veyra::behaviors {
using ::veyra::SelectionTypeahead;
}

namespace veyra::behaviors::selection {
using ::veyra::SelectionTypeahead;
}

#endif
