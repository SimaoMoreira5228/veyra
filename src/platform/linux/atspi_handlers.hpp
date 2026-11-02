#ifndef VEYRA_PLATFORM_LINUX_ATSPI_HANDLERS_HPP
#define VEYRA_PLATFORM_LINUX_ATSPI_HANDLERS_HPP

#include "veyra/platform/linux/atspi_sink.hpp"

#include <dbus/dbus.h>

#include <cstddef>

namespace veyra::atspi_detail {

::DBusMessage* handle_properties(AtspiSink::Impl& impl, DBusMessage* message, std::size_t index);
::DBusMessage* handle_accessible(AtspiSink::Impl& impl, DBusMessage* message, std::size_t index);
::DBusMessage* handle_component(AtspiSink::Impl& impl, DBusMessage* message, std::size_t index);
::DBusMessage* handle_action(AtspiSink::Impl& impl, DBusMessage* message, std::size_t index);
::DBusMessage* handle_text(AtspiSink::Impl& impl, DBusMessage* message, std::size_t index);

}

#endif
