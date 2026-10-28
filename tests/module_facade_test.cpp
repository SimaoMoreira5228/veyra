#include "veyra/behaviors.hpp"
#include "veyra/chat.hpp"
#include "veyra/data.hpp"
#include "veyra/files.hpp"
#include "veyra/foundation.hpp"
#include "veyra/inspector.hpp"
#include "veyra/layout.hpp"
#include "veyra/media.hpp"
#include "veyra/platform.hpp"
#include "veyra/render.hpp"
#include "veyra/services.hpp"
#include "veyra/style.hpp"
#include "veyra/text.hpp"
#include "veyra/widgets.hpp"

#include <type_traits>

static_assert(std::is_same_v<veyra::foundation::LayoutRect, veyra::LayoutRect>);
static_assert(std::is_same_v<veyra::foundation::SoftwarePixel, veyra::SoftwarePixel>);
static_assert(std::is_same_v<veyra::layout::FlexDirection, veyra::FlexDirection>);
static_assert(std::is_same_v<veyra::layout::flex::FlexDirection, veyra::FlexDirection>);
static_assert(std::is_same_v<veyra::render::cpu::DisplayCommand, veyra::DisplayCommand>);
static_assert(std::is_same_v<veyra::text::editing::TextEditorBuffer, veyra::TextEditorBuffer>);
static_assert(std::is_same_v<veyra::behaviors::input::EditableTextController, veyra::EditableTextController>);
static_assert(std::is_same_v<veyra::platform::core::WindowId, veyra::WindowId>);
static_assert(std::is_same_v<veyra::style::theme::Theme, veyra::Theme>);
static_assert(std::is_same_v<veyra::chat::message::ChatMessageChangeQueue, veyra::ChatMessageChangeQueue>);
static_assert(std::is_same_v<veyra::files::listing::FileListEntry, veyra::FileListEntry>);

int main() {
    return 0;
}
