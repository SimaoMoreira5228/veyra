#include "atspi_common.hpp"
#include "atspi_handlers.hpp"

#include <cstddef>
#include <cstring>

namespace veyra::atspi_detail {

::DBusMessage* handle_properties(AtspiSink::Impl& impl, ::DBusMessage* message, std::size_t index) {
    const auto* iface = dbus_message_get_interface(message);
    const auto* member = dbus_message_get_member(message);
    const auto* name = dbus_message_get_signature(message);
    (void)iface;

    auto* reply = dbus_message_new_method_return(message);
    if (reply == nullptr) {
        return nullptr;
    }
    DBusMessageIter root;
    dbus_message_iter_init_append(reply, &root);

    if (std::strcmp(member, "GetAll") == 0) {
        if (std::strcmp(name, "s") != 0) {
            dbus_message_unref(reply);
            impl.send_error(message, DBUS_ERROR_INVALID_ARGS, "GetAll expects an interface name");
            return nullptr;
        }
        const char* interface = nullptr;
        DBusMessageIter arguments;
        dbus_message_iter_init(message, &arguments);
        dbus_message_iter_get_basic(&arguments, static_cast<void*>(&interface));
        const auto app = std::strcmp(interface, "org.a11y.atspi.Application") == 0;
        DBusMessageIter array;
        dbus_message_iter_open_container(&root, DBUS_TYPE_ARRAY, "{sv}", &array);
        const auto put_string = [&array](const char* key, const char* value) {
            DBusMessageIter entry;
            DBusMessageIter variant;
            dbus_message_iter_open_container(&array, DBUS_TYPE_DICT_ENTRY, nullptr, &entry);
            dbus_message_iter_append_basic(
                &entry,
                DBUS_TYPE_STRING,
                static_cast<const void*>(&key)
            );
            dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT, "s", &variant);
            dbus_message_iter_append_basic(
                &variant,
                DBUS_TYPE_STRING,
                static_cast<const void*>(&value)
            );
            dbus_message_iter_close_container(&entry, &variant);
            dbus_message_iter_close_container(&array, &entry);
        };
        const auto put_int = [&array](const char* key, dbus_int32_t value) {
            DBusMessageIter entry;
            DBusMessageIter variant;
            dbus_message_iter_open_container(&array, DBUS_TYPE_DICT_ENTRY, nullptr, &entry);
            dbus_message_iter_append_basic(
                &entry,
                DBUS_TYPE_STRING,
                static_cast<const void*>(&key)
            );
            dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT, "i", &variant);
            dbus_message_iter_append_basic(&variant, DBUS_TYPE_INT32, &value);
            dbus_message_iter_close_container(&entry, &variant);
            dbus_message_iter_close_container(&array, &entry);
        };
        if (app) {
            put_string("ToolkitName", "Veyra");
            put_string("Version", "0.1.0");
            put_string("ToolkitVersion", "0.1.0");
            put_string("AtspiVersion", "2.1");
            put_int("InterfaceVersion", 1);
            put_int("Id", 1);
        } else {
            put_string("Name", impl.name_for(index));
            put_string("Description", "");
            put_string("Locale", "en");
            put_string("AccessibleId", "");
            put_int("ChildCount", static_cast<dbus_int32_t>(impl.child_count(index)));
        }
        dbus_message_iter_close_container(&root, &array);
        return reply;
    }

    if (std::strcmp(member, "Get") != 0) {
        dbus_message_unref(reply);
        impl.send_error(message, DBUS_ERROR_UNKNOWN_METHOD, "unknown properties call");
        return nullptr;
    }

    if (std::strcmp(name, "ss") != 0) {
        dbus_message_unref(reply);
        impl.send_error(message, DBUS_ERROR_INVALID_ARGS, "Get expects interface and property");
        return nullptr;
    }

    const char* interface = nullptr;
    const char* property = nullptr;
    DBusMessageIter arguments;
    dbus_message_iter_init(message, &arguments);
    dbus_message_iter_get_basic(&arguments, static_cast<void*>(&interface));
    dbus_message_iter_next(&arguments);
    dbus_message_iter_get_basic(&arguments, static_cast<void*>(&property));
    const auto app = std::strcmp(interface, "org.a11y.atspi.Application") == 0;
    if (app) {
        const char* value = nullptr;
        if (std::strcmp(property, "ToolkitName") == 0) {
            value = "Veyra";
        } else if (
            std::strcmp(property, "Version") == 0 || std::strcmp(property, "ToolkitVersion") == 0
        ) {
            value = "0.1.0";
        } else if (std::strcmp(property, "AtspiVersion") == 0) {
            value = "2.1";
        }
        if (value != nullptr) {
            append_string_variant(&root, value);
            return reply;
        }
        if (std::strcmp(property, "InterfaceVersion") == 0) {
            append_uint_variant(&root, 1);
            return reply;
        }
        if (std::strcmp(property, "Id") == 0) {
            append_int_variant(&root, 1);
            return reply;
        }
    }

    if (std::strcmp(property, "Name") == 0) {
        append_string_variant(&root, impl.name_for(index));
    } else if (
        std::strcmp(property, "Description") == 0 || std::strcmp(property, "HelpText") == 0 ||
        std::strcmp(property, "AccessibleId") == 0
    ) {
        append_string_variant(&root, "");
    } else if (std::strcmp(property, "Locale") == 0) {
        append_string_variant(&root, "en");
    } else if (std::strcmp(property, "ChildCount") == 0) {
        append_int_variant(&root, static_cast<dbus_int32_t>(impl.child_count(index)));
    } else if (std::strcmp(property, "Parent") == 0) {
        DBusMessageIter variant;
        DBusMessageIter structure;
        dbus_message_iter_open_container(&root, DBUS_TYPE_VARIANT, "(so)", &variant);
        dbus_message_iter_open_container(&variant, DBUS_TYPE_STRUCT, nullptr, &structure);
        const char* parent_name = index == 0 ? "" : impl.bus_name.data();
        const char* path = index == 0 ? "/" : atspi::kRootPath;
        dbus_message_iter_append_basic(
            &structure,
            DBUS_TYPE_STRING,
            static_cast<const void*>(&parent_name)
        );
        dbus_message_iter_append_basic(
            &structure,
            DBUS_TYPE_OBJECT_PATH,
            static_cast<const void*>(&path)
        );
        dbus_message_iter_close_container(&variant, &structure);
        dbus_message_iter_close_container(&root, &variant);
    } else if (std::strcmp(property, "NActions") == 0) {
        append_int_variant(&root, impl.nodes[index].actions != 0 ? 1 : 0);
    } else if (std::strcmp(property, "CharacterCount") == 0) {
        append_int_variant(&root, static_cast<dbus_int32_t>(impl.nodes[index].value_length));
    } else if (std::strcmp(property, "CaretOffset") == 0) {
        append_int_variant(&root, 0);
    } else if (std::strcmp(property, "version") == 0) {
        append_uint_variant(&root, 1);
    } else {
        dbus_message_unref(reply);
        impl.send_error(message, DBUS_ERROR_UNKNOWN_PROPERTY, "unknown property");
        return nullptr;
    }

    return reply;
}

::DBusMessage* handle_accessible(AtspiSink::Impl& impl, ::DBusMessage* message, std::size_t index) {
    const auto* member = dbus_message_get_member(message);
    auto* reply = dbus_message_new_method_return(message);
    if (reply == nullptr) {
        return nullptr;
    }
    DBusMessageIter root;
    dbus_message_iter_init_append(reply, &root);
    const auto append_ref = [&impl](DBusMessageIter* parent, std::size_t node) {
        DBusMessageIter structure;
        dbus_message_iter_open_container(parent, DBUS_TYPE_STRUCT, nullptr, &structure);
        const char* name = impl.bus_name.data();
        const auto path = node == 0 ? std::string{atspi::kRootPath}
                                    : std::string{atspi::kNodePrefix} + std::to_string(node);
        const char* path_pointer = path.c_str();
        dbus_message_iter_append_basic(
            &structure,
            DBUS_TYPE_STRING,
            static_cast<const void*>(&name)
        );
        dbus_message_iter_append_basic(
            &structure,
            DBUS_TYPE_OBJECT_PATH,
            static_cast<const void*>(&path_pointer)
        );
        dbus_message_iter_close_container(parent, &structure);
    };

    if (std::strcmp(member, "GetChildren") == 0) {
        DBusMessageIter array;
        dbus_message_iter_open_container(&root, DBUS_TYPE_ARRAY, "(so)", &array);
        for (auto child = std::size_t{1}; child < impl.count; ++child) {
            if (impl.child_of(index, child)) {
                append_ref(&array, child);
            }
        }
        dbus_message_iter_close_container(&root, &array);
    } else if (std::strcmp(member, "GetChildAtIndex") == 0) {
        if (std::strcmp(dbus_message_get_signature(message), "i") != 0) {
            dbus_message_unref(reply);
            impl.send_error(message, DBUS_ERROR_INVALID_ARGS, "GetChildAtIndex expects an index");
            return nullptr;
        }
        dbus_int32_t ordinal = -1;
        DBusMessageIter arguments;
        dbus_message_iter_init(message, &arguments);
        dbus_message_iter_get_basic(&arguments, &ordinal);
        const auto child = ordinal < 0 ? AtspiSink::kNodeCapacity + 1
                                       : impl.child_at(index, static_cast<std::size_t>(ordinal));
        if (child > AtspiSink::kNodeCapacity) {
            dbus_message_unref(reply);
            impl.send_error(message, DBUS_ERROR_INVALID_ARGS, "no child at index");
            return nullptr;
        }
        append_ref(&root, child);
    } else if (std::strcmp(member, "GetIndexInParent") == 0) {
        dbus_int32_t position = 0;
        if (index != 0) {
            const auto parent = impl.nodes[index].parent;
            for (auto child = std::size_t{1}; child < impl.count; ++child) {
                if (!impl.child_of(parent, child)) {
                    continue;
                }
                if (child == index) {
                    break;
                }
                ++position;
            }
        }
        dbus_message_iter_append_basic(&root, DBUS_TYPE_INT32, &position);
    } else if (std::strcmp(member, "GetRole") == 0) {
        dbus_uint32_t role = static_cast<dbus_uint32_t>(
            index == 0 ? atspi::Role::application : atspi_role(impl.nodes[index].role)
        );
        dbus_message_iter_append_basic(&root, DBUS_TYPE_UINT32, &role);
    } else if (
        std::strcmp(member, "GetRoleName") == 0 || std::strcmp(member, "GetLocalizedRoleName") == 0
    ) {
        const char* role = index == 0 ? "application" : atspi_role_name(impl.nodes[index].role);
        dbus_message_iter_append_basic(&root, DBUS_TYPE_STRING, static_cast<const void*>(&role));
    } else if (std::strcmp(member, "GetState") == 0) {
        const auto states = index == 0 ? std::array<std::uint32_t, 2>{0x02000000U, 0U}
                                       : atspi_states(impl.nodes[index]);
        DBusMessageIter array;
        dbus_message_iter_open_container(&root, DBUS_TYPE_ARRAY, "u", &array);
        for (const auto word : states) {
            dbus_message_iter_append_basic(&array, DBUS_TYPE_UINT32, &word);
        }
        dbus_message_iter_close_container(&root, &array);
    } else if (std::strcmp(member, "GetInterfaces") == 0) {
        DBusMessageIter array;
        dbus_message_iter_open_container(&root, DBUS_TYPE_ARRAY, "s", &array);
        const auto put = [&array](const char* name) {
            dbus_message_iter_append_basic(
                &array,
                DBUS_TYPE_STRING,
                static_cast<const void*>(&name)
            );
        };
        put("org.a11y.atspi.Accessible");
        put("org.a11y.atspi.Component");
        if (index == 0) {
            put("org.a11y.atspi.Application");
        }
        if (index != 0 && impl.nodes[index].actions != 0) {
            put("org.a11y.atspi.Action");
        }
        if (index != 0 && is_text_role(impl.nodes[index].role) &&
            impl.nodes[index].value_length != 0) {
            put("org.a11y.atspi.Text");
        }
        dbus_message_iter_close_container(&root, &array);
    } else if (std::strcmp(member, "GetAttributes") == 0) {
        DBusMessageIter array;
        dbus_message_iter_open_container(&root, DBUS_TYPE_ARRAY, "{ss}", &array);
        dbus_message_iter_close_container(&root, &array);
    } else if (std::strcmp(member, "GetRelationSet") == 0) {
        DBusMessageIter array;
        dbus_message_iter_open_container(&root, DBUS_TYPE_ARRAY, "(ua(so))", &array);
        dbus_message_iter_close_container(&root, &array);
    } else if (std::strcmp(member, "GetApplication") == 0) {
        append_ref(&root, 0);
    } else {
        dbus_message_unref(reply);
        impl.send_error(message, DBUS_ERROR_UNKNOWN_METHOD, "unknown accessible method");
        return nullptr;
    }

    return reply;
}

}
