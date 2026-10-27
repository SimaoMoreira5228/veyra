#include "veyra/platform/headless/fake_services.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

void require(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}
std::string read_text(veyra::FakeServices& services) {
    std::array<char, veyra::FakeServices::kClipboardCapacity> output{};

    std::size_t length = 0;

    require(
        services.clipboard_get(output, length) == veyra::ServiceResult::ok,
        "clipboard read succeeds"
    );

    return std::string(output.data(), length);
}

void check_clipboard_round_trip() {
    veyra::FakeServices services{};

    require(services.clipboard_generation() == 0, "fresh clipboard starts at generation zero");
    require(services.clipboard_length() == 0, "fresh clipboard holds nothing");

    std::array<char, 8> empty_output{};

    std::size_t empty_length = 99;

    require(
        services.clipboard_get(empty_output, empty_length) == veyra::ServiceResult::ok,
        "empty clipboard reads clean"
    );
    require(empty_length == 0, "empty clipboard reports zero length");

    require(
        services.clipboard_set("clipboard text") == veyra::ServiceResult::ok,
        "clipboard accepts NUL-terminated text"
    );
    require(services.clipboard_generation() == 1, "successful write bumps the generation");

    require(read_text(services) == "clipboard text", "clipboard text arrives without a terminator");

    std::array<char, 4> short_output{'x', 'x', 'x', 'x'};

    std::size_t required = 0;

    require(
        services.clipboard_get(short_output, required) ==
            veyra::ServiceResult::insufficient_capacity,
        "short output reports insufficient capacity"
    );
    require(required == 14, "short output reports the required length");
    require(
        short_output == std::array<char, 4>{'x', 'x', 'x', 'x'},
        "short output stays unchanged"
    );

    require(
        services.clipboard_set(nullptr) == veyra::ServiceResult::invalid_input,
        "clipboard rejects a null input"
    );

    std::string oversized(veyra::FakeServices::kClipboardCapacity + 1, 'a');

    require(
        services.clipboard_set(oversized.c_str()) == veyra::ServiceResult::rejected,
        "oversized clipboard text is rejected"
    );
    require(read_text(services) == "clipboard text", "rejected write keeps the old text");
    require(services.clipboard_generation() == 1, "rejected write keeps the generation");

    require(services.clipboard_set("") == veyra::ServiceResult::ok, "empty write is still a write");
    require(read_text(services).empty(), "empty write clears the text");
    require(services.clipboard_generation() == 2, "empty write bumps the generation");
}

void check_clipboard_access_denied() {
    veyra::FakeServices services{};

    require(
        services.clipboard_set("before lock") == veyra::ServiceResult::ok,
        "clipboard writes before the lock"
    );

    services.set_clipboard_access(false);

    require(!services.clipboard_access_allowed(), "access flag reads back");

    std::array<char, 32> output{};

    std::size_t length = 0;

    require(
        services.clipboard_set("locked out") == veyra::ServiceResult::denied,
        "write while denied fails"
    );
    require(
        services.clipboard_get(output, length) == veyra::ServiceResult::denied,
        "read while denied fails"
    );
    require(length == 0, "denied read reports no length");
    require(services.clipboard_generation() == 1, "denied write keeps the generation");

    services.set_clipboard_access(true);

    require(read_text(services) == "before lock", "denied-then-granted flip keeps stored text");
    require(
        services.clipboard_set("after unlock") == veyra::ServiceResult::ok,
        "write works again after the flip"
    );
    require(read_text(services) == "after unlock", "post-flip text reads back");
}

void check_clipboard_persistence() {
    veyra::FakeServices services{};

    require(
        services.clipboard_set("sticky") == veyra::ServiceResult::ok,
        "clipboard writes while persistent"
    );

    services.set_clipboard_persistent(false);

    require(!services.clipboard_persistent(), "persistence flag reads back");
    require(
        services.clipboard_set("gone") == veyra::ServiceResult::unavailable,
        "write without persistence fails"
    );
    require(read_text(services) == "sticky", "unavailable write keeps the old text");
    require(services.clipboard_generation() == 1, "unavailable write keeps the generation");

    require(
        services.simulate_clipboard_external_write("outsider") == veyra::ServiceResult::unavailable,
        "external write without persistence fails too"
    );
    require(read_text(services) == "sticky", "failed external write keeps the old text");

    services.set_clipboard_persistent(true);

    require(
        services.clipboard_set("backed") == veyra::ServiceResult::ok,
        "write works once persistence returns"
    );
    require(read_text(services) == "backed", "restored text reads back");
}

void check_clipboard_generation_fencing() {
    veyra::FakeServices services{};

    require(
        services.clipboard_set("first") == veyra::ServiceResult::ok,
        "clipboard takes the first write"
    );

    const std::uint64_t observed = services.clipboard_generation();

    require(
        services.simulate_clipboard_external_write("second") == veyra::ServiceResult::ok,
        "external write lands"
    );
    require(services.clipboard_generation() == observed + 1, "external write bumps the generation");
    require(
        observed != services.clipboard_generation(),
        "reader detects the mutation through the fence"
    );
    require(read_text(services) == "second", "external text reads back");

    const std::uint64_t settled = services.clipboard_generation();

    require(read_text(services) == "second", "quiet re-read matches");
    require(
        settled == services.clipboard_generation(),
        "generation holds still when nobody writes"
    );

    require(
        services.simulate_clipboard_external_write(nullptr) == veyra::ServiceResult::invalid_input,
        "null external write is rejected"
    );

    std::string oversized(veyra::FakeServices::kClipboardCapacity + 1, 'b');

    require(
        services.simulate_clipboard_external_write(oversized.c_str()) ==
            veyra::ServiceResult::rejected,
        "oversized external write is rejected"
    );
    require(read_text(services) == "second", "rejected external write keeps the text");
}

void check_reset_clears_clipboard() {
    veyra::FakeServices services{};

    require(services.clipboard_set("data") == veyra::ServiceResult::ok, "clipboard fills");

    services.reset();

    require(services.clipboard_generation() == 0, "reset clears the clipboard generation");
    require(services.clipboard_length() == 0, "reset clears the clipboard text");
    require(services.clipboard_access_allowed(), "reset restores clipboard access");
    require(services.clipboard_persistent(), "reset restores persistence");
}

}

int main() {
    try {
        check_clipboard_round_trip();
        check_clipboard_access_denied();
        check_clipboard_persistence();
        check_clipboard_generation_fencing();
        check_reset_clears_clipboard();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
