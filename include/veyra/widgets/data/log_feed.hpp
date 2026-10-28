#ifndef VEYRA_WIDGETS_DATA_LOG_FEED_HPP
#define VEYRA_WIDGETS_DATA_LOG_FEED_HPP

#include "veyra/behaviors/data/log_feed.hpp"
#include "veyra/foundation/geometry.hpp"
#include "veyra/render/cpu/software_renderer.hpp"
#include "veyra/style/theme/theme.hpp"
#include "veyra/widgets/data/virtual_list.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

namespace veyra {

struct LogFeedRowState {
    LogFeedKind kind{LogFeedKind::info};
    bool collapsed{};
    bool selected{};
    bool current{};
    bool placeholder{};
};

struct LogFeedRowAppearance {
    Srgba8 background;
    Srgba8 separator;
    Srgba8 current_outline;
    Srgba8 badge;
};

LogFeedRowAppearance log_feed_row_appearance(const Theme& theme, LogFeedRowState state) noexcept;

enum class LogFeedEmitResult : std::uint8_t {
    complete,
    invalid_input,
    insufficient_output,
};

struct LogFeedRowsOutput {
    std::size_t placed{};
    std::size_t required{};
    LogFeedEmitResult result{LogFeedEmitResult::complete};
};

LogFeedRowsOutput emit_log_feed_rows(
    const Theme& theme,
    std::span<const LogFeedRowState> rows,
    std::span<const LayoutRect> badges,
    const VirtualListRowsGeometry& geometry,
    std::span<DisplayCommand> commands_out
) noexcept;

}

namespace veyra::widgets {
using ::veyra::LogFeedEmitResult;
using ::veyra::LogFeedRowAppearance;
using ::veyra::LogFeedRowState;
using ::veyra::LogFeedRowsOutput;
using ::veyra::emit_log_feed_rows;
using ::veyra::log_feed_row_appearance;
}

namespace veyra::widgets::data {
using ::veyra::LogFeedEmitResult;
using ::veyra::LogFeedRowAppearance;
using ::veyra::LogFeedRowState;
using ::veyra::LogFeedRowsOutput;
using ::veyra::emit_log_feed_rows;
using ::veyra::log_feed_row_appearance;
}

#endif
