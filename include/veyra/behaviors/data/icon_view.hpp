#ifndef VEYRA_BEHAVIORS_ICON_VIEW_HPP
#define VEYRA_BEHAVIORS_ICON_VIEW_HPP

#include "veyra/core/semantics/semantics.hpp"
#include "veyra/render/cpu/software_renderer.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace veyra {

struct IconRecord {
    std::uint64_t id{};
    const SoftwareImage* light{};
    const SoftwareImage* dark{};
    std::string_view description_key{};
    std::uint32_t badge_count{};
    bool tintable{};
};

class IconRegistry {
  public:
    static constexpr std::size_t kCapacity = 256;

    IconRegistry() noexcept = default;

    bool empty() const noexcept;
    std::size_t size() const noexcept;
    std::uint64_t generation() const noexcept;
    bool contains(std::uint64_t id) const noexcept;
    bool pending(std::uint64_t id) const noexcept;
    bool needs_image(std::uint64_t id) const noexcept;
    bool record_for(std::uint64_t id, IconRecord& output) const noexcept;
    bool image_for(std::uint64_t id, bool dark_ui, const SoftwareImage*& output) const noexcept;

    bool register_icon(std::uint64_t id, std::string_view description_key, bool tintable) noexcept;
    bool
    set_image(std::uint64_t id, const SoftwareImage* light, const SoftwareImage* dark) noexcept;
    bool set_badge(std::uint64_t id, std::uint32_t count) noexcept;
    bool set_tintable(std::uint64_t id, bool tintable) noexcept;
    bool unregister(std::uint64_t id) noexcept;
    bool clear() noexcept;

    bool write_icon_semantics(
        std::uint64_t id,
        std::string_view label,
        SemanticNodeData& output
    ) const noexcept;

  private:
    static constexpr std::size_t kNotFound = static_cast<std::size_t>(-1);

    std::size_t index_of(std::uint64_t id) const noexcept;

    std::array<IconRecord, kCapacity> records_{};
    std::size_t size_{};
    std::uint64_t generation_{};
};

}

namespace veyra::behaviors {
using ::veyra::IconRecord;
using ::veyra::IconRegistry;
}

namespace veyra::behaviors::data {
using ::veyra::IconRecord;
using ::veyra::IconRegistry;
}

#endif
