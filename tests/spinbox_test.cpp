#include "veyra/behaviors/spinbox.hpp"

#include "veyra/behaviors/editable_text.hpp"
#include "veyra/behaviors/range.hpp"

#include <array>
#include <iostream>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

const double kQuietNan = std::numeric_limits<double>::quiet_NaN();
const double kInfinity = std::numeric_limits<double>::infinity();

void require(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

veyra::SpinboxParsed parse(std::string_view text) noexcept {
    return veyra::parse_spinbox_text(text);
}

void require_value(std::string_view text, double expected) {
    const veyra::SpinboxParsed parsed = parse(text);

    require(parsed.result == veyra::SpinboxParseResult::value && parsed.value == expected, text);
}

void require_rejected(std::string_view text) {
    require(parse(text).result == veyra::SpinboxParseResult::rejected, text);
}

void check_parse_accepts_plain_numbers() {
    require_value("0", 0.0);
    require_value("42", 42.0);
    require_value("-7", -7.0);
    require_value("3.5", 3.5);
    require_value(".5", 0.5);
    require_value("12.", 12.0);
    require_value("5.", 5.0);
    require_value("00.5", 0.5);
    require_value("-0", 0.0);
    require_value("0e0", 0.0);
    require_value("1e3", 1000.0);
    require_value("1E5", 100000.0);
    require_value("-2.5e-2", -0.025);
    require_value(".5e2", 50.0);
    require_value("1e308", 1e308);
}

void check_parse_rejects_blanks_and_padding() {
    require_rejected("");
    require_rejected(" ");
    require_rejected(" 12");
    require_rejected("\t12");
    require_rejected("12 ");
    require_rejected("12 34");
}

void check_parse_rejects_partial_forms() {
    require_rejected(".");
    require_rejected("-");
    require_rejected("+");
    require_rejected("+12");
    require_rejected("+.5");
    require_rejected("1e");
    require_rejected("1e+");
    require_rejected("e5");
    require_rejected("--1");
}

void check_parse_rejects_hex_and_named_spellings() {
    require_rejected("0x10");
    require_rejected("0X10");
    require_rejected("-0x10");
    require_rejected("0x1p3");
    require_rejected("inf");
    require_rejected("-inf");
    require_rejected("infinity");
    require_rejected("INF");
    require_rejected("nan");
    require_rejected("NAN");
    require_rejected("nan()");
}

void check_parse_rejects_junk_and_overflow() {
    require_rejected("1_000");
    require_rejected("12,5");
    require_rejected("1e9999");
    require_rejected("-1e9999");
    require_rejected("1e309");
    require_rejected("1e-9999");
}

void check_parse_accepts_denormals() {
    const veyra::SpinboxParsed parsed = parse("5e-324");

    require(
        parsed.result == veyra::SpinboxParseResult::value && parsed.value > 0.0 &&
            parsed.value < 1e-307,
        "a denormal that fits commits as parsed instead of rejecting"
    );
}

std::string format(double value) {
    std::array<char, veyra::kSpinboxFormatCapacity> storage{};

    std::size_t length = 0;

    const auto result = veyra::format_spinbox_text(value, storage, length);

    require(
        result == veyra::SpinboxFormatResult::complete,
        "values used for round-trips must format cleanly"
    );

    return std::string(storage.data(), length);
}

void check_format_round_trips() {
    const double grid_cases[] = {0.0, 1.0, 42.0, -7.5, 0.3, 0.1 + 0.2, 1e308, 5e-324};

    for (const double value : grid_cases) {
        const std::string text = format(value);
        const veyra::SpinboxParsed parsed = parse(text);

        require(parsed.result == veyra::SpinboxParseResult::value && parsed.value == value, text);
    }

    require(format(0.3) == "0.3", "a grid value like 0.3 formats short");
    require(
        format(0.1 + 0.2) == "0.30000000000000004",
        "a float sum formats exact, pretty-printing stays with the app"
    );
}

void check_format_rejects_and_overflows() {
    std::array<char, veyra::kSpinboxFormatCapacity> storage{};

    std::size_t length = 0;

    require(
        veyra::format_spinbox_text(kQuietNan, storage, length) ==
                veyra::SpinboxFormatResult::invalid_input &&
            length == 0,
        "NaN never formats, it reports invalid input"
    );
    require(
        veyra::format_spinbox_text(kInfinity, storage, length) ==
                veyra::SpinboxFormatResult::invalid_input &&
            length == 0,
        "infinity never formats, it reports invalid input"
    );
    require(
        veyra::format_spinbox_text(1.0, {}, length) ==
                veyra::SpinboxFormatResult::insufficient_output &&
            length == 0,
        "an empty span reports insufficient output"
    );

    std::array<char, 2> tiny{};

    require(
        veyra::format_spinbox_text(100.0, tiny, length) ==
                veyra::SpinboxFormatResult::insufficient_output &&
            length == 0,
        "a span shorter than the digits reports insufficient output"
    );

    std::array<char, veyra::kSpinboxFormatCapacity> room{};

    require(
        veyra::format_spinbox_text(std::numeric_limits<double>::max(), room, length) ==
            veyra::SpinboxFormatResult::complete,
        "the documented capacity fits the longest shortest-repr double"
    );
}

void check_commit_policy_mapping() {
    const veyra::SpinboxCommit flagged = veyra::resolve_spinbox_commit(parse("1e"), 40.0);

    require(
        flagged.action == veyra::SpinboxCommitAction::flag_invalid && flagged.value == 40.0,
        "a rejected parse flags invalid and echoes the stored value"
    );

    const veyra::SpinboxCommit kept = veyra::resolve_spinbox_commit(parse("40"), 40.0);

    require(
        kept.action == veyra::SpinboxCommitAction::keep_old && kept.value == 40.0,
        "text matching the stored value keeps old with no set_value call"
    );

    const veyra::SpinboxCommit accepted = veyra::resolve_spinbox_commit(parse("42.6"), 40.0);

    require(
        accepted.action == veyra::SpinboxCommitAction::accept && accepted.value == 42.6,
        "a new number accepts with the raw parsed value for set_value to snap"
    );
}

void check_wiring_through_range_and_text() {
    std::array<char, 64> storage{};

    auto buffer = veyra::TextEditorBuffer{std::span<char>{storage}, std::string_view{}};
    auto field = veyra::EditableTextController{buffer};
    auto range = veyra::RangeController{};

    require(
        field.insert("42.6").result == veyra::EditableTextResult::changed,
        "typed digits land in the text field"
    );

    std::array<char, 64> readout{};
    std::size_t readout_length = 0;

    field.select_all();

    require(
        field.copy_selection(readout, readout_length) == veyra::TextEditResult::complete,
        "the committed field text reads back for the codec"
    );

    const veyra::SpinboxCommit commit =
        veyra::resolve_spinbox_commit(parse({readout.data(), readout_length}), range.value());

    require(
        commit.action == veyra::SpinboxCommitAction::accept,
        "fresh digits accept against the stored range value"
    );

    const veyra::RangeTransition moved = range.set_value(commit.value);

    require(
        moved.result == veyra::RangeResult::changed && moved.value == 43.0,
        "the accepted value commits through set_value, which clamps and snaps"
    );
    require(
        field.set_invalid(false) == false,
        "a committed value clears the invalid flag with no duplicate report"
    );

    const std::string shown = format(range.value());

    require(
        field.clear().result == veyra::EditableTextResult::changed,
        "clearing makes room for the formatted range value"
    );
    require(
        field.insert(shown).result == veyra::EditableTextResult::changed,
        "the formatted range value writes back into the field"
    );

    field.select_all();

    require(
        field.copy_selection(readout, readout_length) == veyra::TextEditResult::complete &&
            std::string_view{readout.data(), readout_length} == "43",
        "the field shows the snapped value, not the typed draft"
    );

    require(
        field.clear().result == veyra::EditableTextResult::changed &&
            field.insert("1e").result == veyra::EditableTextResult::changed,
        "a partial draft can sit in the field while typing"
    );

    field.select_all();

    require(
        field.copy_selection(readout, readout_length) == veyra::TextEditResult::complete,
        "the partial draft reads back at submit time"
    );

    const veyra::SpinboxCommit rejected =
        veyra::resolve_spinbox_commit(parse({readout.data(), readout_length}), range.value());

    require(
        rejected.action == veyra::SpinboxCommitAction::flag_invalid &&
            rejected.value == range.value(),
        "the partial draft flags invalid instead of committing"
    );
    require(field.set_invalid(true), "flagging invalid reports the change once");
    require(range.value() == 43.0, "a flagged submit leaves the stored range value untouched");
}

}

int main() {
    try {
        check_parse_accepts_plain_numbers();
        check_parse_rejects_blanks_and_padding();
        check_parse_rejects_partial_forms();
        check_parse_rejects_hex_and_named_spellings();
        check_parse_rejects_junk_and_overflow();
        check_parse_accepts_denormals();
        check_format_round_trips();
        check_format_rejects_and_overflows();
        check_commit_policy_mapping();
        check_wiring_through_range_and_text();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
