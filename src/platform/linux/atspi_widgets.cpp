#include "atspi_common.hpp"
#include "atspi_handlers.hpp"

#include <algorithm>
#include <cstddef>
#include <cstring>

namespace veyra::atspi_detail {

::DBusMessage* handle_component(AtspiSink::Impl& impl, ::DBusMessage* message, std::size_t index) {
    const auto* member = dbus_message_get_member(message);
    const auto& record = impl.nodes[index];
    auto* reply = dbus_message_new_method_return(message);
    if (reply == nullptr) {
        return nullptr;
    }
    DBusMessageIter root;
    dbus_message_iter_init_append(reply, &root);

    if (std::strcmp(member, "GetExtents") == 0) {
        DBusMessageIter structure;
        dbus_message_iter_open_container(&root, DBUS_TYPE_STRUCT, nullptr, &structure);
        dbus_int32_t values[4]{
            static_cast<dbus_int32_t>(record.bounds.x),
            static_cast<dbus_int32_t>(record.bounds.y),
            static_cast<dbus_int32_t>(record.bounds.width),
            static_cast<dbus_int32_t>(record.bounds.height),
        };
        for (const auto value : values) {
            dbus_message_iter_append_basic(&structure, DBUS_TYPE_INT32, &value);
        }
        dbus_message_iter_close_container(&root, &structure);
    } else if (std::strcmp(member, "GetPosition") == 0) {
        dbus_int32_t x = static_cast<dbus_int32_t>(record.bounds.x);
        dbus_int32_t y = static_cast<dbus_int32_t>(record.bounds.y);
        dbus_message_iter_append_basic(&root, DBUS_TYPE_INT32, &x);
        dbus_message_iter_append_basic(&root, DBUS_TYPE_INT32, &y);
    } else if (std::strcmp(member, "GetSize") == 0) {
        dbus_int32_t width = static_cast<dbus_int32_t>(record.bounds.width);
        dbus_int32_t height = static_cast<dbus_int32_t>(record.bounds.height);
        dbus_message_iter_append_basic(&root, DBUS_TYPE_INT32, &width);
        dbus_message_iter_append_basic(&root, DBUS_TYPE_INT32, &height);
    } else if (std::strcmp(member, "Contains") == 0) {
        if (std::strcmp(dbus_message_get_signature(message), "iiu") != 0) {
            dbus_message_unref(reply);
            impl.send_error(message, DBUS_ERROR_INVALID_ARGS, "Contains expects point");
            return nullptr;
        }
        dbus_int32_t x = 0;
        dbus_int32_t y = 0;
        dbus_uint32_t coord_type = 0;
        DBusMessageIter arguments;
        dbus_message_iter_init(message, &arguments);
        dbus_message_iter_get_basic(&arguments, &x);
        dbus_message_iter_next(&arguments);
        dbus_message_iter_get_basic(&arguments, &y);
        dbus_message_iter_next(&arguments);
        dbus_message_iter_get_basic(&arguments, &coord_type);
        (void)coord_type;
        const auto left = static_cast<dbus_int32_t>(record.bounds.x);
        const auto top = static_cast<dbus_int32_t>(record.bounds.y);
        const auto right = static_cast<dbus_int32_t>(record.bounds.x + record.bounds.width);
        const auto bottom = static_cast<dbus_int32_t>(record.bounds.y + record.bounds.height);
        dbus_bool_t contained = x >= left && y >= top && x < right && y < bottom;
        dbus_message_iter_append_basic(&root, DBUS_TYPE_BOOLEAN, &contained);
    } else if (std::strcmp(member, "GetLayer") == 0) {
        dbus_uint32_t layer = 0;
        dbus_message_iter_append_basic(&root, DBUS_TYPE_UINT32, &layer);
    } else if (std::strcmp(member, "GetAlpha") == 0) {
        double alpha = 1.0;
        dbus_message_iter_append_basic(&root, DBUS_TYPE_DOUBLE, &alpha);
    } else if (std::strcmp(member, "GetMDIZOrder") == 0) {
        dbus_int16_t order = 0;
        dbus_message_iter_append_basic(&root, DBUS_TYPE_INT16, &order);
    } else {
        dbus_message_unref(reply);
        impl.send_error(message, DBUS_ERROR_UNKNOWN_METHOD, "unknown component method");
        return nullptr;
    }

    return reply;
}

::DBusMessage* handle_action(AtspiSink::Impl& impl, ::DBusMessage* message, std::size_t index) {
    const auto* member = dbus_message_get_member(message);
    auto* reply = dbus_message_new_method_return(message);
    if (reply == nullptr) {
        return nullptr;
    }
    DBusMessageIter root;
    dbus_message_iter_init_append(reply, &root);

    if (std::strcmp(member, "GetActions") == 0) {
        DBusMessageIter array;
        dbus_message_iter_open_container(&root, DBUS_TYPE_ARRAY, "(sss)", &array);
        if (impl.nodes[index].actions != 0) {
            DBusMessageIter action;
            dbus_message_iter_open_container(&array, DBUS_TYPE_STRUCT, nullptr, &action);
            const char* name = "activate";
            const char* description = "Activate";
            const char* key = "";
            dbus_message_iter_append_basic(
                &action,
                DBUS_TYPE_STRING,
                static_cast<const void*>(&name)
            );
            dbus_message_iter_append_basic(
                &action,
                DBUS_TYPE_STRING,
                static_cast<const void*>(&description)
            );
            dbus_message_iter_append_basic(
                &action,
                DBUS_TYPE_STRING,
                static_cast<const void*>(&key)
            );
            dbus_message_iter_close_container(&array, &action);
        }
        dbus_message_iter_close_container(&root, &array);
    } else if (std::strcmp(member, "DoAction") == 0) {
        if (std::strcmp(dbus_message_get_signature(message), "i") != 0) {
            dbus_message_unref(reply);
            impl.send_error(message, DBUS_ERROR_INVALID_ARGS, "DoAction expects an index");
            return nullptr;
        }
        dbus_int32_t action = -1;
        DBusMessageIter arguments;
        dbus_message_iter_init(message, &arguments);
        dbus_message_iter_get_basic(&arguments, &action);
        const auto valid = action == 0 && impl.nodes[index].actions != 0;
        dbus_bool_t handled = valid ? TRUE : FALSE;
        if (valid && impl.action_handler != nullptr) {
            impl.action_handler(impl.action_user, impl.nodes[index].id);
        }
        dbus_message_iter_append_basic(&root, DBUS_TYPE_BOOLEAN, &handled);
    } else if (
        std::strcmp(member, "GetName") == 0 || std::strcmp(member, "GetLocalizedName") == 0
    ) {
        const char* name = "activate";
        dbus_message_iter_append_basic(&root, DBUS_TYPE_STRING, static_cast<const void*>(&name));
    } else if (std::strcmp(member, "GetDescription") == 0) {
        const char* description = "Activate";
        dbus_message_iter_append_basic(
            &root,
            DBUS_TYPE_STRING,
            static_cast<const void*>(&description)
        );
    } else if (std::strcmp(member, "GetKeyBinding") == 0) {
        const char* key = "";
        dbus_message_iter_append_basic(&root, DBUS_TYPE_STRING, static_cast<const void*>(&key));
    } else {
        dbus_message_unref(reply);
        impl.send_error(message, DBUS_ERROR_UNKNOWN_METHOD, "unknown action method");
        return nullptr;
    }

    return reply;
}

::DBusMessage* handle_text(AtspiSink::Impl& impl, ::DBusMessage* message, std::size_t index) {
    const auto* member = dbus_message_get_member(message);
    const auto value = text_of(impl.nodes[index].value, impl.nodes[index].value_length);
    auto* reply = dbus_message_new_method_return(message);
    if (reply == nullptr) {
        return nullptr;
    }
    DBusMessageIter root;
    dbus_message_iter_init_append(reply, &root);

    if (std::strcmp(member, "GetText") == 0) {
        if (std::strcmp(dbus_message_get_signature(message), "ii") != 0) {
            dbus_message_unref(reply);
            impl.send_error(message, DBUS_ERROR_INVALID_ARGS, "GetText expects two offsets");
            return nullptr;
        }
        dbus_int32_t start = 0;
        dbus_int32_t end = -1;
        DBusMessageIter argument;
        dbus_message_iter_init(message, &argument);
        dbus_message_iter_get_basic(&argument, &start);
        dbus_message_iter_next(&argument);
        dbus_message_iter_get_basic(&argument, &end);
        const auto first = static_cast<std::size_t>(std::max(start, 0));
        const auto last = static_cast<std::size_t>(
            end < 0 ? value.size()
                    : std::min<std::size_t>(static_cast<std::size_t>(end), value.size())
        );
        const auto slice = first < last ? value.substr(first, last - first) : std::string_view{};
        const char* text = slice.data();
        dbus_message_iter_append_basic(&root, DBUS_TYPE_STRING, static_cast<const void*>(&text));
    } else if (std::strcmp(member, "GetStringAtOffset") == 0) {
        if (std::strcmp(dbus_message_get_signature(message), "iu") != 0) {
            dbus_message_unref(reply);
            impl.send_error(message, DBUS_ERROR_INVALID_ARGS, "GetStringAtOffset expects offset");
            return nullptr;
        }
        dbus_int32_t offset = 0;
        dbus_uint32_t granularity = 0;
        DBusMessageIter argument;
        dbus_message_iter_init(message, &argument);
        dbus_message_iter_get_basic(&argument, &offset);
        dbus_message_iter_next(&argument);
        dbus_message_iter_get_basic(&argument, &granularity);
        (void)granularity;
        const auto first = static_cast<std::size_t>(std::max(offset, 0));
        const auto slice = first < value.size() ? value.substr(first) : std::string_view{};
        const char* text = slice.data();
        dbus_int32_t start = static_cast<dbus_int32_t>(first);
        dbus_int32_t end = static_cast<dbus_int32_t>(value.size());
        dbus_message_iter_append_basic(&root, DBUS_TYPE_STRING, static_cast<const void*>(&text));
        dbus_message_iter_append_basic(&root, DBUS_TYPE_INT32, &start);
        dbus_message_iter_append_basic(&root, DBUS_TYPE_INT32, &end);
    } else if (std::strcmp(member, "SetCaretOffset") == 0) {
        dbus_bool_t accepted = FALSE;
        dbus_message_iter_append_basic(&root, DBUS_TYPE_BOOLEAN, &accepted);
    } else {
        dbus_message_unref(reply);
        impl.send_error(message, DBUS_ERROR_UNKNOWN_METHOD, "unknown text method");
        return nullptr;
    }

    return reply;
}

}
