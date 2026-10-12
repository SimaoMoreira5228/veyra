#define STB_SPRINTF_IMPLEMENTATION
#include "stb_sprintf.h"

#include <cstdarg>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

void require(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

void require_format(const char* expected, const char* format, ...) {
    char buffer[256]{};

    std::va_list args;

    va_start(args, format);

    const int length = stbsp_vsnprintf(buffer, sizeof(buffer), format, args);

    va_end(args);

    require(
        length == static_cast<int>(std::strlen(expected)),
        "formatted length must match exactly"
    );
    require(std::string_view{buffer} == expected, expected);
}

void check_signed_integers() {
    require_format("-12345", "%d", -12345);
    require_format("-9223372036854775808", "%lld", std::numeric_limits<long long>::min());
    require_format("-9876543210987", "%I64d", -9876543210987LL);
    require_format("+42", "%+d", 42);
    require_format(" 42", "% d", 42);
}

void check_unsigned_integers() {
    require_format("18446744073709551615", "%llu", std::numeric_limits<unsigned long long>::max());
    require_format("4294967295", "%u", 4294967295U);
    require_format("deadbeef", "%x", 0xdeadbeefU);
    require_format("DEADBEEF", "%X", 0xdeadbeefU);
    require_format("123456789012345", "%zu", static_cast<std::size_t>(123456789012345ULL));
}

void check_padding_and_alignment() {
    require_format("-0000042", "%08d", -42);
    require_format("000001a3", "%08x", 0x1a3U);
    require_format("42      !", "%-8d!", 42);
    require_format("  abc", "%5s", "abc");
    require_format("abc", "%.3s", "abcdef");
    require_format("A", "%c", 'A');
    require_format("100%", "%d%%", 100);
}

void check_double_round_trip() {
    char buffer[256]{};

    const double probe = 0.1 + 0.2;
    const int length = stbsp_snprintf(buffer, sizeof(buffer), "%.17g", probe);

    require(length > 0, "a %.17g double format must produce output");

    char* end = nullptr;
    const double parsed = std::strtod(buffer, &end);

    require(end != buffer && *end == '\0', "the formatted double must parse back fully");
    require(parsed == probe, "%.17g output must round-trip to the bit-exact double");
    require(
        std::string_view{buffer} == "0.30000000000000005",
        "this toolchain pins one double spelling"
    );
}

void check_snprintf_truncation() {
    char buffer[8]{};

    const int length = stbsp_snprintf(buffer, sizeof(buffer), "%s", "0123456789ABCDEF");

    require(length == 16, "truncated snprintf must still report the full length");
    require(buffer[sizeof(buffer) - 1] == '\0', "truncated output must stay zero-terminated");
    require(std::string_view{buffer} == "0123456", "truncation must keep the leading bytes");
}

void check_separator_globals() {
    stbsp_set_separators(' ', ',');

    char grouped[64]{};
    char decimal[64]{};

    stbsp_snprintf(grouped, sizeof(grouped), "%'d", 1234567);
    stbsp_snprintf(decimal, sizeof(decimal), "%f", 1.5);

    stbsp_set_separators(',', '.');

    require(std::string_view{grouped} == "1 234 567", "the comma global must regroup thousands");
    require(
        std::string_view{decimal} == "1,500000",
        "the period global must move the decimal point"
    );

    char regrouped[64]{};

    stbsp_snprintf(regrouped, sizeof(regrouped), "%'d", 1234567);

    require(
        std::string_view{regrouped} == "1,234,567",
        "restored separators must regroup by default"
    );
}

}

int main() {
    try {
        check_signed_integers();
        check_unsigned_integers();
        check_padding_and_alignment();
        check_double_round_trip();
        check_snprintf_truncation();
        check_separator_globals();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }

    return 0;
}
