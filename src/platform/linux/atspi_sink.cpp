#include "veyra/platform/linux/atspi_sink.hpp"

#include "atspi_impl.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <span>
#include <string_view>
#include <utility>

namespace veyra {
namespace {

void copy_text(std::span<char> storage, std::string_view text, std::uint32_t& length) noexcept {
    length = static_cast<std::uint32_t>(std::min(text.size(), storage.size() - 1));
    if (length != 0) {
        std::memcpy(storage.data(), text.data(), length);
    }
    storage[length] = '\0';
}

void close_connection(DBusConnection* connection) noexcept {
    dbus_connection_close(connection);
    dbus_connection_unref(connection);
}

}

void AtspiSink::Impl::close() noexcept {
    if (!connected) {
        return;
    }
    running.store(false, std::memory_order_release);
    if (worker.joinable()) {
        worker.join();
    }
    if (bus != nullptr) {
        dbus_connection_unregister_object_path(bus, atspi::kFallbackPath);
        dbus_connection_flush(bus);
        close_connection(bus);
        bus = nullptr;
    }
    connected = false;
}

void AtspiSink::Impl::begin_window(WindowId window) noexcept {
    std::lock_guard lock(mutex);
    nodes = {};
    count = 1;
    published = 0;
    nodes[0].live = true;
    nodes[0].role = SemanticRole::window;
    copy_text(nodes[0].name, "Veyra", nodes[0].name_length);
    nodes[0].window = window;
}

void AtspiSink::Impl::add_node(const AccessibilityNode& record) noexcept {
    std::lock_guard lock(mutex);
    if (count >= nodes.size()) {
        return;
    }

    auto& target = nodes[count];
    target = {};
    target.live = true;
    target.window = record.window;
    target.id = record.id;
    target.parent_id = record.parent;
    target.role = record.role;
    target.states = record.states;
    target.actions = record.actions;
    target.bounds = record.bounds;
    target.virtualized = record.virtualized;
    copy_text(target.name, record.name, target.name_length);
    copy_text(target.value, record.value, target.value_length);

    target.parent = 0;
    if (!record.parent.is_null()) {
        for (auto index = std::size_t{1}; index < count; ++index) {
            if (nodes[index].id == record.parent) {
                target.parent = index;
                break;
            }
        }
    }

    ++count;
    ++published;
}

AtspiSink::AtspiSink() noexcept : impl_(new (std::nothrow) Impl()) {
}

AtspiSink::~AtspiSink() = default;

bool AtspiSink::connect() noexcept {
    if (impl_ == nullptr) {
        return false;
    }
    if (impl_->connected.load(std::memory_order_acquire)) {
        return true;
    }

    auto error = DBusError{};
    dbus_error_init(&error);
    auto* session = dbus_bus_get_private(DBUS_BUS_SESSION, &error);
    if (session == nullptr) {
        dbus_error_free(&error);
        return false;
    }
    dbus_connection_set_exit_on_disconnect(session, FALSE);

    auto* bus = static_cast<DBusConnection*>(nullptr);
    auto* request =
        dbus_message_new_method_call("org.a11y.Bus", "/org/a11y/bus", "org.a11y.Bus", "GetAddress");
    auto* response = request == nullptr
        ? nullptr
        : dbus_connection_send_with_reply_and_block(session, request, 2000, &error);
    if (request != nullptr) {
        dbus_message_unref(request);
    }
    if (response != nullptr) {
        const char* address = nullptr;
        if (dbus_message_get_args(
                response,
                &error,
                DBUS_TYPE_STRING,
                &address,
                DBUS_TYPE_INVALID
            )) {
            if (address != nullptr && *address != '\0') {
                std::snprintf(impl_->bus_address.data(), impl_->bus_address.size(), "%s", address);
                bus = dbus_connection_open_private(address, &error);
            }
        }
        dbus_message_unref(response);
    }
    dbus_error_free(&error);

    if (bus == nullptr) {
        bus = session;
        session = nullptr;
        const auto* session_address = std::getenv("DBUS_SESSION_BUS_ADDRESS");
        if (session_address != nullptr) {
            std::snprintf(
                impl_->bus_address.data(),
                impl_->bus_address.size(),
                "%s",
                session_address
            );
        }
    } else {
        close_connection(session);
    }
    if (bus == nullptr) {
        return false;
    }

    dbus_connection_set_exit_on_disconnect(bus, FALSE);
    auto register_error = DBusError{};
    dbus_error_init(&register_error);
    const auto registered = dbus_bus_register(bus, &register_error);
    dbus_error_free(&register_error);
    if (!registered) {
        close_connection(bus);
        return false;
    }

    const auto* unique = dbus_bus_get_unique_name(bus);
    if (unique == nullptr) {
        close_connection(bus);
        return false;
    }
    std::snprintf(impl_->bus_name.data(), impl_->bus_name.size(), "%s", unique);

    DBusObjectPathVTable vtable{};
    vtable.message_function = &Impl::dispatch;
    if (!dbus_connection_register_fallback(bus, atspi::kFallbackPath, &vtable, impl_.get())) {
        close_connection(bus);
        return false;
    }

    const auto* root_path = atspi::kRootPath;
    auto* embed = dbus_message_new_method_call(
        "org.a11y.atspi.Registry",
        atspi::kRootPath,
        "org.a11y.atspi.Socket",
        "Embed"
    );
    if (embed != nullptr) {
        DBusMessageIter arguments;
        DBusMessageIter socket;
        dbus_message_iter_init_append(embed, &arguments);
        dbus_message_iter_open_container(&arguments, DBUS_TYPE_STRUCT, nullptr, &socket);
        const char* name = impl_->bus_name.data();
        dbus_message_iter_append_basic(&socket, DBUS_TYPE_STRING, static_cast<const void*>(&name));
        dbus_message_iter_append_basic(
            &socket,
            DBUS_TYPE_OBJECT_PATH,
            static_cast<const void*>(&root_path)
        );
        dbus_message_iter_close_container(&arguments, &socket);
        auto* embed_reply = dbus_connection_send_with_reply_and_block(bus, embed, 1000, &error);
        if (embed_reply != nullptr) {
            dbus_message_unref(embed_reply);
        }
        dbus_message_unref(embed);
        dbus_error_free(&error);
    }

    impl_->bus = bus;
    impl_->connected.store(true, std::memory_order_release);
    impl_->running.store(true, std::memory_order_release);
    try {
        impl_->worker = std::thread([impl = impl_.get()] {
            while (impl->running.load(std::memory_order_acquire)) {
                dbus_connection_read_write_dispatch(impl->bus, 25);
            }
        });
    } catch (...) {
        impl_->running.store(false, std::memory_order_release);
        impl_->connected.store(false, std::memory_order_release);
        impl_->bus = nullptr;
        dbus_connection_unregister_object_path(bus, atspi::kFallbackPath);
        close_connection(bus);
        return false;
    }

    return true;
}

bool AtspiSink::connected() const noexcept {
    return impl_ != nullptr && impl_->connected.load(std::memory_order_acquire);
}

void AtspiSink::close() noexcept {
    if (impl_ != nullptr) {
        impl_->close();
    }
}

void AtspiSink::set_action_handler(ActionHandler handler, void* user) noexcept {
    if (impl_ == nullptr) {
        return;
    }
    std::lock_guard lock(impl_->mutex);
    impl_->action_handler = handler;
    impl_->action_user = user;
}

void AtspiSink::begin_window(WindowId window) noexcept {
    if (impl_ != nullptr) {
        impl_->begin_window(window);
    }
}

void AtspiSink::node(const AccessibilityNode& record) noexcept {
    if (impl_ != nullptr) {
        impl_->add_node(record);
    }
}

void AtspiSink::end_window(WindowId window) noexcept {
    (void)window;
}

std::size_t AtspiSink::node_count() const noexcept {
    if (impl_ == nullptr) {
        return 0;
    }
    std::lock_guard lock(impl_->mutex);
    return impl_->published;
}

const char* AtspiSink::bus_name() const noexcept {
    return impl_ == nullptr ? "" : impl_->bus_name.data();
}

const char* AtspiSink::bus_address() const noexcept {
    return impl_ == nullptr ? "" : impl_->bus_address.data();
}

}
