#ifndef VEYRA_BEHAVIORS_SPINBOX_HPP
#define VEYRA_BEHAVIORS_SPINBOX_HPP

#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <system_error>

namespace veyra {

enum class SpinboxParseResult : std::uint8_t {
    value,
    rejected,
};

struct SpinboxParsed {
    SpinboxParseResult result{SpinboxParseResult::rejected};
    double value{0.0};
};

enum class SpinboxFormatResult : std::uint8_t {
    complete,
    insufficient_output,
    invalid_input,
};

enum class SpinboxCommitAction : std::uint8_t {
    accept,
    keep_old,
    flag_invalid,
};

struct SpinboxCommit {
    SpinboxCommitAction action{SpinboxCommitAction::flag_invalid};
    double value{0.0};
};

constexpr std::size_t kSpinboxFormatCapacity = 32;

inline SpinboxParsed parse_spinbox_text(std::string_view text) noexcept {
    if (text.empty()) {
        return {};
    }

    if (text.front() == '+') {
        return {};
    }

    std::string_view digits = text;

    if (digits.front() == '-') {
        digits.remove_prefix(1);
    }

    if (digits.size() >= 2 && digits[0] == '0' && (digits[1] == 'x' || digits[1] == 'X')) {
        return {};
    }

    double parsed{};

    const auto outcome = std::from_chars(text.data(), text.data() + text.size(), parsed);

    if (outcome.ec != std::errc{}) {
        return {};
    }

    if (outcome.ptr != text.data() + text.size()) {
        return {};
    }

    if (!std::isfinite(parsed)) {
        return {};
    }

    return {
        .result = SpinboxParseResult::value,
        .value = parsed,
    };
}

inline SpinboxFormatResult
format_spinbox_text(double value, std::span<char> output, std::size_t& length) noexcept {
    length = 0;

    if (!std::isfinite(value)) {
        return SpinboxFormatResult::invalid_input;
    }

    if (output.empty()) {
        return SpinboxFormatResult::insufficient_output;
    }

    const auto outcome = std::to_chars(output.data(), output.data() + output.size(), value);

    if (outcome.ec != std::errc{}) {
        return SpinboxFormatResult::insufficient_output;
    }

    length = static_cast<std::size_t>(outcome.ptr - output.data());

    return SpinboxFormatResult::complete;
}

inline SpinboxCommit resolve_spinbox_commit(SpinboxParsed parsed, double current) noexcept {
    if (parsed.result != SpinboxParseResult::value) {
        return {
            .action = SpinboxCommitAction::flag_invalid,
            .value = current,
        };
    }

    if (parsed.value == current) {
        return {
            .action = SpinboxCommitAction::keep_old,
            .value = current,
        };
    }

    return {
        .action = SpinboxCommitAction::accept,
        .value = parsed.value,
    };
}

}

namespace veyra::behaviors {
using ::veyra::SpinboxCommit;
using ::veyra::SpinboxCommitAction;
using ::veyra::SpinboxFormatResult;
using ::veyra::SpinboxParseResult;
using ::veyra::SpinboxParsed;
using ::veyra::format_spinbox_text;
using ::veyra::kSpinboxFormatCapacity;
using ::veyra::parse_spinbox_text;
using ::veyra::resolve_spinbox_commit;
}

namespace veyra::behaviors::input {
using ::veyra::SpinboxCommit;
using ::veyra::SpinboxCommitAction;
using ::veyra::SpinboxFormatResult;
using ::veyra::SpinboxParseResult;
using ::veyra::SpinboxParsed;
using ::veyra::format_spinbox_text;
using ::veyra::kSpinboxFormatCapacity;
using ::veyra::parse_spinbox_text;
using ::veyra::resolve_spinbox_commit;
}

#endif
