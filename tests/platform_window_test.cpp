#include "veyra/platform/null_window.hpp"

#include <array>
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

veyra::WindowEvent make_resize(float width, float height) {
    veyra::WindowEvent event{};

    event.kind = veyra::WindowEventKind::resize;
    event.data.resize = {
        .width = width,
        .height = height,
    };

    return event;
}

veyra::WindowEvent make_scale(float x, float y) {
    veyra::WindowEvent event{};

    event.kind = veyra::WindowEventKind::scale_change;
    event.data.scale = {
        .x = x,
        .y = y,
    };

    return event;
}

veyra::WindowEvent make_pointer(
    veyra::WindowEventKind kind,
    float x,
    float y,
    std::uint32_t pointer,
    std::uint32_t buttons,
    std::uint32_t modifiers
) {
    veyra::WindowEvent event{};

    event.kind = kind;
    event.data.pointer = {
        .position =
            {
                .x = x,
                .y = y,
            },
        .pointer = pointer,
        .buttons = buttons,
        .modifiers = modifiers,
    };

    return event;
}

veyra::WindowEvent make_wheel(float delta_x, float delta_y, std::uint32_t modifiers) {
    veyra::WindowEvent event{};

    event.kind = veyra::WindowEventKind::wheel;
    event.data.wheel = {
        .delta_x = delta_x,
        .delta_y = delta_y,
        .modifiers = modifiers,
    };

    return event;
}

veyra::WindowEvent
make_key(veyra::WindowEventKind kind, std::uint32_t code, std::uint32_t modifiers, bool repeat) {
    veyra::WindowEvent event{};

    event.kind = kind;
    event.data.key = {
        .code = code,
        .modifiers = modifiers,
        .repeat = repeat,
    };

    return event;
}

veyra::WindowEvent make_commit(std::string_view text) {
    veyra::WindowEvent event{};

    event.kind = veyra::WindowEventKind::text_commit;
    event.data.commit.length = static_cast<std::uint32_t>(text.size());

    for (std::size_t index = 0; index < text.size(); ++index) {
        event.data.commit.text[index] = text[index];
    }

    return event;
}

veyra::WindowEvent make_editing(std::string_view text, std::int32_t cursor) {
    veyra::WindowEvent event{};

    event.kind = veyra::WindowEventKind::text_editing;
    event.data.editing.length = static_cast<std::uint32_t>(text.size());
    event.data.editing.cursor = cursor;
    event.data.editing.selection_length = 0;

    for (std::size_t index = 0; index < text.size(); ++index) {
        event.data.editing.text[index] = text[index];
    }

    return event;
}

veyra::WindowEvent make_bare(veyra::WindowEventKind kind) {
    veyra::WindowEvent event{};

    event.kind = kind;

    return event;
}

void check_invalid_construction() {
    veyra::NullWindow null_title(nullptr, 320, 240);

    require(
        null_title.creation_result() == veyra::WindowResult::invalid_input,
        "null title is rejected"
    );
    require(null_title.window_id() == 0, "rejected window owns no id");

    veyra::NullWindow flat_title("null", 0, 240);

    require(
        flat_title.creation_result() == veyra::WindowResult::invalid_input,
        "zero width is rejected"
    );

    veyra::NullWindow tall_title("null", 320, -1);

    require(
        tall_title.creation_result() == veyra::WindowResult::invalid_input,
        "negative height is rejected"
    );

    veyra::NullWindow idle_id("null", 320, 240, 0);

    require(
        idle_id.creation_result() == veyra::WindowResult::invalid_input,
        "reserved window id zero is rejected"
    );

    veyra::WindowEvent event{};

    require(!null_title.poll_event(event), "rejected window polls nothing");

    veyra::SoftwareSurface surface{};
    std::array<veyra::SoftwarePixel, 64> backing{};

    require(
        null_title.acquire_surface(surface, backing) == veyra::WindowResult::invalid_input,
        "rejected window reports its creation result on acquire"
    );
    require(
        null_title.present() == veyra::WindowResult::invalid_state,
        "present without acquire is invalid"
    );

    std::array<char, 16> clipboard_output{};
    std::size_t clipboard_length = 0;

    require(
        null_title.get_clipboard_text(clipboard_output, clipboard_length) ==
            veyra::WindowResult::invalid_input,
        "rejected window reports its creation result on clipboard read"
    );
    require(
        null_title.set_clipboard_text("x") == veyra::WindowResult::invalid_input,
        "rejected window reports its creation result on clipboard write"
    );
}

void check_script_round_trip() {
    veyra::NullWindow window("script", 320, 240, 7);

    require(window.creation_result() == veyra::WindowResult::ready, "script window is ready");
    require(window.window_id() == 7, "script window keeps its id");

    const std::uint32_t modifiers = veyra::modifier_flag(veyra::ShortcutModifier::ctrl) |
        veyra::modifier_flag(veyra::ShortcutModifier::shift);
    const std::uint32_t primary = veyra::button_flag(veyra::PointerButton::primary);

    require(window.push_event(make_resize(400.0F, 300.0F)), "resize scripts");
    require(window.push_event(make_scale(2.0F, 2.0F)), "scale scripts");

    require(
        window.push_event(make_pointer(
            veyra::WindowEventKind::pointer_down,
            40.0F,
            30.0F,
            veyra::kMousePointerId,
            primary,
            modifiers
        )),
        "pointer down scripts"
    );
    require(
        window.push_event(make_pointer(
            veyra::WindowEventKind::pointer_move,
            44.0F,
            32.0F,
            veyra::kMousePointerId,
            primary,
            modifiers
        )),
        "pointer move scripts"
    );
    require(
        window.push_event(make_pointer(
            veyra::WindowEventKind::pointer_up,
            44.0F,
            32.0F,
            veyra::kMousePointerId,
            0,
            modifiers
        )),
        "pointer up scripts"
    );
    require(
        window.push_event(
            make_pointer(veyra::WindowEventKind::pointer_cancel, 44.0F, 32.0F, 3, 0, 0)
        ),
        "pointer cancel scripts"
    );
    require(window.push_event(make_wheel(0.0F, -3.0F, modifiers)), "wheel scripts");

    require(
        window.push_event(make_key(veyra::WindowEventKind::key_down, 11, modifiers, false)),
        "key down scripts"
    );
    require(
        window.push_event(make_key(veyra::WindowEventKind::key_down, 11, modifiers, true)),
        "key repeat scripts"
    );
    require(
        window.push_event(make_key(veyra::WindowEventKind::key_up, 11, modifiers, false)),
        "key up scripts"
    );

    require(window.push_event(make_commit("hi")), "text commit scripts");
    require(window.push_event(make_editing("h", 1)), "text editing scripts");

    require(window.push_event(make_bare(veyra::WindowEventKind::focus_lost)), "focus lost scripts");
    require(
        window.push_event(make_bare(veyra::WindowEventKind::focus_gained)),
        "focus gained scripts"
    );
    require(window.push_event(make_bare(veyra::WindowEventKind::close_request)), "close scripts");

    veyra::WindowEvent event{};

    require(window.poll_event(event), "resize polls");
    require(event.kind == veyra::WindowEventKind::resize, "resize keeps its kind");
    require(event.window == 7, "resize carries the window id");
    require(event.data.resize.width == 400.0F, "resize keeps logical width");
    require(event.data.resize.height == 300.0F, "resize keeps logical height");
    require(window.logical_width() == 400.0F, "polled resize updates logical width");
    require(window.logical_height() == 300.0F, "polled resize updates logical height");

    require(window.poll_event(event), "scale polls");
    require(event.kind == veyra::WindowEventKind::scale_change, "scale keeps its kind");
    require(event.window == 7, "scale carries the window id");
    require(event.data.scale.x == 2.0F && event.data.scale.y == 2.0F, "scale keeps both axes");
    require(window.scale_x() == 2.0F && window.scale_y() == 2.0F, "polled scale updates density");

    require(window.poll_event(event), "pointer down polls");
    require(event.data.pointer.position.x == 40.0F, "pointer down keeps logical x");
    require(event.data.pointer.position.y == 30.0F, "pointer down keeps logical y");
    require(event.data.pointer.pointer == 0, "mouse uses the reserved pointer id");
    require(
        veyra::has_button(event.data.pointer.buttons, veyra::PointerButton::primary),
        "pointer down keeps the button mask"
    );
    require(event.data.pointer.modifiers == modifiers, "pointer down keeps modifiers");

    require(window.poll_event(event), "pointer move polls");
    require(
        event.kind == veyra::WindowEventKind::pointer_move &&
            event.data.pointer.position.x == 44.0F,
        "pointer move keeps kind and position"
    );

    require(window.poll_event(event), "pointer up polls");
    require(
        event.kind == veyra::WindowEventKind::pointer_up && event.data.pointer.buttons == 0,
        "pointer up releases the button mask"
    );

    require(window.poll_event(event), "pointer cancel polls");
    require(
        event.kind == veyra::WindowEventKind::pointer_cancel && event.data.pointer.pointer == 3,
        "pointer cancel keeps the touch id"
    );

    require(window.poll_event(event), "wheel polls");
    require(
        event.kind == veyra::WindowEventKind::wheel && event.data.wheel.delta_y == -3.0F &&
            event.data.wheel.modifiers == modifiers,
        "wheel keeps delta and modifiers"
    );

    require(window.poll_event(event), "key down polls");
    require(
        event.kind == veyra::WindowEventKind::key_down && event.data.key.code == 11 &&
            !event.data.key.repeat,
        "key down keeps code without repeat"
    );

    require(window.poll_event(event), "key repeat polls");
    require(event.data.key.repeat, "repeat flag survives the round trip");

    require(window.poll_event(event), "key up polls");
    require(event.kind == veyra::WindowEventKind::key_up, "key up keeps its kind");

    require(window.poll_event(event), "text commit polls");
    require(
        event.kind == veyra::WindowEventKind::text_commit && event.data.commit.length == 2 &&
            !event.data.commit.truncated,
        "text commit keeps its bytes"
    );
    require(
        std::string_view(event.data.commit.text.data(), 2) == "hi",
        "text commit bytes stay intact"
    );

    require(window.poll_event(event), "text editing polls");
    require(
        event.kind == veyra::WindowEventKind::text_editing && event.data.editing.cursor == 1,
        "text editing keeps the cursor"
    );

    require(window.poll_event(event), "focus lost polls");
    require(event.kind == veyra::WindowEventKind::focus_lost, "focus lost keeps its kind");

    require(window.poll_event(event), "focus gained polls");
    require(event.kind == veyra::WindowEventKind::focus_gained, "focus gained keeps its kind");

    require(window.poll_event(event), "close polls");
    require(
        event.kind == veyra::WindowEventKind::close_request && event.window == 7,
        "close request keeps kind and window"
    );

    require(!window.poll_event(event), "drained queue polls nothing");
}

void check_queue_limits() {
    veyra::NullWindow window("queue", 160, 120);

    for (std::size_t index = 0; index < veyra::NullWindow::kEventCapacity; ++index) {
        require(
            window.push_event(make_bare(veyra::WindowEventKind::focus_gained)),
            "queue fills to capacity"
        );
    }

    require(
        !window.push_event(make_bare(veyra::WindowEventKind::focus_gained)),
        "push past capacity fails"
    );

    require(!window.push_event(make_resize(0.0F, 120.0F)), "zero-width resize is rejected");
    require(!window.push_event(make_resize(160.0F, -4.0F)), "negative resize is rejected");
    require(!window.push_event(make_scale(0.0F, 1.0F)), "zero scale is rejected");

    veyra::WindowEvent oversized_commit = make_commit("x");

    oversized_commit.data.commit.length =
        static_cast<std::uint32_t>(veyra::kWindowTextCommitCapacity + 1);

    require(!window.push_event(oversized_commit), "overlong commit is rejected");

    veyra::WindowEvent event{};
    std::size_t drained = 0;

    while (window.poll_event(event)) {
        ++drained;
    }

    require(drained == veyra::NullWindow::kEventCapacity, "full queue drains completely");
    require(!window.poll_event(event), "empty queue polls nothing");
}

void check_surface_lifecycle() {
    veyra::NullWindow window("surface", 64, 48);

    std::array<veyra::SoftwarePixel, 64 * 48> backing{};
    veyra::SoftwareSurface surface{};

    require(
        window.acquire_surface(surface, backing) == veyra::WindowResult::ready,
        "surface acquires into caller pixels"
    );
    require(
        surface.width == 64 && surface.height == 48 && surface.stride == 64,
        "surface matches logical size at unit scale"
    );
    require(surface.pixels.size() == 64 * 48, "surface hands the full caller span");
    require(window.surface_generation() == 1, "first acquire starts generation one");

    require(
        window.acquire_surface(surface, backing) == veyra::WindowResult::invalid_state,
        "second acquire before present is invalid"
    );
    require(
        window.present() == veyra::WindowResult::ready && window.presented_frames() == 1,
        "present flips the frame counter"
    );
    require(
        window.present() == veyra::WindowResult::invalid_state,
        "present without acquire is invalid"
    );

    require(window.push_event(make_resize(40.0F, 30.0F)), "resize scripts");
    require(window.push_event(make_scale(2.0F, 1.0F)), "scale scripts");

    veyra::WindowEvent event{};

    require(window.poll_event(event) && window.poll_event(event), "geometry events poll");

    std::array<veyra::SoftwarePixel, 80 * 30> grown{};

    require(
        window.acquire_surface(surface, grown) == veyra::WindowResult::ready,
        "resized surface acquires"
    );
    require(
        surface.width == 80 && surface.height == 30,
        "surface follows logical size times scale"
    );
    require(window.surface_generation() == 2, "geometry change bumps the generation");
    require(window.present() == veyra::WindowResult::ready, "resized surface presents");

    std::array<veyra::SoftwarePixel, 8> tiny{};

    require(
        window.acquire_surface(surface, tiny) == veyra::WindowResult::insufficient_capacity,
        "short caller span reports insufficient capacity"
    );
    require(
        window.acquire_surface(surface, grown) == veyra::WindowResult::ready,
        "failed acquire keeps no frame lock"
    );
    require(window.present() == veyra::WindowResult::ready, "surface presents after retry");

    window.set_suspended(true);

    require(
        window.acquire_surface(surface, grown) == veyra::WindowResult::suspended,
        "suspended window reports suspension"
    );

    window.set_suspended(false);

    require(
        window.acquire_surface(surface, grown) == veyra::WindowResult::ready,
        "resumed window acquires again"
    );
    require(window.present() == veyra::WindowResult::ready, "resumed surface presents");
}

void check_text_and_clipboard() {
    veyra::NullWindow window("text", 160, 120);

    require(
        window.start_text_input() == veyra::WindowResult::ready && window.text_input_active(),
        "text input starts"
    );

    const veyra::TextInputArea area{
        .x = 8,
        .y = 12,
        .width = 80,
        .height = 24,
        .cursor = 16,
    };

    require(
        window.set_text_input_area(area) == veyra::WindowResult::ready,
        "text input area is accepted"
    );

    const veyra::TextInputArea broken{
        .x = 0,
        .y = 0,
        .width = -1,
        .height = 20,
        .cursor = 0,
    };

    require(
        window.set_text_input_area(broken) == veyra::WindowResult::invalid_input,
        "negative area width is rejected"
    );
    require(window.clear_text_input_area() == veyra::WindowResult::ready, "text input area clears");
    require(
        window.stop_text_input() == veyra::WindowResult::ready && !window.text_input_active(),
        "text input stops"
    );

    require(
        window.set_clipboard_text("clipboard text") == veyra::WindowResult::ready,
        "clipboard accepts NUL-terminated text"
    );

    std::array<char, 32> output{};
    std::size_t length = 0;

    require(
        window.get_clipboard_text(output, length) == veyra::WindowResult::ready,
        "clipboard copies into caller storage"
    );
    require(
        length == 14 && std::string_view(output.data(), length) == "clipboard text",
        "clipboard text arrives without a terminator"
    );

    std::array<char, 4> short_output{'x', 'x', 'x', 'x'};
    std::size_t required = 0;

    require(
        window.get_clipboard_text(short_output, required) ==
            veyra::WindowResult::insufficient_capacity,
        "short clipboard output reports insufficient capacity"
    );
    require(required == 14, "short output reports the required length");
    require(
        short_output == std::array<char, 4>{'x', 'x', 'x', 'x'},
        "short output stays unchanged"
    );

    require(
        window.set_clipboard_text(nullptr) == veyra::WindowResult::invalid_input,
        "clipboard rejects a null input"
    );

    std::string oversized(veyra::NullWindow::kClipboardCapacity + 1, 'a');

    require(
        window.set_clipboard_text(oversized.c_str()) == veyra::WindowResult::clipboard_failed,
        "oversized clipboard text fails"
    );

    length = 0;

    require(
        window.get_clipboard_text(output, length) == veyra::WindowResult::ready &&
            std::string_view(output.data(), length) == "clipboard text",
        "failed clipboard write keeps the old text"
    );
}

}

int main() {
    try {
        check_invalid_construction();
        check_script_round_trip();
        check_queue_limits();
        check_surface_lifecycle();
        check_text_and_clipboard();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
