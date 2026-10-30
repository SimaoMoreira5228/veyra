#ifndef VEYRA_PLATFORM_FAKE_SERVICES_HPP
#define VEYRA_PLATFORM_FAKE_SERVICES_HPP

#include "veyra/platform/core/dialog.hpp"
#include "veyra/platform/core/notice.hpp"

#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <span>

namespace veyra {

struct NoticeAsk {
    ServiceResult result{ServiceResult::pending};
    NoticePermission permission{NoticePermission::undecided};
};

struct NoticeRead {
    ServiceResult result{ServiceResult::not_found};
    NoticeAnnouncement announcement{};
};

template <typename Backend>
concept NoticeBackend = requires(
    Backend& backend,
    const Backend& observer,
    std::uint64_t id,
    const char* text,
    std::uint64_t& clicked
) {
    { observer.notice_permission() } noexcept -> std::same_as<NoticePermission>;
    { backend.notice_request_permission() } noexcept -> std::same_as<NoticeAsk>;
    { backend.notice_show(id, text, text) } noexcept -> std::same_as<ServiceResult>;
    { observer.notice_read(id) } noexcept -> std::same_as<NoticeRead>;
    { backend.notice_dismiss(id) } noexcept -> std::same_as<ServiceResult>;
    { observer.notice_visible(id) } noexcept -> std::same_as<bool>;
    { observer.notice_count() } noexcept -> std::same_as<std::size_t>;
    { backend.notice_poll_click(clicked) } noexcept -> std::same_as<bool>;
};

// NOTE: scripted stand-in for OS services the fake never touches. Clipboard,
// dialogs, notifications, and power state all run on injected state the test
// drives; nothing here calls the OS, blocks, or starts a thread. Every answer
// is observed through polling, so headless CI cannot hang on a modal loop.
class FakeServices : public DialogService {
  public:
    static constexpr std::size_t kClipboardCapacity = 256;
    static constexpr std::size_t kDialogScripts = 16;
    static constexpr std::size_t kLiveDialogs = 8;
    static constexpr std::size_t kNotices = 8;
    static constexpr std::size_t kClickQueue = 8;

    FakeServices() noexcept = default;

    FakeServices(const FakeServices&) = delete;
    FakeServices& operator=(const FakeServices&) = delete;
    FakeServices(FakeServices&&) = delete;
    FakeServices& operator=(FakeServices&&) = delete;

    void reset() noexcept;

    ServiceResult clipboard_set(const char* text) noexcept;
    ServiceResult clipboard_get(std::span<char> output, std::size_t& length) noexcept;
    ServiceResult simulate_clipboard_external_write(const char* text) noexcept;
    std::uint64_t clipboard_generation() const noexcept;
    std::size_t clipboard_length() const noexcept;
    bool clipboard_access_allowed() const noexcept;
    bool clipboard_persistent() const noexcept;
    void set_clipboard_access(bool allowed) noexcept;
    void set_clipboard_persistent(bool available) noexcept;

    ServiceResult dialog_script(DialogAnswer answer) noexcept;
    DialogOpen dialog_open(DialogKind kind) noexcept override;
    DialogPoll dialog_poll(std::uint64_t handle) const noexcept override;
    ServiceResult dialog_answer_pending(DialogAnswer answer) noexcept override;
    ServiceResult dialog_close(std::uint64_t handle) noexcept override;
    std::size_t dialog_live_count() const noexcept;
    std::size_t dialog_pending_count() const noexcept;
    std::size_t dialog_requests() const noexcept;
    std::size_t dialog_scripted_count() const noexcept;
    DialogKind last_dialog_kind() const noexcept;

    NoticePermission notice_permission() const noexcept;
    void set_notice_permission(NoticePermission permission) noexcept;
    ServiceResult notice_script_response(NoticePermission permission) noexcept;
    NoticeAsk notice_request_permission() noexcept;
    ServiceResult notice_show(std::uint64_t id, const char* title, const char* body) noexcept;
    NoticeRead notice_read(std::uint64_t id) const noexcept;
    ServiceResult notice_dismiss(std::uint64_t id) noexcept;
    bool notice_visible(std::uint64_t id) const noexcept;
    std::size_t notice_count() const noexcept;
    ServiceResult simulate_notice_click(std::uint64_t id) noexcept;
    bool notice_poll_click(std::uint64_t& id) noexcept;

    bool on_battery() const noexcept;
    void set_on_battery(bool battery) noexcept;
    std::uint64_t power_generation() const noexcept;
    void simulate_suspend_resume() noexcept;

  private:
    struct DialogSlot {
        std::uint64_t handle{};
        DialogKind kind{DialogKind::alert};
        DialogAnswer answer{};
        bool in_use{};
        bool resolved{};
    };

    struct NoticeSlot {
        std::uint64_t id{};
        NoticeAnnouncement announcement{};
    };

    static bool is_valid_answer(DialogAnswer answer) noexcept;
    static std::size_t stored_length(const char* text, bool& fits) noexcept;
    static NoticeAnnouncement
    make_announcement(std::uint64_t id, const char* title, const char* body) noexcept;
    std::size_t notice_index(std::uint64_t id) const noexcept;
    const DialogSlot* find_dialog(std::uint64_t handle) const noexcept;
    DialogSlot* find_dialog(std::uint64_t handle) noexcept;

    std::array<char, kClipboardCapacity> clipboard_{};
    std::size_t clipboard_length_{};
    std::uint64_t clipboard_generation_{};
    bool clipboard_access_{true};
    bool clipboard_persistent_{true};

    std::array<DialogAnswer, kDialogScripts> dialog_scripts_{};
    std::size_t dialog_script_head_{};
    std::size_t dialog_script_count_{};
    std::array<DialogSlot, kLiveDialogs> dialogs_{};
    std::uint64_t next_dialog_handle_{1};
    std::size_t dialog_requests_{};
    DialogKind last_dialog_kind_{DialogKind::alert};

    NoticePermission notice_permission_{NoticePermission::undecided};
    NoticePermission scripted_response_{NoticePermission::undecided};
    bool has_scripted_response_{};
    std::array<NoticeSlot, kNotices> notices_{};
    std::size_t notice_count_{};

    std::array<std::uint64_t, kClickQueue> clicks_{};
    std::size_t click_head_{};
    std::size_t click_count_{};

    bool on_battery_{};
    std::uint64_t power_generation_{};
};

static_assert(NoticeBackend<FakeServices>);

}

namespace veyra::platform {
using ::veyra::DialogAnswer;
using ::veyra::DialogKind;
using ::veyra::DialogOpen;
using ::veyra::DialogPoll;
using ::veyra::FakeServices;
using ::veyra::NoticeAsk;
using ::veyra::NoticeRead;
using ::veyra::ServiceResult;
}

namespace veyra::platform::headless {
using ::veyra::DialogAnswer;
using ::veyra::DialogKind;
using ::veyra::DialogOpen;
using ::veyra::DialogPoll;
using ::veyra::FakeServices;
using ::veyra::NoticeAsk;
using ::veyra::NoticeRead;
using ::veyra::ServiceResult;
}

#endif
