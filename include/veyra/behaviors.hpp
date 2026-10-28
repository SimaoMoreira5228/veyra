#ifndef VEYRA_BEHAVIORS_HPP
#define VEYRA_BEHAVIORS_HPP

#include "veyra/behaviors/control/pressable.hpp"
#include "veyra/behaviors/control/progress.hpp"
#include "veyra/behaviors/control/scroll.hpp"
#include "veyra/behaviors/control/toggle.hpp"
#include "veyra/behaviors/data/datagrid.hpp"
#include "veyra/behaviors/data/file_operation_queue.hpp"
#include "veyra/behaviors/data/icon_view.hpp"
#include "veyra/behaviors/data/list_reorder.hpp"
#include "veyra/behaviors/data/log_feed.hpp"
#include "veyra/behaviors/data/treegrid.hpp"
#include "veyra/behaviors/input/editable_text.hpp"
#include "veyra/behaviors/input/radiogroup.hpp"
#include "veyra/behaviors/input/range.hpp"
#include "veyra/behaviors/input/spinbox.hpp"
#include "veyra/behaviors/navigation/breadcrumb.hpp"
#include "veyra/behaviors/navigation/combobox.hpp"
#include "veyra/behaviors/navigation/command_palette.hpp"
#include "veyra/behaviors/navigation/disclosure.hpp"
#include "veyra/behaviors/navigation/focus_chain.hpp"
#include "veyra/behaviors/navigation/keybinding.hpp"
#include "veyra/behaviors/navigation/menu.hpp"
#include "veyra/behaviors/navigation/menubar.hpp"
#include "veyra/behaviors/overlay/drag_drop.hpp"
#include "veyra/behaviors/overlay/overlay.hpp"
#include "veyra/behaviors/overlay/pull_refresh.hpp"
#include "veyra/behaviors/overlay/swipe_dismiss.hpp"
#include "veyra/behaviors/overlay/toast.hpp"
#include "veyra/behaviors/overlay/tooltip.hpp"
#include "veyra/behaviors/selection/cell_range.hpp"
#include "veyra/behaviors/selection/selection.hpp"
#include "veyra/behaviors/selection/selection_typeahead.hpp"
#include "veyra/behaviors/selection/tabs.hpp"
#include "veyra/behaviors/selection/tree_cursor.hpp"

namespace veyra::behaviors {}
namespace veyra::behaviors::control {}
namespace veyra::behaviors::data {}
namespace veyra::behaviors::input {}
namespace veyra::behaviors::navigation {}
namespace veyra::behaviors::overlay {}
namespace veyra::behaviors::selection {}

#endif
