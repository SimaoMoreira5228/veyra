#ifndef VEYRA_PLATFORM_ATSPI_PROTOCOL_HPP
#define VEYRA_PLATFORM_ATSPI_PROTOCOL_HPP

#include <cstdint>

namespace veyra::atspi {

// Values from the AT-SPI2 org.a11y.atspi.Role enumeration.
enum class Role : std::uint32_t {
    check_box = 7,
    column_header = 10,
    combo_box = 11,
    dialog = 16,
    frame = 23,
    image = 27,
    label = 29,
    list = 31,
    list_item = 32,
    menu = 33,
    menu_bar = 34,
    menu_item = 35,
    page_tab = 37,
    page_tab_list = 38,
    panel = 39,
    password_text = 40,
    progress_bar = 42,
    button = 43,
    radio_button = 44,
    row_header = 47,
    scroll_bar = 48,
    slider = 51,
    spin_button = 52,
    table = 55,
    table_cell = 56,
    text = 61,
    toggle_button = 62,
    application = 75,
    entry = 79,
    heading = 83,
    link = 88,
    table_row = 90,
    list_box = 98,
};

// Bit indices from the AT-SPI2 org.a11y.atspi.StateType enumeration.
enum class State : std::uint32_t {
    active = 1,
    busy = 3,
    checked = 4,
    editable = 7,
    enabled = 8,
    expandable = 9,
    expanded = 10,
    focusable = 11,
    focused = 12,
    modal = 16,
    selected = 23,
    sensitive = 24,
    showing = 25,
    visible = 30,
    indeterminate = 32,
    required = 33,
    invalid_entry = 36,
};

inline constexpr auto kRootPath = "/org/a11y/atspi/accessible/root";
inline constexpr auto kNodePrefix = "/org/a11y/atspi/accessible/n";
inline constexpr auto kFallbackPath = "/org/a11y/atspi/accessible";

}

#endif
