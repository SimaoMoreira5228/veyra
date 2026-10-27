#ifndef VEYRA_WIDGETS_DATA_VIRTUAL_LIST_HPP
#define VEYRA_WIDGETS_DATA_VIRTUAL_LIST_HPP

#include "veyra/layout/core/geometry.hpp"
#include "veyra/render/cpu/software_renderer.hpp"
#include "veyra/style/theme/theme.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

namespace veyra {

struct VirtualListRowState {
    bool selected{};
    bool current{};
    bool placeholder{};
};

struct VirtualListRowAppearance {
    Srgba8 background;
    Srgba8 separator;
    Srgba8 current_outline;
};

VirtualListRowAppearance
virtual_list_row_appearance(const Theme& theme, VirtualListRowState state) noexcept;

struct VirtualListRowsGeometry {
    LayoutRect list_bounds{};
    float row_height{};
    float sticky_header_height{};
};

enum class VirtualListEmitResult : std::uint8_t {
    complete,
    invalid_input,
    insufficient_output,
};

struct VirtualListRowsOutput {
    std::size_t placed{};
    std::size_t required{};
    VirtualListEmitResult result{VirtualListEmitResult::complete};
};

VirtualListRowsOutput emit_virtual_list_rows(
    const Theme& theme,
    std::span<const VirtualListRowState> rows,
    const VirtualListRowsGeometry& geometry,
    std::span<DisplayCommand> commands_out
) noexcept;

}

#endif
