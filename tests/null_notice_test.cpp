#include "veyra/platform/null_window.hpp"

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string_view>

namespace {

void require(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

void check_permission_is_granted() {
    veyra::NullNotices backend{};

    require(
        backend.notice_permission() == veyra::NoticePermission::granted,
        "a null backend reports granted"
    );

    const auto ask = backend.notice_request_permission();

    require(
        ask.result == veyra::ServiceResult::ok &&
            ask.permission == veyra::NoticePermission::granted,
        "a null backend answers at once instead of pending"
    );
}

void check_show_accepts_and_drops() {
    veyra::NullNotices backend{};

    require(
        backend.notice_show(1, "Title", "Body") == veyra::ServiceResult::ok,
        "a valid show is accepted"
    );
    require(!backend.notice_visible(1), "an accepted show presents nothing");
    require(backend.notice_count() == 0, "an accepted show retains nothing");

    const auto read = backend.notice_read(1);

    require(
        read.result == veyra::ServiceResult::not_found,
        "an accepted show leaves nothing to read back"
    );

    auto clicked = std::uint64_t{};

    require(!backend.notice_poll_click(clicked), "an accepted show never clicks");
    require(clicked == 0, "a quiet poll leaves its output alone");
}

void check_invalid_input_rejected() {
    veyra::NullNotices backend{};

    require(
        backend.notice_show(0, "Title", "Body") == veyra::ServiceResult::invalid_input,
        "a zero id is rejected"
    );
    require(
        backend.notice_show(1, nullptr, "Body") == veyra::ServiceResult::invalid_input,
        "a null title is rejected"
    );
    require(
        backend.notice_show(1, "Title", nullptr) == veyra::ServiceResult::invalid_input,
        "a null body is rejected"
    );
}

void check_dismiss_finds_nothing() {
    veyra::NullNotices backend{};

    require(
        backend.notice_show(1, "Title", "Body") == veyra::ServiceResult::ok,
        "a valid show is accepted"
    );
    require(
        backend.notice_dismiss(1) == veyra::ServiceResult::not_found,
        "a dropped show has nothing to dismiss"
    );
    require(
        backend.notice_dismiss(99) == veyra::ServiceResult::not_found,
        "an unknown id has nothing to dismiss"
    );
}

}

int main() {
    try {
        check_permission_is_granted();
        check_show_accepts_and_drops();
        check_invalid_input_rejected();
        check_dismiss_finds_nothing();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
