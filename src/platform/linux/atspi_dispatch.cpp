#include "atspi_common.hpp"
#include "atspi_handlers.hpp"
#include "atspi_impl.hpp"

namespace veyra {

::DBusMessage* AtspiSink::Impl::handle(::DBusMessage* message) noexcept {
    std::lock_guard lock(mutex);
    const auto* path = dbus_message_get_path(message);
    const auto* interface = dbus_message_get_interface(message);
    const auto* member = dbus_message_get_member(message);
    if (path == nullptr || interface == nullptr || member == nullptr) {
        send_error(message, DBUS_ERROR_INVALID_ARGS, "missing message fields");
        return nullptr;
    }

    const auto index = index_for_path(path);
    if (index > AtspiSink::kNodeCapacity || index >= count) {
        send_error(message, DBUS_ERROR_UNKNOWN_OBJECT, "unknown accessibility object");
        return nullptr;
    }

    if (std::strcmp(interface, "org.freedesktop.DBus.Introspectable") == 0 &&
        std::strcmp(member, "Introspect") == 0) {
        auto* reply = dbus_message_new_method_return(message);
        if (reply != nullptr) {
            DBusMessageIter root;
            dbus_message_iter_init_append(reply, &root);
            const char* xml = atspi_detail::kIntrospection;
            dbus_message_iter_append_basic(&root, DBUS_TYPE_STRING, static_cast<const void*>(&xml));
        }
        return reply;
    }
    if (std::strcmp(interface, "org.freedesktop.DBus.Properties") == 0) {
        return atspi_detail::handle_properties(*this, message, index);
    }
    if (std::strcmp(interface, "org.a11y.atspi.Accessible") == 0) {
        return atspi_detail::handle_accessible(*this, message, index);
    }
    if (std::strcmp(interface, "org.a11y.atspi.Component") == 0) {
        return atspi_detail::handle_component(*this, message, index);
    }
    if (std::strcmp(interface, "org.a11y.atspi.Action") == 0) {
        return atspi_detail::handle_action(*this, message, index);
    }
    if (std::strcmp(interface, "org.a11y.atspi.Text") == 0) {
        return atspi_detail::handle_text(*this, message, index);
    }
    if (std::strcmp(interface, "org.a11y.atspi.Application") == 0 &&
        std::strcmp(member, "GetLocale") == 0) {
        auto* reply = dbus_message_new_method_return(message);
        if (reply != nullptr) {
            DBusMessageIter root;
            dbus_message_iter_init_append(reply, &root);
            const char* locale = "en";
            dbus_message_iter_append_basic(
                &root,
                DBUS_TYPE_STRING,
                static_cast<const void*>(&locale)
            );
        }
        return reply;
    }

    send_error(message, DBUS_ERROR_UNKNOWN_METHOD, "unknown accessibility method");
    return nullptr;
}

DBusHandlerResult
AtspiSink::Impl::dispatch(DBusConnection* connection, ::DBusMessage* message, void* user) noexcept {
    (void)connection;
    auto* impl = static_cast<Impl*>(user);
    if (!dbus_message_get_path(message)) {
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    }
    if (auto* reply = impl->handle(message); reply != nullptr) {
        impl->reply(message, reply);
    }

    return DBUS_HANDLER_RESULT_HANDLED;
}

}
