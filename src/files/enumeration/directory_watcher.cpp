#include "veyra/files/enumeration/directory_watcher.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstddef>
#include <limits>
#include <span>
#include <stop_token>
#include <system_error>

#ifdef _WIN32
#include <windows.h>
#elif defined(__linux__)
#include <poll.h>
#include <sys/eventfd.h>
#include <sys/inotify.h>
#include <unistd.h>
#endif

namespace veyra {
namespace {

constexpr auto quiet_period = std::chrono::milliseconds{50};

#ifdef _WIN32
HANDLE native_handle(std::intptr_t value) noexcept {
    return reinterpret_cast<HANDLE>(value);
}

std::intptr_t stored_handle(HANDLE handle) noexcept {
    return reinterpret_cast<std::intptr_t>(handle);
}

void cancel_read(HANDLE directory, OVERLAPPED& operation, HANDLE completion) noexcept {
    CancelIoEx(directory, &operation);
    WaitForSingleObject(completion, INFINITE);
}

bool read_changes(
    HANDLE directory,
    HANDLE completion,
    HANDLE cancellation,
    OVERLAPPED& operation,
    std::span<std::byte> buffer,
    DWORD timeout,
    bool& changed,
    std::error_code& error
) noexcept {
    ResetEvent(completion);
    operation = {};
    operation.hEvent = completion;

    const auto started = ReadDirectoryChangesW(
        directory,
        buffer.data(),
        static_cast<DWORD>(buffer.size()),
        FALSE,
        FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME | FILE_NOTIFY_CHANGE_SIZE |
            FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_ATTRIBUTES |
            FILE_NOTIFY_CHANGE_CREATION,
        nullptr,
        &operation,
        nullptr
    );
    if (!started && GetLastError() != ERROR_IO_PENDING) {
        error = {static_cast<int>(GetLastError()), std::system_category()};
        return false;
    }

    const auto events = std::array{completion, cancellation};
    const auto result =
        WaitForMultipleObjects(static_cast<DWORD>(events.size()), events.data(), FALSE, timeout);
    if (result == WAIT_OBJECT_0 + 1) {
        cancel_read(directory, operation, completion);
        return false;
    }
    if (result == WAIT_TIMEOUT) {
        cancel_read(directory, operation, completion);
        return true;
    }
    if (result != WAIT_OBJECT_0) {
        const auto wait_error = GetLastError();
        cancel_read(directory, operation, completion);
        error = {static_cast<int>(wait_error), std::system_category()};
        return false;
    }

    auto bytes_returned = DWORD{0};
    if (!GetOverlappedResult(directory, &operation, &bytes_returned, FALSE)) {
        const auto result_error = GetLastError();
        if (result_error == ERROR_NOTIFY_ENUM_DIR) {
            changed = true;
            return true;
        }
        if (result_error == ERROR_OPERATION_ABORTED) {
            return false;
        }
        error = {static_cast<int>(result_error), std::system_category()};
        return false;
    }

    changed = true;
    return true;
}
#elif defined(__linux__)
int poll_timeout(std::chrono::milliseconds timeout) noexcept {
    if (timeout == std::chrono::milliseconds::max()) {
        return -1;
    }
    if (timeout <= std::chrono::milliseconds::zero()) {
        return 0;
    }
    return static_cast<int>(std::min(
        timeout.count(),
        static_cast<std::chrono::milliseconds::rep>(std::numeric_limits<int>::max())
    ));
}

bool clear_cancellation(int descriptor, std::error_code& error) noexcept {
    auto value = std::uint64_t{};
    while (true) {
        const auto count = ::read(descriptor, &value, sizeof(value));
        if (count == static_cast<ssize_t>(sizeof(value)) || (count < 0 && errno == EAGAIN)) {
            return true;
        }
        if (count < 0 && errno == EINTR) {
            continue;
        }
        error = count < 0 ? std::error_code{errno, std::generic_category()}
                          : std::make_error_code(std::errc::io_error);
        return false;
    }
}

int wait_for_descriptors(int notification, int cancellation, int timeout) noexcept {
    auto descriptors = std::array{
        pollfd{
            .fd = notification,
            .events = POLLIN,
        },
        pollfd{
            .fd = cancellation,
            .events = POLLIN,
        },
    };
    auto ready = ::poll(descriptors.data(), descriptors.size(), timeout);
    while (ready < 0 && errno == EINTR) {
        ready = ::poll(descriptors.data(), descriptors.size(), timeout);
    }
    return ready;
}

bool drain_notifications(int descriptor, bool& changed, std::error_code& error) noexcept {
    alignas(inotify_event) auto buffer = std::array<std::byte, 8192>{};
    while (true) {
        const auto count = ::read(descriptor, buffer.data(), buffer.size());
        if (count > 0) {
            changed = true;
            auto offset = std::size_t{0};
            const auto available = static_cast<std::size_t>(count);
            while (offset < available) {
                const auto* event = reinterpret_cast<const inotify_event*>(buffer.data() + offset);
                if ((event->mask & (IN_DELETE_SELF | IN_MOVE_SELF | IN_IGNORED)) != 0) {
                    error = std::make_error_code(std::errc::no_such_file_or_directory);
                    return false;
                }
                offset += sizeof(inotify_event) + event->len;
            }
            continue;
        }
        if (count == 0 || errno == EAGAIN) {
            return true;
        }
        if (errno == EINTR) {
            continue;
        }
        error = {errno, std::generic_category()};
        return false;
    }
}
#endif

}

DirectoryWatcher::DirectoryWatcher(
    const std::filesystem::path& directory,
    std::error_code& error
) noexcept {
    error.clear();
    if (!directory.is_absolute()) {
        error_ = std::make_error_code(std::errc::invalid_argument);
        error = error_;
        return;
    }

#ifdef _WIN32
    const auto raw_directory = CreateFileW(
        directory.c_str(),
        FILE_LIST_DIRECTORY,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED,
        nullptr
    );
    if (raw_directory == INVALID_HANDLE_VALUE) {
        error_ = {static_cast<int>(GetLastError()), std::system_category()};
        error = error_;
        return;
    }
    auto information = BY_HANDLE_FILE_INFORMATION{};
    if (!GetFileInformationByHandle(raw_directory, &information) ||
        (information.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0) {
        CloseHandle(raw_directory);
        error_ = std::make_error_code(std::errc::not_a_directory);
        error = error_;
        return;
    }
    directory_handle_ = stored_handle(raw_directory);

    const auto completion = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (completion == nullptr) {
        error_ = {static_cast<int>(GetLastError()), std::system_category()};
        error = error_;
        close();
        return;
    }
    completion_event_ = stored_handle(completion);

    const auto cancellation = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (cancellation == nullptr) {
        error_ = {static_cast<int>(GetLastError()), std::system_category()};
        error = error_;
        close();
        return;
    }
    cancellation_event_ = stored_handle(cancellation);
#elif defined(__linux__)
    notification_descriptor_ = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
    if (notification_descriptor_ < 0) {
        error_ = {errno, std::generic_category()};
        error = error_;
        return;
    }

    cancellation_descriptor_ = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (cancellation_descriptor_ < 0) {
        error_ = {errno, std::generic_category()};
        error = error_;
        close();
        return;
    }

    const auto mask = IN_CREATE | IN_DELETE | IN_MODIFY | IN_ATTRIB | IN_CLOSE_WRITE |
        IN_MOVED_FROM | IN_MOVED_TO | IN_DELETE_SELF | IN_MOVE_SELF;
    watch_descriptor_ =
        inotify_add_watch(notification_descriptor_, directory.c_str(), mask | IN_ONLYDIR);
    if (watch_descriptor_ < 0) {
        error_ = {errno, std::generic_category()};
        error = error_;
        close();
        return;
    }
#else
    error_ = std::make_error_code(std::errc::operation_not_supported);
    error = error_;
#endif
}

DirectoryWatcher::~DirectoryWatcher() {
    close();
}

DirectoryWatchResult DirectoryWatcher::wait_for_change(
    const std::stop_token& stop_token,
    std::chrono::milliseconds timeout,
    std::error_code& error
) noexcept {
    error.clear();
    if (error_) {
        error = error_;
        return DirectoryWatchResult::error;
    }
    if (stop_token.stop_requested()) {
        return DirectoryWatchResult::stopped;
    }

#ifdef _WIN32
    const auto directory = native_handle(directory_handle_);
    const auto completion = native_handle(completion_event_);
    const auto cancellation = native_handle(cancellation_event_);
    if (!ResetEvent(cancellation)) {
        error = {static_cast<int>(GetLastError()), std::system_category()};
        return DirectoryWatchResult::error;
    }
    const std::stop_callback wake(stop_token, [cancellation] {
        SetEvent(cancellation);
    });
    alignas(DWORD) auto buffer = std::array<std::byte, 16 * 1024>{};
    auto operation = OVERLAPPED{};
    auto changed = false;
    const auto maximum_timeout =
        static_cast<std::chrono::milliseconds::rep>(std::numeric_limits<DWORD>::max() - 1);
    const auto first_timeout = timeout == std::chrono::milliseconds::max()
        ? INFINITE
        : static_cast<DWORD>(
              std::clamp<std::chrono::milliseconds::rep>(timeout.count(), 0, maximum_timeout)
          );
    if (!read_changes(
            directory,
            completion,
            cancellation,
            operation,
            buffer,
            first_timeout,
            changed,
            error
        )) {
        if (stop_token.stop_requested()) {
            return DirectoryWatchResult::stopped;
        }
        return error ? DirectoryWatchResult::error : DirectoryWatchResult::timeout;
    }
    if (!changed) {
        return DirectoryWatchResult::timeout;
    }

    while (true) {
        changed = false;
        if (!read_changes(
                directory,
                completion,
                cancellation,
                operation,
                buffer,
                static_cast<DWORD>(quiet_period.count()),
                changed,
                error
            )) {
            if (stop_token.stop_requested()) {
                return DirectoryWatchResult::stopped;
            }
            return error ? DirectoryWatchResult::error : DirectoryWatchResult::changed;
        }
        if (!changed) {
            return DirectoryWatchResult::changed;
        }
    }
#elif defined(__linux__)
    if (!clear_cancellation(cancellation_descriptor_, error)) {
        return DirectoryWatchResult::error;
    }
    const std::stop_callback wake(stop_token, [descriptor = cancellation_descriptor_] {
        const auto value = std::uint64_t{1};
        const auto result = ::write(descriptor, &value, sizeof(value));
        static_cast<void>(result);
    });
    auto changed = false;
    auto ready = wait_for_descriptors(
        notification_descriptor_,
        cancellation_descriptor_,
        poll_timeout(timeout)
    );
    if (ready == 0) {
        return DirectoryWatchResult::timeout;
    }
    if (ready < 0) {
        error = {errno, std::generic_category()};
        return DirectoryWatchResult::error;
    }
    if (stop_token.stop_requested()) {
        return DirectoryWatchResult::stopped;
    }
    if (!drain_notifications(notification_descriptor_, changed, error)) {
        return DirectoryWatchResult::error;
    }

    while (true) {
        ready = wait_for_descriptors(
            notification_descriptor_,
            cancellation_descriptor_,
            static_cast<int>(quiet_period.count())
        );
        if (ready == 0) {
            return DirectoryWatchResult::changed;
        }
        if (ready < 0) {
            error = {errno, std::generic_category()};
            return DirectoryWatchResult::error;
        }
        if (stop_token.stop_requested()) {
            return DirectoryWatchResult::stopped;
        }
        if (!drain_notifications(notification_descriptor_, changed, error)) {
            return DirectoryWatchResult::error;
        }
    }
#else
    static_cast<void>(timeout);
    error = std::make_error_code(std::errc::operation_not_supported);
    return DirectoryWatchResult::error;
#endif
}

void DirectoryWatcher::close() noexcept {
#ifdef _WIN32
    if (directory_handle_ != -1) {
        CloseHandle(native_handle(directory_handle_));
        directory_handle_ = -1;
    }
    if (completion_event_ != -1) {
        CloseHandle(native_handle(completion_event_));
        completion_event_ = -1;
    }
    if (cancellation_event_ != -1) {
        CloseHandle(native_handle(cancellation_event_));
        cancellation_event_ = -1;
    }
#elif defined(__linux__)
    if (notification_descriptor_ >= 0 && watch_descriptor_ >= 0) {
        inotify_rm_watch(notification_descriptor_, watch_descriptor_);
        watch_descriptor_ = -1;
    }
    if (notification_descriptor_ >= 0) {
        ::close(notification_descriptor_);
        notification_descriptor_ = -1;
    }
    if (cancellation_descriptor_ >= 0) {
        ::close(cancellation_descriptor_);
        cancellation_descriptor_ = -1;
    }
#endif
}

}
