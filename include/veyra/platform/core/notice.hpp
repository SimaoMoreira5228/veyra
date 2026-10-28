#ifndef VEYRA_PLATFORM_NOTICE_HPP
#define VEYRA_PLATFORM_NOTICE_HPP

#include <array>
#include <cstddef>
#include <cstdint>

namespace veyra {

inline constexpr std::size_t kNoticeTitleCapacity = 64;
inline constexpr std::size_t kNoticeBodyCapacity = 256;

enum class NoticePermission : std::uint8_t {
    undecided,
    granted,
    denied,
};

// NOTE: the announcement owns its text for as long as the record lives. The
// caller lends title and body for the duration of the show call only; any
// backend that needs them past return copies during the call. Inline bounds
// keep that copy fixed-size and headless tests byte-exact.
struct NoticeAnnouncement {
    std::uint64_t id{};
    std::array<char, kNoticeTitleCapacity> title{};
    std::uint32_t title_length{};
    bool title_truncated{};
    std::array<char, kNoticeBodyCapacity> body{};
    std::uint32_t body_length{};
    bool body_truncated{};
};

struct NoticeTextCopy {
    bool valid{};
    std::uint32_t length{};
    bool truncated{};
};

template <std::size_t Capacity>
NoticeTextCopy copy_notice_text(const char* text, std::array<char, Capacity>& storage) noexcept {
    static_assert(Capacity > 0);

    static_assert(Capacity <= 4096);

    NoticeTextCopy copy{};

    if (text == nullptr) {
        return copy;
    }

    copy.valid = true;

    std::size_t length{};

    while (text[length] != '\0') {
        if (length == Capacity) {
            copy.truncated = true;

            break;
        }

        storage[length] = text[length];
        ++length;
    }

    copy.length = static_cast<std::uint32_t>(length);

    for (std::size_t index = length; index < Capacity; ++index) {
        storage[index] = 0;
    }

    return copy;
}

}

namespace veyra::platform {
using ::veyra::NoticeAnnouncement;
using ::veyra::NoticePermission;
using ::veyra::NoticeTextCopy;
using ::veyra::copy_notice_text;
using ::veyra::kNoticeBodyCapacity;
using ::veyra::kNoticeTitleCapacity;
}

namespace veyra::platform::core {
using ::veyra::NoticeAnnouncement;
using ::veyra::NoticePermission;
using ::veyra::NoticeTextCopy;
using ::veyra::copy_notice_text;
using ::veyra::kNoticeBodyCapacity;
using ::veyra::kNoticeTitleCapacity;
}

#endif
