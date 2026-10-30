#ifndef VEYRA_PLATFORM_DIALOG_HPP
#define VEYRA_PLATFORM_DIALOG_HPP

#include <cstdint>

namespace veyra {

enum class ServiceResult : std::uint8_t {
    ok,
    pending,
    invalid_input,
    denied,
    unavailable,
    insufficient_capacity,
    overflow,
    rejected,
    not_found,
};

enum class DialogKind : std::uint8_t {
    alert,
    confirm,
    choice,
};

struct DialogAnswer {
    std::int32_t choice{};
    bool cancelled{};
};

struct DialogOpen {
    ServiceResult result{ServiceResult::rejected};
    std::uint64_t handle{};
};

struct DialogPoll {
    ServiceResult result{ServiceResult::pending};
    DialogAnswer answer{};
};

class DialogService {
  public:
    virtual ~DialogService() = default;

    virtual DialogOpen dialog_open(DialogKind kind) noexcept = 0;
    virtual DialogPoll dialog_poll(std::uint64_t handle) const noexcept = 0;
    virtual ServiceResult dialog_answer_pending(DialogAnswer answer) noexcept = 0;
    virtual ServiceResult dialog_close(std::uint64_t handle) noexcept = 0;
};

}

namespace veyra::platform {
using ::veyra::DialogAnswer;
using ::veyra::DialogKind;
using ::veyra::DialogOpen;
using ::veyra::DialogPoll;
using ::veyra::DialogService;
using ::veyra::ServiceResult;
}

namespace veyra::platform::core {
using ::veyra::DialogAnswer;
using ::veyra::DialogKind;
using ::veyra::DialogOpen;
using ::veyra::DialogPoll;
using ::veyra::DialogService;
using ::veyra::ServiceResult;
}

#endif
