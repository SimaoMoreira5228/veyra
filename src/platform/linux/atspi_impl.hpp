#ifndef VEYRA_PLATFORM_LINUX_ATSPI_IMPL_HPP
#define VEYRA_PLATFORM_LINUX_ATSPI_IMPL_HPP

#include "veyra/platform/linux/atspi_protocol.hpp"
#include "veyra/platform/linux/atspi_sink.hpp"

#include <dbus/dbus.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <span>
#include <string_view>
#include <thread>

namespace veyra {

struct AtspiNode {
    bool live{};
    WindowId window{};
    NodeId id{};
    NodeId parent_id{};
    std::size_t parent{};
    SemanticRole role{SemanticRole::none};
    std::uint16_t states{};
    std::uint16_t actions{};
    LayoutRect bounds{};
    std::array<char, 96> name{};
    std::array<char, 96> value{};
    std::uint32_t name_length{};
    std::uint32_t value_length{};
    bool virtualized{};
};

struct AtspiSink::Impl {
    DBusConnection* bus{};
    std::atomic<bool> running{};
    std::thread worker;
    mutable std::mutex mutex;
    std::array<AtspiNode, AtspiSink::kNodeCapacity + 1> nodes{};
    std::size_t count{};
    std::size_t published{};
    std::array<char, 128> bus_name{};
    std::array<char, 128> bus_address{};
    ActionHandler action_handler{};
    void* action_user{};
    std::atomic<bool> connected{};

    ~Impl() {
        close();
    }

    void close() noexcept;
    ::DBusMessage* handle(::DBusMessage* message) noexcept;

    static DBusHandlerResult
    dispatch(DBusConnection* connection, DBusMessage* message, void* user) noexcept;

    std::size_t index_for_path(std::string_view path) const noexcept {
        if (path == atspi::kRootPath) {
            return 0;
        }
        if (!path.starts_with(atspi::kNodePrefix)) {
            return AtspiSink::kNodeCapacity + 1;
        }
        const auto digits = path.substr(std::string_view{atspi::kNodePrefix}.size());
        if (digits.empty()) {
            return AtspiSink::kNodeCapacity + 1;
        }
        auto value = std::size_t{};
        for (const auto digit : digits) {
            if (digit < '0' || digit > '9') {
                return AtspiSink::kNodeCapacity + 1;
            }
            value = value * 10 + static_cast<std::size_t>(digit - '0');
            if (value > AtspiSink::kNodeCapacity) {
                return AtspiSink::kNodeCapacity + 1;
            }
        }

        return value;
    }

    bool child_of(std::size_t parent, std::size_t child) const noexcept {
        return child < count && child != 0 && nodes[child].live && nodes[child].parent == parent;
    }

    std::size_t child_count(std::size_t index) const noexcept {
        auto result = std::size_t{};
        for (auto child = std::size_t{1}; child < count; ++child) {
            if (child_of(index, child)) {
                ++result;
            }
        }

        return result;
    }

    std::size_t child_at(std::size_t index, std::size_t ordinal) const noexcept {
        auto current = std::size_t{};
        for (auto child = std::size_t{1}; child < count; ++child) {
            if (!child_of(index, child)) {
                continue;
            }
            if (current == ordinal) {
                return child;
            }
            ++current;
        }

        return AtspiSink::kNodeCapacity + 1;
    }

    const char* name_for(std::size_t index) const noexcept {
        return index == 0 ? "Veyra" : nodes[index].name.data();
    }

    void reply(::DBusMessage* message, ::DBusMessage* response) noexcept {
        dbus_connection_send(bus, response, nullptr);
        dbus_message_unref(response);
    }

    void send_error(::DBusMessage* message, const char* name, const char* text) noexcept {
        auto* error = dbus_message_new_error(message, name, text);
        if (error != nullptr) {
            reply(message, error);
        }
    }

    void begin_window(WindowId window) noexcept;
    void add_node(const AccessibilityNode& record) noexcept;
};

}

#endif
