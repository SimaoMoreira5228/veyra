#include "veyra/platform/linux/atspi_sink.hpp"

#include "veyra/platform/linux/atspi_protocol.hpp"

#include <dbus/dbus.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

void require(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

struct Reference {
    std::string name;
    std::string path;
};

void free_error(DBusError& error) {
    if (dbus_error_is_set(&error)) {
        dbus_error_free(&error);
    }
}

DBusMessage* call(
    DBusConnection* connection,
    const char* destination,
    const char* path,
    const char* interface,
    const char* method
) {
    auto* message = dbus_message_new_method_call(destination, path, interface, method);
    require(message != nullptr, "the test call is created");
    auto error = DBusError{};
    dbus_error_init(&error);
    auto* reply = dbus_connection_send_with_reply_and_block(connection, message, 3000, &error);
    dbus_message_unref(message);
    if (reply == nullptr) {
        const auto text = std::string{error.message == nullptr ? "no reply" : error.message};
        free_error(error);
        throw std::runtime_error(text);
    }
    free_error(error);

    return reply;
}

std::vector<Reference> read_references(DBusMessage* reply) {
    auto references = std::vector<Reference>{};
    DBusMessageIter root;
    require(dbus_message_iter_init(reply, &root), "the reply carries arguments");
    require(dbus_message_iter_get_arg_type(&root) == DBUS_TYPE_ARRAY, "references are an array");
    DBusMessageIter array;
    dbus_message_iter_recurse(&root, &array);
    while (dbus_message_iter_get_arg_type(&array) == DBUS_TYPE_STRUCT) {
        DBusMessageIter structure;
        dbus_message_iter_recurse(&array, &structure);
        const char* name = nullptr;
        const char* path = nullptr;
        dbus_message_iter_get_basic(&structure, static_cast<void*>(&name));
        dbus_message_iter_next(&structure);
        dbus_message_iter_get_basic(&structure, static_cast<void*>(&path));
        references.push_back({name, path});
        dbus_message_iter_next(&array);
    }

    return references;
}

std::vector<std::string> read_strings(DBusMessage* reply) {
    auto strings = std::vector<std::string>{};
    DBusMessageIter root;
    require(dbus_message_iter_init(reply, &root), "the reply carries arguments");
    DBusMessageIter array;
    dbus_message_iter_recurse(&root, &array);
    while (dbus_message_iter_get_arg_type(&array) == DBUS_TYPE_STRING) {
        const char* value = nullptr;
        dbus_message_iter_get_basic(&array, static_cast<void*>(&value));
        strings.emplace_back(value);
        dbus_message_iter_next(&array);
    }

    return strings;
}

std::string read_string(DBusMessage* reply) {
    DBusMessageIter root;
    require(dbus_message_iter_init(reply, &root), "the reply carries a string");
    const char* value = nullptr;
    dbus_message_iter_get_basic(&root, static_cast<void*>(&value));

    return value;
}

uint32_t read_uint(DBusMessage* reply) {
    DBusMessageIter root;
    require(dbus_message_iter_init(reply, &root), "the reply carries a uint");
    dbus_uint32_t value = 0;
    dbus_message_iter_get_basic(&root, static_cast<void*>(&value));

    return value;
}

bool read_bool(DBusMessage* reply) {
    DBusMessageIter root;
    require(dbus_message_iter_init(reply, &root), "the reply carries a bool");
    dbus_bool_t value = FALSE;
    dbus_message_iter_get_basic(&root, static_cast<void*>(&value));

    return value == TRUE;
}

std::array<uint32_t, 2> read_state(DBusMessage* reply) {
    auto state = std::array<uint32_t, 2>{};
    DBusMessageIter root;
    require(dbus_message_iter_init(reply, &root), "the reply carries state");
    DBusMessageIter array;
    dbus_message_iter_recurse(&root, &array);
    for (auto index = std::size_t{}; index < state.size(); ++index) {
        require(
            dbus_message_iter_get_arg_type(&array) == DBUS_TYPE_UINT32,
            "state words are uints"
        );
        dbus_uint32_t word = 0;
        dbus_message_iter_get_basic(&array, &word);
        state[index] = word;
        dbus_message_iter_next(&array);
    }

    return state;
}

bool has_atspi_state(const std::array<uint32_t, 2>& state, veyra::atspi::State bit) {
    const auto value = static_cast<uint32_t>(bit);
    return (state[value / 32] & (1U << (value % 32))) != 0;
}

std::pair<int, int> read_size(DBusMessage* reply) {
    DBusMessageIter root;
    require(dbus_message_iter_init(reply, &root), "the reply carries size");
    dbus_int32_t width = 0;
    dbus_int32_t height = 0;
    dbus_message_iter_get_basic(&root, &width);
    dbus_message_iter_next(&root);
    dbus_message_iter_get_basic(&root, &height);

    return {width, height};
}

std::string property(
    DBusConnection* connection,
    const char* destination,
    const std::string& path,
    const char* interface,
    const char* name
) {
    auto* message = dbus_message_new_method_call(
        destination,
        path.c_str(),
        "org.freedesktop.DBus.Properties",
        "Get"
    );
    require(message != nullptr, "the property call is created");
    DBusMessageIter arguments;
    dbus_message_iter_init_append(message, &arguments);
    dbus_message_iter_append_basic(
        &arguments,
        DBUS_TYPE_STRING,
        static_cast<const void*>(&interface)
    );
    dbus_message_iter_append_basic(&arguments, DBUS_TYPE_STRING, static_cast<const void*>(&name));
    auto error = DBusError{};
    dbus_error_init(&error);
    auto* reply = dbus_connection_send_with_reply_and_block(connection, message, 3000, &error);
    dbus_message_unref(message);
    if (reply == nullptr) {
        const auto text = std::string{error.message == nullptr ? "no reply" : error.message};
        free_error(error);
        throw std::runtime_error(text);
    }
    free_error(error);

    DBusMessageIter root;
    require(dbus_message_iter_init(reply, &root), "the property reply carries a variant");
    DBusMessageIter variant;
    dbus_message_iter_recurse(&root, &variant);
    const char* value = nullptr;
    dbus_message_iter_get_basic(&variant, static_cast<void*>(&value));
    const auto result = std::string{value};
    dbus_message_unref(reply);

    return result;
}

std::atomic<veyra::NodeId> action_node{};
std::atomic<bool> action_user_matches{true};

void on_action(void* user, veyra::NodeId node) noexcept {
    action_user_matches.store(user == &action_node);
    action_node.store(node);
}

}

int main() {
    try {
        auto sink = veyra::AtspiSink{};
        if (!sink.connect()) {
            std::cout << "no D-Bus session, skipping\n";

            return 77;
        }
        sink.set_action_handler(on_action, &action_node);

        const auto window_id = veyra::WindowId{1};
        const auto window_node = veyra::NodeId{.index = 1, .generation = 1};
        const auto button_node = veyra::NodeId{.index = 2, .generation = 1};
        const auto password_node = veyra::NodeId{.index = 3, .generation = 1};
        const auto label_node = veyra::NodeId{.index = 4, .generation = 1};

        sink.begin_window(window_id);
        sink.node({
            .window = window_id,
            .id = window_node,
            .role = veyra::SemanticRole::window,
            .name = "Main",
            .bounds = {.width = 320.0F, .height = 240.0F},
            .states = veyra::state_flag(veyra::SemanticState::focusable),
        });
        sink.node({
            .window = window_id,
            .id = button_node,
            .parent = window_node,
            .role = veyra::SemanticRole::button,
            .name = "Save",
            .bounds = {.x = 10.0F, .y = 20.0F, .width = 80.0F, .height = 30.0F},
            .states = veyra::state_flag(veyra::SemanticState::focusable) |
                veyra::state_flag(veyra::SemanticState::focused),
            .actions = veyra::action_flag(veyra::SemanticAction::activate),
        });
        sink.node({
            .window = window_id,
            .id = password_node,
            .parent = window_node,
            .role = veyra::SemanticRole::text_input,
            .name = "Password",
            .bounds = {.x = 10.0F, .y = 60.0F, .width = 200.0F, .height = 24.0F},
            .states = veyra::state_flag(veyra::SemanticState::protected_value),
        });
        sink.node({
            .window = window_id,
            .id = label_node,
            .parent = window_node,
            .role = veyra::SemanticRole::text,
            .name = "Status",
            .value = "Hello",
            .bounds = {.x = 10.0F, .y = 100.0F, .width = 120.0F, .height = 20.0F},
        });
        sink.end_window(window_id);

        require(sink.node_count() == 4, "every published node is stored");
        require(std::strlen(sink.bus_name()) != 0, "the sink owns a bus name");

        auto error = DBusError{};
        dbus_error_init(&error);
        auto* connection = dbus_connection_open_private(sink.bus_address(), &error);
        if (connection == nullptr) {
            const auto text = std::string{error.message == nullptr ? "no client" : error.message};
            free_error(error);
            throw std::runtime_error(text);
        }
        dbus_connection_set_exit_on_disconnect(connection, FALSE);
        if (!dbus_bus_register(connection, &error)) {
            const auto text =
                std::string{error.message == nullptr ? "no client name" : error.message};
            free_error(error);
            dbus_connection_close(connection);
            dbus_connection_unref(connection);
            throw std::runtime_error(text);
        }
        free_error(error);

        const auto root_path = std::string{"/org/a11y/atspi/accessible/root"};
        const auto window_path = std::string{"/org/a11y/atspi/accessible/n1"};

        auto* reply = call(
            connection,
            sink.bus_name(),
            root_path.c_str(),
            "org.a11y.atspi.Accessible",
            "GetChildren"
        );
        auto children = read_references(reply);
        dbus_message_unref(reply);
        require(children.size() == 1, "the application exposes one top-level window");
        require(children[0].name == sink.bus_name(), "the child reference names the bus");
        require(children[0].path == window_path, "the window child path is stable");

        reply = call(
            connection,
            sink.bus_name(),
            window_path.c_str(),
            "org.a11y.atspi.Accessible",
            "GetChildren"
        );
        children = read_references(reply);
        dbus_message_unref(reply);
        require(children.size() == 3, "the window exposes its children");

        const auto button_path = std::string{"/org/a11y/atspi/accessible/n2"};
        const auto password_path = std::string{"/org/a11y/atspi/accessible/n3"};
        const auto label_path = std::string{"/org/a11y/atspi/accessible/n4"};

        require(
            property(
                connection,
                sink.bus_name(),
                button_path,
                "org.a11y.atspi.Accessible",
                "Name"
            ) == "Save",
            "the button name reaches AT-SPI"
        );
        reply = call(
            connection,
            sink.bus_name(),
            button_path.c_str(),
            "org.a11y.atspi.Accessible",
            "GetRole"
        );
        require(
            read_uint(reply) == static_cast<uint32_t>(veyra::atspi::Role::button),
            "the button role maps"
        );
        dbus_message_unref(reply);

        reply = call(
            connection,
            sink.bus_name(),
            button_path.c_str(),
            "org.a11y.atspi.Accessible",
            "GetState"
        );
        const auto state = read_state(reply);
        dbus_message_unref(reply);
        require(has_atspi_state(state, veyra::atspi::State::focused), "focus state maps");
        require(has_atspi_state(state, veyra::atspi::State::focusable), "focusable state maps");
        require(has_atspi_state(state, veyra::atspi::State::enabled), "enabled state maps");

        reply = call(
            connection,
            sink.bus_name(),
            button_path.c_str(),
            "org.a11y.atspi.Component",
            "GetSize"
        );
        const auto size = read_size(reply);
        dbus_message_unref(reply);
        require(size.first == 80 && size.second == 30, "the component size maps");

        reply = call(
            connection,
            sink.bus_name(),
            button_path.c_str(),
            "org.a11y.atspi.Action",
            "GetActions"
        );
        auto action_count = std::size_t{};
        DBusMessageIter root;
        require(dbus_message_iter_init(reply, &root), "actions carry an array");
        DBusMessageIter array;
        dbus_message_iter_recurse(&root, &array);
        while (dbus_message_iter_get_arg_type(&array) == DBUS_TYPE_STRUCT) {
            ++action_count;
            dbus_message_iter_next(&array);
        }
        dbus_message_unref(reply);
        require(action_count == 1, "the button exposes one action");

        auto* do_action = dbus_message_new_method_call(
            sink.bus_name(),
            button_path.c_str(),
            "org.a11y.atspi.Action",
            "DoAction"
        );
        require(do_action != nullptr, "the action call is created");
        DBusMessageIter arguments;
        dbus_message_iter_init_append(do_action, &arguments);
        dbus_int32_t action_index = 0;
        dbus_message_iter_append_basic(&arguments, DBUS_TYPE_INT32, &action_index);
        reply = dbus_connection_send_with_reply_and_block(connection, do_action, 3000, &error);
        dbus_message_unref(do_action);
        require(reply != nullptr, "the action call returns");
        require(read_bool(reply), "the action is handled");
        dbus_message_unref(reply);
        require(action_node.load() == button_node, "the action reaches the host callback");
        require(action_user_matches.load(), "the action user pointer is preserved");

        require(
            property(
                connection,
                sink.bus_name(),
                password_path,
                "org.a11y.atspi.Accessible",
                "Name"
            ) == "Password",
            "the password field keeps its label"
        );
        reply = call(
            connection,
            sink.bus_name(),
            password_path.c_str(),
            "org.a11y.atspi.Accessible",
            "GetRole"
        );
        require(
            read_uint(reply) == static_cast<uint32_t>(veyra::atspi::Role::entry),
            "the text input role maps"
        );
        dbus_message_unref(reply);

        auto* text_call = dbus_message_new_method_call(
            sink.bus_name(),
            label_path.c_str(),
            "org.a11y.atspi.Text",
            "GetText"
        );
        require(text_call != nullptr, "the text call is created");
        DBusMessageIter text_arguments;
        dbus_message_iter_init_append(text_call, &text_arguments);
        dbus_int32_t text_start = 0;
        dbus_int32_t text_end = -1;
        dbus_message_iter_append_basic(&text_arguments, DBUS_TYPE_INT32, &text_start);
        dbus_message_iter_append_basic(&text_arguments, DBUS_TYPE_INT32, &text_end);
        reply = dbus_connection_send_with_reply_and_block(connection, text_call, 3000, &error);
        dbus_message_unref(text_call);
        require(reply != nullptr, "the text call returns");
        require(read_string(reply) == "Hello", "text values reach AT-SPI");
        dbus_message_unref(reply);

        reply = call(
            connection,
            sink.bus_name(),
            label_path.c_str(),
            "org.a11y.atspi.Accessible",
            "GetInterfaces"
        );
        const auto interfaces = read_strings(reply);
        dbus_message_unref(reply);
        require(
            std::find(interfaces.begin(), interfaces.end(), "org.a11y.atspi.Text") !=
                interfaces.end(),
            "text nodes advertise the Text interface"
        );

        reply = call(
            connection,
            sink.bus_name(),
            root_path.c_str(),
            "org.freedesktop.DBus.Introspectable",
            "Introspect"
        );
        require(
            read_string(reply).find("org.a11y.atspi.Accessible") != std::string::npos,
            "introspection advertises the accessible interface"
        );
        dbus_message_unref(reply);

        require(
            property(
                connection,
                sink.bus_name(),
                root_path,
                "org.a11y.atspi.Application",
                "ToolkitName"
            ) == "Veyra",
            "the application advertises the toolkit"
        );

        dbus_connection_close(connection);
        dbus_connection_unref(connection);
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';

        return 1;
    }

    std::cout << "AT-SPI checks passed\n";

    return 0;
}
