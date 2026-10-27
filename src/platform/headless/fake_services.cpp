#include "veyra/platform/fake_services.hpp"

namespace veyra {

void FakeServices::reset() noexcept {
    for (std::size_t index = 0; index < kClipboardCapacity; ++index) {
        clipboard_[index] = 0;
    }

    clipboard_length_ = 0;
    clipboard_generation_ = 0;
    clipboard_access_ = true;
    clipboard_persistent_ = true;

    for (std::size_t index = 0; index < kDialogScripts; ++index) {
        dialog_scripts_[index] = {};
    }

    dialog_script_head_ = 0;
    dialog_script_count_ = 0;

    for (std::size_t index = 0; index < kLiveDialogs; ++index) {
        dialogs_[index] = {};
    }

    next_dialog_handle_ = 1;
    dialog_requests_ = 0;
    last_dialog_kind_ = DialogKind::alert;

    notice_permission_ = NoticePermission::undecided;
    scripted_response_ = NoticePermission::undecided;
    has_scripted_response_ = false;

    for (std::size_t index = 0; index < kNotices; ++index) {
        notices_[index] = {};
    }

    notice_count_ = 0;

    for (std::size_t index = 0; index < kClickQueue; ++index) {
        clicks_[index] = 0;
    }

    click_head_ = 0;
    click_count_ = 0;

    on_battery_ = false;
    power_generation_ = 0;
}

ServiceResult FakeServices::clipboard_set(const char* text) noexcept {
    if (text == nullptr) {
        return ServiceResult::invalid_input;
    }

    if (!clipboard_access_) {
        return ServiceResult::denied;
    }

    if (!clipboard_persistent_) {
        return ServiceResult::unavailable;
    }

    bool fits{};

    const std::size_t length = stored_length(text, fits);

    if (!fits) {
        return ServiceResult::rejected;
    }

    for (std::size_t index = 0; index < length; ++index) {
        clipboard_[index] = text[index];
    }

    clipboard_length_ = length;
    ++clipboard_generation_;

    return ServiceResult::ok;
}

ServiceResult FakeServices::clipboard_get(std::span<char> output, std::size_t& length) noexcept {
    if (!clipboard_access_) {
        length = 0;

        return ServiceResult::denied;
    }

    length = clipboard_length_;

    if (clipboard_length_ > output.size()) {
        return ServiceResult::insufficient_capacity;
    }

    for (std::size_t index = 0; index < clipboard_length_; ++index) {
        output[index] = clipboard_[index];
    }

    return ServiceResult::ok;
}

ServiceResult FakeServices::simulate_clipboard_external_write(const char* text) noexcept {
    if (text == nullptr) {
        return ServiceResult::invalid_input;
    }

    if (!clipboard_persistent_) {
        return ServiceResult::unavailable;
    }

    bool fits{};

    const std::size_t length = stored_length(text, fits);

    if (!fits) {
        return ServiceResult::rejected;
    }

    for (std::size_t index = 0; index < length; ++index) {
        clipboard_[index] = text[index];
    }

    clipboard_length_ = length;
    ++clipboard_generation_;

    return ServiceResult::ok;
}

std::uint64_t FakeServices::clipboard_generation() const noexcept {
    return clipboard_generation_;
}

std::size_t FakeServices::clipboard_length() const noexcept {
    return clipboard_length_;
}

bool FakeServices::clipboard_access_allowed() const noexcept {
    return clipboard_access_;
}

bool FakeServices::clipboard_persistent() const noexcept {
    return clipboard_persistent_;
}

void FakeServices::set_clipboard_access(bool allowed) noexcept {
    clipboard_access_ = allowed;
}

void FakeServices::set_clipboard_persistent(bool available) noexcept {
    clipboard_persistent_ = available;
}

ServiceResult FakeServices::dialog_script(DialogAnswer answer) noexcept {
    if (!is_valid_answer(answer)) {
        return ServiceResult::invalid_input;
    }

    if (dialog_script_count_ == kDialogScripts) {
        return ServiceResult::overflow;
    }

    const std::size_t slot = (dialog_script_head_ + dialog_script_count_) % kDialogScripts;

    dialog_scripts_[slot] = answer;
    ++dialog_script_count_;

    return ServiceResult::ok;
}

DialogOpen FakeServices::dialog_open(DialogKind kind) noexcept {
    DialogSlot* free_slot{};

    for (std::size_t index = 0; index < kLiveDialogs; ++index) {
        if (!dialogs_[index].in_use) {
            free_slot = &dialogs_[index];
            break;
        }
    }

    if (free_slot == nullptr) {
        return {.result = ServiceResult::overflow, .handle = 0};
    }

    const std::uint64_t handle = next_dialog_handle_++;

    *free_slot = {
        .handle = handle,
        .kind = kind,
        .answer = {},
        .in_use = true,
        .resolved = false,
    };

    if (dialog_script_count_ > 0) {
        free_slot->answer = dialog_scripts_[dialog_script_head_];
        free_slot->resolved = true;
        dialog_script_head_ = (dialog_script_head_ + 1) % kDialogScripts;
        --dialog_script_count_;
    }

    ++dialog_requests_;
    last_dialog_kind_ = kind;

    return {.result = ServiceResult::ok, .handle = handle};
}

DialogPoll FakeServices::dialog_poll(std::uint64_t handle) const noexcept {
    const DialogSlot* slot = find_dialog(handle);

    if (slot == nullptr) {
        return {.result = ServiceResult::not_found, .answer = {}};
    }

    if (!slot->resolved) {
        return {.result = ServiceResult::pending, .answer = {}};
    }

    return {.result = ServiceResult::ok, .answer = slot->answer};
}

ServiceResult FakeServices::dialog_answer_pending(DialogAnswer answer) noexcept {
    if (!is_valid_answer(answer)) {
        return ServiceResult::invalid_input;
    }

    DialogSlot* oldest{};

    for (std::size_t index = 0; index < kLiveDialogs; ++index) {
        if (!dialogs_[index].in_use || dialogs_[index].resolved) {
            continue;
        }

        if (oldest == nullptr || dialogs_[index].handle < oldest->handle) {
            oldest = &dialogs_[index];
        }
    }

    if (oldest == nullptr) {
        return ServiceResult::not_found;
    }

    oldest->answer = answer;
    oldest->resolved = true;

    return ServiceResult::ok;
}

ServiceResult FakeServices::dialog_close(std::uint64_t handle) noexcept {
    DialogSlot* slot = find_dialog(handle);

    if (slot == nullptr) {
        return ServiceResult::not_found;
    }

    *slot = {};

    return ServiceResult::ok;
}

std::size_t FakeServices::dialog_live_count() const noexcept {
    std::size_t live{};

    for (std::size_t index = 0; index < kLiveDialogs; ++index) {
        if (dialogs_[index].in_use) {
            ++live;
        }
    }

    return live;
}

std::size_t FakeServices::dialog_pending_count() const noexcept {
    std::size_t pending{};

    for (std::size_t index = 0; index < kLiveDialogs; ++index) {
        if (dialogs_[index].in_use && !dialogs_[index].resolved) {
            ++pending;
        }
    }

    return pending;
}

std::size_t FakeServices::dialog_requests() const noexcept {
    return dialog_requests_;
}

std::size_t FakeServices::dialog_scripted_count() const noexcept {
    return dialog_script_count_;
}

DialogKind FakeServices::last_dialog_kind() const noexcept {
    return last_dialog_kind_;
}

NoticePermission FakeServices::notice_permission() const noexcept {
    return notice_permission_;
}

void FakeServices::set_notice_permission(NoticePermission permission) noexcept {
    notice_permission_ = permission;
}

ServiceResult FakeServices::notice_script_response(NoticePermission permission) noexcept {
    if (permission == NoticePermission::undecided) {
        return ServiceResult::invalid_input;
    }

    scripted_response_ = permission;
    has_scripted_response_ = true;

    return ServiceResult::ok;
}

NoticeAsk FakeServices::notice_request_permission() noexcept {
    if (notice_permission_ != NoticePermission::undecided) {
        return {.result = ServiceResult::ok, .permission = notice_permission_};
    }

    if (!has_scripted_response_) {
        return {.result = ServiceResult::pending, .permission = NoticePermission::undecided};
    }

    notice_permission_ = scripted_response_;
    has_scripted_response_ = false;

    return {.result = ServiceResult::ok, .permission = notice_permission_};
}

NoticeAnnouncement
FakeServices::make_announcement(std::uint64_t id, const char* title, const char* body) noexcept {
    NoticeAnnouncement announcement{};

    announcement.id = id;

    const NoticeTextCopy title_copy = copy_notice_text(title, announcement.title);

    announcement.title_length = title_copy.length;
    announcement.title_truncated = title_copy.truncated;

    const NoticeTextCopy body_copy = copy_notice_text(body, announcement.body);

    announcement.body_length = body_copy.length;
    announcement.body_truncated = body_copy.truncated;

    return announcement;
}

ServiceResult
FakeServices::notice_show(std::uint64_t id, const char* title, const char* body) noexcept {
    if (id == 0) {
        return ServiceResult::invalid_input;
    }

    if (title == nullptr || body == nullptr) {
        return ServiceResult::invalid_input;
    }

    if (notice_permission_ != NoticePermission::granted) {
        return ServiceResult::denied;
    }

    const std::size_t slot = notice_index(id);

    if (slot != kNotices) {
        notices_[slot].announcement = make_announcement(id, title, body);

        return ServiceResult::ok;
    }

    if (notice_count_ == kNotices) {
        return ServiceResult::overflow;
    }

    notices_[notice_count_] = {.id = id, .announcement = make_announcement(id, title, body)};
    ++notice_count_;

    return ServiceResult::ok;
}

NoticeRead FakeServices::notice_read(std::uint64_t id) const noexcept {
    const std::size_t slot = notice_index(id);

    if (slot == kNotices) {
        return {.result = ServiceResult::not_found, .announcement = {}};
    }

    return {.result = ServiceResult::ok, .announcement = notices_[slot].announcement};
}

ServiceResult FakeServices::notice_dismiss(std::uint64_t id) noexcept {
    const std::size_t slot = notice_index(id);

    if (slot == kNotices) {
        return ServiceResult::not_found;
    }

    notices_[slot] = notices_[notice_count_ - 1];
    notices_[notice_count_ - 1] = {};
    --notice_count_;

    std::size_t kept{};

    for (std::size_t index = 0; index < click_count_; ++index) {
        const std::size_t source = (click_head_ + index) % kClickQueue;

        if (clicks_[source] == id) {
            continue;
        }

        clicks_[kept] = clicks_[source];
        ++kept;
    }

    click_head_ = 0;
    click_count_ = kept;

    return ServiceResult::ok;
}

bool FakeServices::notice_visible(std::uint64_t id) const noexcept {
    return notice_index(id) != kNotices;
}

std::size_t FakeServices::notice_count() const noexcept {
    return notice_count_;
}

ServiceResult FakeServices::simulate_notice_click(std::uint64_t id) noexcept {
    if (notice_index(id) == kNotices) {
        return ServiceResult::not_found;
    }

    if (click_count_ == kClickQueue) {
        return ServiceResult::overflow;
    }

    const std::size_t slot = (click_head_ + click_count_) % kClickQueue;

    clicks_[slot] = id;
    ++click_count_;

    return ServiceResult::ok;
}

bool FakeServices::notice_poll_click(std::uint64_t& id) noexcept {
    if (click_count_ == 0) {
        return false;
    }

    id = clicks_[click_head_];
    clicks_[click_head_] = 0;
    click_head_ = (click_head_ + 1) % kClickQueue;
    --click_count_;

    return true;
}

bool FakeServices::on_battery() const noexcept {
    return on_battery_;
}

void FakeServices::set_on_battery(bool battery) noexcept {
    on_battery_ = battery;
}

std::uint64_t FakeServices::power_generation() const noexcept {
    return power_generation_;
}

void FakeServices::simulate_suspend_resume() noexcept {
    ++power_generation_;
}

bool FakeServices::is_valid_answer(DialogAnswer answer) noexcept {
    if (answer.cancelled) {
        return true;
    }

    return answer.choice >= 0;
}

std::size_t FakeServices::stored_length(const char* text, bool& fits) noexcept {
    std::size_t length{};

    while (text[length] != '\0') {
        ++length;

        if (length > kClipboardCapacity) {
            fits = false;

            return length;
        }
    }

    fits = true;

    return length;
}

std::size_t FakeServices::notice_index(std::uint64_t id) const noexcept {
    for (std::size_t index = 0; index < notice_count_; ++index) {
        if (notices_[index].id == id) {
            return index;
        }
    }

    return kNotices;
}

const FakeServices::DialogSlot* FakeServices::find_dialog(std::uint64_t handle) const noexcept {
    if (handle == 0) {
        return nullptr;
    }

    for (std::size_t index = 0; index < kLiveDialogs; ++index) {
        if (dialogs_[index].in_use && dialogs_[index].handle == handle) {
            return &dialogs_[index];
        }
    }

    return nullptr;
}

FakeServices::DialogSlot* FakeServices::find_dialog(std::uint64_t handle) noexcept {
    if (handle == 0) {
        return nullptr;
    }

    for (std::size_t index = 0; index < kLiveDialogs; ++index) {
        if (dialogs_[index].in_use && dialogs_[index].handle == handle) {
            return &dialogs_[index];
        }
    }

    return nullptr;
}

}
