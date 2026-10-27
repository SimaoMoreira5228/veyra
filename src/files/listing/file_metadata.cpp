#include "file_metadata.hpp"

#include <cstdint>
#include <limits>

#ifdef _WIN32
#include <memory>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <cerrno>
#include <sys/stat.h>
#endif

namespace veyra {

#ifndef _WIN32
namespace {

bool unix_time_nanoseconds(
    std::int64_t seconds,
    std::int64_t subsecond_nanoseconds,
    std::int64_t& result
) noexcept {
    constexpr auto nanoseconds_per_second = std::int64_t{1'000'000'000};
    if (subsecond_nanoseconds < 0 || subsecond_nanoseconds >= nanoseconds_per_second) {
        return false;
    }

    const auto maximum = std::numeric_limits<std::int64_t>::max();
    const auto minimum_seconds = std::numeric_limits<std::int64_t>::min() / nanoseconds_per_second;
    const auto maximum_seconds = maximum / nanoseconds_per_second;
    if (seconds < minimum_seconds || seconds > maximum_seconds) {
        return false;
    }

    const auto value = seconds * nanoseconds_per_second;
    if (value > maximum - subsecond_nanoseconds) {
        return false;
    }
    result = value + subsecond_nanoseconds;
    return true;
}

}
#endif

#ifdef _WIN32
namespace {

constexpr auto windows_epoch_offset = std::uint64_t{116444736000000000};

bool file_time_nanoseconds(FILETIME time, std::int64_t& nanoseconds) noexcept {
    auto ticks = ULARGE_INTEGER{};
    ticks.LowPart = time.dwLowDateTime;
    ticks.HighPart = time.dwHighDateTime;

    const auto maximum_delta = static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
    if (ticks.QuadPart >= windows_epoch_offset) {
        const auto delta = ticks.QuadPart - windows_epoch_offset;
        if (delta > maximum_delta / 100) {
            return false;
        }
        nanoseconds = static_cast<std::int64_t>(delta * 100);
        return true;
    }

    const auto delta = windows_epoch_offset - ticks.QuadPart;
    const auto maximum_negative_delta = maximum_delta / 100;
    if (delta > maximum_negative_delta) {
        return false;
    }
    nanoseconds = -static_cast<std::int64_t>(delta * 100);
    return true;
}

}
#endif

FileMetadata read_file_metadata(const std::filesystem::path& path) noexcept {
#ifdef _WIN32
    const auto raw_handle = CreateFileW(
        path.c_str(),
        FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS,
        nullptr
    );
    if (raw_handle == INVALID_HANDLE_VALUE) {
        return {
            .id = {},
            .size_bytes = 0,
            .modified_nanoseconds = 0,
            .is_directory = false,
            .error = {static_cast<int>(GetLastError()), std::system_category()},
        };
    }

    const auto handle = std::unique_ptr<void, decltype(&CloseHandle)>(raw_handle, CloseHandle);
    auto information = BY_HANDLE_FILE_INFORMATION{};
    if (!GetFileInformationByHandle(handle.get(), &information)) {
        return {
            .id = {},
            .size_bytes = 0,
            .modified_nanoseconds = 0,
            .is_directory = false,
            .error = {static_cast<int>(GetLastError()), std::system_category()},
        };
    }

    const auto id = FileId{
        information.dwVolumeSerialNumber,
        (static_cast<std::uint64_t>(information.nFileIndexHigh) << 32U) | information.nFileIndexLow,
    };
    const auto is_directory = (information.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
    const auto size_bytes = is_directory
        ? std::uint64_t{0}
        : (static_cast<std::uint64_t>(information.nFileSizeHigh) << 32U) | information.nFileSizeLow;
    auto modified_nanoseconds = std::int64_t{0};
    if (!file_time_nanoseconds(information.ftLastWriteTime, modified_nanoseconds)) {
        return {
            .id = id,
            .size_bytes = size_bytes,
            .modified_nanoseconds = 0,
            .is_directory = is_directory,
            .error = std::make_error_code(std::errc::value_too_large),
        };
    }

    return {
        .id = id,
        .size_bytes = size_bytes,
        .modified_nanoseconds = modified_nanoseconds,
        .is_directory = is_directory,
        .error = {},
    };
#else
    struct stat information{};
    if (::stat(path.c_str(), &information) != 0) {
        return {
            .id = {},
            .size_bytes = 0,
            .modified_nanoseconds = 0,
            .is_directory = false,
            .error = {errno, std::generic_category()},
        };
    }

#ifdef __APPLE__
    const auto modified_seconds = information.st_mtimespec.tv_sec;
    const auto modified_nanoseconds_part = information.st_mtimespec.tv_nsec;
#else
    const auto modified_seconds = information.st_mtim.tv_sec;
    const auto modified_nanoseconds_part = information.st_mtim.tv_nsec;
#endif
    const auto is_directory = S_ISDIR(information.st_mode);
    const auto size_bytes = is_directory || !S_ISREG(information.st_mode)
        ? std::uint64_t{0}
        : static_cast<std::uint64_t>(information.st_size);
    const auto id = FileId{
        static_cast<std::uint64_t>(information.st_dev),
        static_cast<std::uint64_t>(information.st_ino),
    };
    auto modified_nanoseconds = std::int64_t{0};
    if (!unix_time_nanoseconds(
            static_cast<std::int64_t>(modified_seconds),
            static_cast<std::int64_t>(modified_nanoseconds_part),
            modified_nanoseconds
        )) {
        return {
            .id = id,
            .size_bytes = size_bytes,
            .modified_nanoseconds = 0,
            .is_directory = is_directory,
            .error = std::make_error_code(std::errc::value_too_large),
        };
    }

    return {
        .id = id,
        .size_bytes = size_bytes,
        .modified_nanoseconds = modified_nanoseconds,
        .is_directory = is_directory,
        .error = {},
    };
#endif
}

}
