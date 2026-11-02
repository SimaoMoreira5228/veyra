#ifndef VEYRA_PLATFORM_LINUX_ATSPI_COMMON_HPP
#define VEYRA_PLATFORM_LINUX_ATSPI_COMMON_HPP

#include "atspi_impl.hpp"

#include <cstdint>
#include <cstring>
#include <string_view>

namespace veyra::atspi_detail {

inline std::string_view
text_of(const std::array<char, 96>& storage, std::uint32_t length) noexcept {
    return {storage.data(), length};
}

inline atspi::Role atspi_role(SemanticRole role) noexcept {
    switch (role) {
    case SemanticRole::button:
        return atspi::Role::button;
    case SemanticRole::checkbox:
        return atspi::Role::check_box;
    case SemanticRole::text:
        return atspi::Role::label;
    case SemanticRole::text_input:
        return atspi::Role::entry;
    case SemanticRole::heading:
        return atspi::Role::heading;
    case SemanticRole::link:
        return atspi::Role::link;
    case SemanticRole::image:
        return atspi::Role::image;
    case SemanticRole::list:
        return atspi::Role::list;
    case SemanticRole::list_item:
        return atspi::Role::list_item;
    case SemanticRole::slider:
        return atspi::Role::slider;
    case SemanticRole::progress_indicator:
        return atspi::Role::progress_bar;
    case SemanticRole::tab:
        return atspi::Role::page_tab;
    case SemanticRole::tab_list:
        return atspi::Role::page_tab_list;
    case SemanticRole::menu:
        return atspi::Role::menu;
    case SemanticRole::menu_item:
        return atspi::Role::menu_item;
    case SemanticRole::menubar:
        return atspi::Role::menu_bar;
    case SemanticRole::combobox:
        return atspi::Role::combo_box;
    case SemanticRole::listbox:
        return atspi::Role::list_box;
    case SemanticRole::option:
        return atspi::Role::list_item;
    case SemanticRole::dialog:
        return atspi::Role::dialog;
    case SemanticRole::window:
        return atspi::Role::frame;
    case SemanticRole::switch_:
        return atspi::Role::toggle_button;
    case SemanticRole::radio:
        return atspi::Role::radio_button;
    case SemanticRole::column_header:
        return atspi::Role::column_header;
    case SemanticRole::scrollbar:
        return atspi::Role::scroll_bar;
    case SemanticRole::spinbutton:
        return atspi::Role::spin_button;
    case SemanticRole::grid:
        return atspi::Role::table;
    case SemanticRole::row:
        return atspi::Role::table_row;
    case SemanticRole::row_header:
        return atspi::Role::row_header;
    case SemanticRole::grid_cell:
        return atspi::Role::table_cell;
    case SemanticRole::none:
    case SemanticRole::generic:
        break;
    }

    return atspi::Role::panel;
}

inline const char* atspi_role_name(SemanticRole role) noexcept {
    switch (role) {
    case SemanticRole::button:
        return "push button";
    case SemanticRole::checkbox:
        return "check box";
    case SemanticRole::text:
        return "label";
    case SemanticRole::text_input:
        return "text";
    case SemanticRole::heading:
        return "heading";
    case SemanticRole::link:
        return "link";
    case SemanticRole::image:
        return "image";
    case SemanticRole::list:
        return "list";
    case SemanticRole::list_item:
        return "list item";
    case SemanticRole::slider:
        return "slider";
    case SemanticRole::progress_indicator:
        return "progress bar";
    case SemanticRole::tab:
        return "page tab";
    case SemanticRole::tab_list:
        return "page tab list";
    case SemanticRole::menu:
        return "menu";
    case SemanticRole::menu_item:
        return "menu item";
    case SemanticRole::menubar:
        return "menu bar";
    case SemanticRole::combobox:
        return "combo box";
    case SemanticRole::listbox:
        return "list box";
    case SemanticRole::option:
        return "list item";
    case SemanticRole::dialog:
        return "dialog";
    case SemanticRole::window:
        return "frame";
    case SemanticRole::switch_:
        return "toggle button";
    case SemanticRole::radio:
        return "radio button";
    case SemanticRole::column_header:
        return "column header";
    case SemanticRole::scrollbar:
        return "scroll bar";
    case SemanticRole::spinbutton:
        return "spin button";
    case SemanticRole::grid:
        return "table";
    case SemanticRole::row:
        return "table row";
    case SemanticRole::row_header:
        return "row header";
    case SemanticRole::grid_cell:
        return "table cell";
    case SemanticRole::none:
    case SemanticRole::generic:
        break;
    }

    return "panel";
}

inline void set_state(std::array<std::uint32_t, 2>& words, atspi::State bit) noexcept {
    const auto value = static_cast<std::uint32_t>(bit);
    const auto index = static_cast<std::size_t>(value) / 32;
    if (index < words.size()) {
        words[index] |= 1U << (value % 32U);
    }
}

inline std::array<std::uint32_t, 2> atspi_states(const AtspiNode& record) noexcept {
    auto words = std::array<std::uint32_t, 2>{};
    set_state(words, atspi::State::sensitive);
    set_state(words, atspi::State::showing);
    set_state(words, atspi::State::visible);

    if (!has_state(record.states, SemanticState::disabled)) {
        set_state(words, atspi::State::enabled);
    }
    if (has_state(record.states, SemanticState::selected)) {
        set_state(words, atspi::State::selected);
    }
    if (has_state(record.states, SemanticState::checked)) {
        set_state(words, atspi::State::checked);
    }
    if (has_state(record.states, SemanticState::expanded)) {
        set_state(words, atspi::State::expanded);
    }
    if (has_state(record.states, SemanticState::busy)) {
        set_state(words, atspi::State::busy);
    }
    if (!has_state(record.states, SemanticState::read_only)) {
        set_state(words, atspi::State::editable);
    }
    if (has_state(record.states, SemanticState::required)) {
        set_state(words, atspi::State::required);
    }
    if (has_state(record.states, SemanticState::invalid)) {
        set_state(words, atspi::State::invalid_entry);
    }
    if (has_state(record.states, SemanticState::focused)) {
        set_state(words, atspi::State::focused);
    }
    if (has_state(record.states, SemanticState::focusable)) {
        set_state(words, atspi::State::focusable);
    }
    if (has_state(record.states, SemanticState::mixed)) {
        set_state(words, atspi::State::indeterminate);
    }
    if (has_state(record.states, SemanticState::modal)) {
        set_state(words, atspi::State::modal);
    }

    return words;
}

inline bool is_text_role(SemanticRole role) noexcept {
    return role == SemanticRole::text || role == SemanticRole::text_input ||
        role == SemanticRole::heading || role == SemanticRole::link;
}

inline void append_string_variant(DBusMessageIter* iter, const char* value) noexcept {
    DBusMessageIter variant;
    dbus_message_iter_open_container(iter, DBUS_TYPE_VARIANT, "s", &variant);
    dbus_message_iter_append_basic(&variant, DBUS_TYPE_STRING, static_cast<const void*>(&value));
    dbus_message_iter_close_container(iter, &variant);
}

inline void append_int_variant(DBusMessageIter* iter, dbus_int32_t value) noexcept {
    DBusMessageIter variant;
    dbus_message_iter_open_container(iter, DBUS_TYPE_VARIANT, "i", &variant);
    dbus_message_iter_append_basic(&variant, DBUS_TYPE_INT32, &value);
    dbus_message_iter_close_container(iter, &variant);
}

inline void append_uint_variant(DBusMessageIter* iter, dbus_uint32_t value) noexcept {
    DBusMessageIter variant;
    dbus_message_iter_open_container(iter, DBUS_TYPE_VARIANT, "u", &variant);
    dbus_message_iter_append_basic(&variant, DBUS_TYPE_UINT32, &value);
    dbus_message_iter_close_container(iter, &variant);
}

constexpr char kIntrospection[] =
    R"XML(<!DOCTYPE node PUBLIC "-//freedesktop//DTD D-BUS Object Introspection 1.0//EN" "http://www.freedesktop.org/standards/dbus/1.0/introspect.dtd">
<node>
  <interface name="org.a11y.atspi.Accessible">
    <property name="Name" type="s" access="read"/>
    <property name="Description" type="s" access="read"/>
    <property name="Parent" type="(so)" access="read"/>
    <property name="ChildCount" type="i" access="read"/>
    <property name="Locale" type="s" access="read"/>
    <property name="AccessibleId" type="s" access="read"/>
    <method name="GetChildAtIndex">
      <arg direction="in" name="index" type="i"/>
      <arg direction="out" type="(so)"/>
    </method>
    <method name="GetChildren"><arg direction="out" type="a(so)"/></method>
    <method name="GetIndexInParent"><arg direction="out" type="i"/></method>
    <method name="GetRole"><arg direction="out" type="u"/></method>
    <method name="GetRoleName"><arg direction="out" type="s"/></method>
    <method name="GetLocalizedRoleName"><arg direction="out" type="s"/></method>
    <method name="GetState"><arg direction="out" type="au"/></method>
    <method name="GetAttributes"><arg direction="out" type="a{ss}"/></method>
    <method name="GetInterfaces"><arg direction="out" type="as"/></method>
  </interface>
  <interface name="org.a11y.atspi.Component">
    <property name="version" type="u" access="read"/>
    <method name="Contains">
      <arg direction="in" name="x" type="i"/>
      <arg direction="in" name="y" type="i"/>
      <arg direction="in" name="coord_type" type="u"/>
      <arg direction="out" type="b"/>
    </method>
    <method name="GetExtents">
      <arg direction="in" name="coord_type" type="u"/>
      <arg direction="out" type="(iiii)"/>
    </method>
    <method name="GetPosition">
      <arg direction="in" name="coord_type" type="u"/>
      <arg direction="out" name="x" type="i"/>
      <arg direction="out" name="y" type="i"/>
    </method>
    <method name="GetSize">
      <arg direction="out" name="width" type="i"/>
      <arg direction="out" name="height" type="i"/>
    </method>
  </interface>
</node>
)XML";

}

#endif
