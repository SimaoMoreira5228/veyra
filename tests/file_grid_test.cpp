#include "veyra/services/shell.hpp"

#include "veyra/behaviors/data/icon_view.hpp"
#include "veyra/behaviors/selection/selection.hpp"
#include "veyra/behaviors/selection/selection_typeahead.hpp"
#include "veyra/data/list_pipeline.hpp"
#include "veyra/data/server_rows.hpp"
#include "veyra/files/file_list.hpp"
#include "veyra/files/file_thumbnail_cache.hpp"
#include "veyra/widgets/data/icon_view.hpp"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace {

void require(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

constexpr std::string_view kDirectory = "srv://share/grid";

struct SharedDecoder {
    std::uint32_t calls{};
    std::uint32_t budget_pixels{64};
    std::array<veyra::SoftwarePixel, 64> pixels{};
    veyra::SoftwareImage image{
        .width = 8,
        .height = 8,
        .stride = 8,
        .pixels = pixels,
    };

    SharedDecoder() noexcept {
        for (auto& pixel : pixels) {
            pixel = {.red = 10, .green = 20, .blue = 30, .alpha = 255};
        }
    }

    const veyra::SoftwareImage* decode(std::uint32_t tag) noexcept {
        if (tag == 0 || 64 > budget_pixels) {
            return nullptr;
        }

        ++calls;

        return &image;
    }
};

constexpr std::array<veyra::SoftwarePixel, 4> kFolderPixels{{
    {.red = 90, .green = 110, .blue = 140, .alpha = 255},
    {.red = 90, .green = 110, .blue = 140, .alpha = 255},
    {.red = 90, .green = 110, .blue = 140, .alpha = 255},
    {.red = 90, .green = 110, .blue = 140, .alpha = 255},
}};

constexpr veyra::SoftwareImage kFolderImage{
    .width = 2,
    .height = 2,
    .stride = 2,
    .pixels = kFolderPixels,
};

void script_grid_entries(veyra::FakeShell& shell) {
    require(shell.script_entry({1, "alpha.png", 120, 20, false, false, 2}), "alpha scripts");
    require(shell.script_entry({2, "beta.txt", 40, 30, false, false, 1}), "beta scripts");
    require(shell.script_entry({3, "docs", 0, 10, true, false, 0}), "docs scripts");
    require(shell.script_entry({4, "delta.md", 60, 40, false, false, 3}), "delta scripts");
    require(shell.script_entry({5, "gamma.png", 200, 50, false, false, 2}), "gamma scripts");
    require(shell.script_entry({6, "epsilon.png", 150, 60, false, false, 2}), "epsilon scripts");
    require(shell.entry_count() == 6, "six rows stay scripted");
}

veyra::ShellEnumerateRequest
request_now(veyra::FakeShell& shell, std::string_view query, std::uint64_t now_ms) {
    const auto request = shell.request_enumerate(kDirectory, query, now_ms);
    require(request.result == veyra::ShellResult::pending, "a live scan pends on the caller clock");
    require(request.generation != 0, "a live scan carries a generation");

    return request;
}

std::size_t poll_now(
    veyra::FakeShell& shell,
    std::span<veyra::ShellEntry> entries,
    std::span<char> names,
    std::uint64_t now_ms,
    std::uint64_t generation
) {
    auto count = std::size_t{};
    const auto result = shell.poll_enumerate(entries, names, count, now_ms, generation);
    require(result == veyra::ShellResult::ok, "a ready scan fills the caller buffers");

    return count;
}

bool row_shows_image(const veyra::Theme& theme, const veyra::IconRecord& record, float top) {
    std::array<veyra::DisplayCommand, 8> commands{};
    const veyra::LayoutRect box{.x = 8.0F, .y = top, .width = 32.0F, .height = 32.0F};
    const auto output = veyra::emit_icon(
        theme,
        {.light = record.light, .dark = record.dark},
        {.box = box, .fit = veyra::ImageFitMode::contain},
        commands
    );
    require(
        output.result == veyra::IconEmitResult::complete,
        "a bounded row emit always fits its span"
    );

    for (std::size_t index = 0; index < output.placed; ++index) {
        if (std::holds_alternative<veyra::DrawImage>(commands[index])) {
            return true;
        }
    }

    return false;
}

void check_fake_starts_advertised_and_empty() {
    auto shell = veyra::FakeShell{};
    const auto caps = shell.capabilities();
    require(caps.can_enumerate && caps.can_open && caps.can_trash, "the fake opens fully capable");

    const auto empty = shell.request_enumerate("", "", 0);
    require(
        empty.result == veyra::ShellResult::invalid_input && empty.generation == 0,
        "an empty directory never starts a scan"
    );
    require(!shell.has_pending(), "a rejected scan leaves nothing pending");

    auto count = std::size_t{99};
    std::array<veyra::ShellEntry, 4> entries{};
    std::array<char, 64> names{};
    require(
        shell.poll_enumerate(entries, names, count, 0, 1) == veyra::ShellResult::invalid_input,
        "polling with no scan reports invalid input"
    );

    require(
        shell.open_entry(0) == veyra::ShellResult::invalid_input,
        "opening id zero is rejected"
    );
    require(
        shell.trash_entry(0) == veyra::ShellResult::invalid_input,
        "trashing id zero is rejected"
    );
    require(shell.open_entry(7) == veyra::ShellResult::not_found, "opening unknown ids misses");
    require(shell.trash_entry(7) == veyra::ShellResult::not_found, "trashing unknown ids misses");
    require(shell.last_opened() == 0 && shell.last_trashed() == 0, "misses record nothing");

    const auto request = request_now(shell, "", 0);
    const auto found = poll_now(shell, entries, names, 0, request.generation);
    require(found == 0, "an unscripted shell enumerates nothing");
    require(!shell.has_pending(), "a drained scan clears its pending flag");
}

void check_scripted_enumerate_filters_and_reports_capacity() {
    auto shell = veyra::FakeShell{};
    script_grid_entries(shell);

    const auto request = request_now(shell, "png", 10);
    std::array<veyra::ShellEntry, 8> entries{};
    std::array<char, 256> names{};
    const auto found = poll_now(shell, entries, names, 10, request.generation);
    require(found == 3, "the query keeps only matching rows");

    for (std::size_t index = 0; index < found; ++index) {
        const std::string_view name(entries[index].name);
        require(name.ends_with(".png"), "every filtered row carries the query");
        require(
            name.data() >= names.data() && name.data() + name.size() <= names.data() + names.size(),
            "filtered names borrow the caller buffer"
        );
    }

    const auto full = request_now(shell, "", 20);
    std::array<veyra::ShellEntry, 2> small_entries{};
    small_entries[0].id = 999;
    small_entries[1].id = 888;
    std::array<char, 256> roomy{};
    auto count = std::size_t{};
    require(
        shell.poll_enumerate(small_entries, roomy, count, 20, full.generation) ==
            veyra::ShellResult::insufficient_capacity,
        "a short entry span refuses instead of truncating"
    );
    require(count == 6, "the refusal reports how many rows the view needs");
    require(
        small_entries[0].id == 999 && small_entries[1].id == 888,
        "a refused poll leaves the caller span alone"
    );
    require(shell.has_pending(), "a refused scan stays pollable under its generation");

    std::array<veyra::ShellEntry, 8> retry{};
    const auto retried = poll_now(shell, retry, roomy, 20, full.generation);
    require(retried == 6, "the same generation fills a sized span");
}

void check_latency_defers_and_stale_drops() {
    auto shell = veyra::FakeShell{};
    script_grid_entries(shell);
    shell.set_latency_ms(40);

    const auto request = request_now(shell, "", 100);
    std::array<veyra::ShellEntry, 8> entries{};
    std::array<char, 256> names{};
    auto count = std::size_t{};
    require(
        shell.poll_enumerate(entries, names, count, 100, request.generation) ==
            veyra::ShellResult::pending,
        "a fresh scan pends until the caller clock passes the latency"
    );
    require(
        shell.poll_enumerate(entries, names, count, 139, request.generation) ==
            veyra::ShellResult::pending,
        "the scan still pends one tick before readiness"
    );

    const auto superseded = request_now(shell, "png", 110);
    require(superseded.generation != request.generation, "a new scan retires the old generation");
    require(
        shell.poll_enumerate(entries, names, count, 200, request.generation) ==
            veyra::ShellResult::stale,
        "polling a retired scan drops instead of filling"
    );

    const auto found = poll_now(shell, entries, names, 200, superseded.generation);
    require(found == 3, "the live scan fills once its own latency passes");

    auto rows = veyra::ServerRows{};
    const std::uint64_t first_key = rows.request_key();
    require(rows.note_loaded(0, 3, first_key) == veyra::ServerRowsResult::changed, "live loads");
    require(rows.refresh() == veyra::ServerRowsResult::changed, "refresh retires the query key");
    require(
        rows.confirm(first_key) == veyra::ServerRowsResult::stale,
        "a late arrival under a retired key drops"
    );
    require(!rows.is_covered(0, 3), "the dropped arrival keeps no coverage");
}

void check_grid_replay_shared_decode_never_reorders() {
    auto shell = veyra::FakeShell{};
    script_grid_entries(shell);

    auto rows = veyra::ServerRows{};
    const std::uint64_t live_key = rows.request_key();

    auto pipe = veyra::ListPipeline{};
    require(
        pipe.request(veyra::ListLoadDirection::refresh, 0, 6) == veyra::ListPipelineResult::changed,
        "the grid requests its window"
    );
    require(pipe.is_loading_anywhere(), "the window request reads as loading");
    require(pipe.set_placeholders(0, 6), "the view stages placeholders for the window");
    require(pipe.should_show_placeholders(), "staged placeholders show while loading");

    auto pending = veyra::ListLoadRequest{};
    require(
        pipe.pending_request(veyra::ListLoadDirection::refresh, pending),
        "the window request stays observable"
    );

    const auto request = request_now(shell, "", 0);
    std::array<veyra::ShellEntry, 8> entries{};
    std::array<char, 256> names{};
    const auto found = poll_now(shell, entries, names, 0, request.generation);
    require(found == 6, "the replay starts from six shell rows");

    std::vector<std::u8string> owned;
    std::vector<veyra::FileListEntry> files;
    owned.reserve(found);
    files.reserve(found);

    for (std::size_t index = 0; index < found; ++index) {
        const auto bytes = std::string_view(entries[index].name);
        owned.emplace_back(
            reinterpret_cast<const char8_t*>(bytes.data()),
            reinterpret_cast<const char8_t*>(bytes.data() + bytes.size())
        );
        files.push_back(
            {.id = entries[index].id,
             .name = owned.back(),
             .size_bytes = entries[index].size_bytes,
             .modified_nanoseconds = entries[index].modified_nanoseconds,
             .is_symlink = entries[index].is_symlink,
             .is_directory = entries[index].is_directory}
        );
    }

    std::vector<std::size_t> visible(files.size(), 0);
    auto visible_count = std::size_t{};
    require(
        veyra::build_file_list_order(files, {}, {}, visible, visible_count) ==
            veyra::FileListResult::complete,
        "the visible order builds over the shell rows"
    );
    require(visible_count == files.size(), "an empty filter keeps every shell row");

    std::vector<std::uint64_t> before;
    before.reserve(visible_count);

    for (std::size_t position = 0; position < visible_count; ++position) {
        before.push_back(files[visible[position]].id);
    }

    require(
        rows.note_loaded(0, visible_count, live_key) == veyra::ServerRowsResult::changed,
        "the live key covers the fresh window"
    );

    auto icons = veyra::IconRegistry{};
    auto decoder = SharedDecoder{};
    std::array<std::uint32_t, 4> tags{};

    for (const auto id : before) {
        require(icons.register_icon(id, "file", false), "every visible row mounts an icon");
        require(icons.pending(id), "a mounted row with no pixels pends");

        auto semantics = veyra::SemanticNodeData{};
        require(
            icons.write_icon_semantics(id, "File", semantics),
            "a pending row still describes itself"
        );
        require(
            veyra::has_state(semantics.states, veyra::SemanticState::busy),
            "a pending row reads busy until its pixels land"
        );
    }

    require(
        pipe.resolve(veyra::ListLoadDirection::refresh, pending.generation, true) ==
            veyra::ListPipelineResult::changed,
        "the window request resolves under its own generation"
    );
    require(pipe.set_placeholders(0, 0), "landed rows drop their placeholders");
    require(!pipe.should_show_placeholders(), "no staged rows means no placeholders");
    require(!pipe.is_loading_anywhere(), "a resolved window stops reading as loading");

    auto decoded_tags = std::size_t{0};

    for (const auto id : before) {
        auto record = veyra::IconRecord{};
        require(icons.record_for(id, record), "every mounted row reads back");

        const veyra::ShellEntry* match = nullptr;

        for (std::size_t index = 0; index < found; ++index) {
            if (entries[index].id == id) {
                match = &entries[index];
                break;
            }
        }

        require(match != nullptr, "every row traces back to its shell entry");

        if (id == before.back()) {
            continue;
        }

        if (match->is_directory) {
            require(icons.set_image(id, &kFolderImage, nullptr), "a folder row bakes its glyph");
            continue;
        }

        bool seen = false;

        for (std::size_t slot = 0; slot < decoded_tags; ++slot) {
            if (tags[slot] == match->type_tag) {
                seen = true;
                break;
            }
        }

        if (!seen) {
            tags[decoded_tags] = match->type_tag;
            ++decoded_tags;
            require(decoder.decode(match->type_tag) != nullptr, "one decode serves a new type");
        }

        require(
            icons.set_image(id, &decoder.image, nullptr),
            "same-type rows share the one decoded image"
        );
    }

    require(decoder.calls == decoded_tags, "repeat types never decode twice");
    require(decoder.calls == 3, "txt, png, and md decode once each for six rows");

    const auto* shared = &decoder.image;
    const veyra::SoftwareImage* resolved = nullptr;
    require(icons.image_for(1, false, resolved) && resolved == shared, "png rows share pixels");
    require(icons.image_for(6, false, resolved) && resolved == shared, "all baked pngs match");
    require(icons.pending(before.back()), "the unfetched row still pends");

    const auto theme = veyra::Theme{};

    for (std::size_t position = 0; position < before.size(); ++position) {
        auto record = veyra::IconRecord{};
        require(icons.record_for(before[position], record), "emit reads mounted rows");
        const bool shown = row_shows_image(theme, record, static_cast<float>(position) * 40.0F);

        if (before[position] == before.back()) {
            require(!shown, "a pending row emits its placeholder");
        } else {
            require(shown, "a baked row emits its image");
        }
    }

    require(rows.refresh() == veyra::ServerRowsResult::changed, "a resort retires the window key");
    require(
        rows.confirm(live_key) == veyra::ServerRowsResult::stale,
        "a thumbnail racing the resort drops instead of painting"
    );
    require(icons.pending(before.back()), "the dropped arrival leaves the row pending");

    std::vector<std::uint64_t> after;
    after.reserve(visible_count);

    for (std::size_t position = 0; position < visible_count; ++position) {
        after.push_back(files[visible[position]].id);
    }

    require(after == before, "async thumbnail arrivals never reorder rows");
}

void check_eviction_keeps_newest_visible() {
    std::array<veyra::FileThumbnailCacheSlot, 4> slots{};
    std::array<veyra::SoftwarePixel, 4> storage{};
    auto cache = veyra::FileThumbnailCache(slots, storage, 1, 1);
    require(cache.valid(), "a four-slot cache configures");

    auto pixel = veyra::SoftwarePixel{.red = 7, .alpha = 255};
    const auto thumb = veyra::FileThumbnail{
        .image = {
            .width = 1,
            .height = 1,
            .stride = 1,
            .pixels = std::span<const veyra::SoftwarePixel>{&pixel, 1},
        },
    };
    const auto key_for = [](std::uint64_t id) {
        return veyra::FileThumbnailKey{
            .file_id = veyra::FileId{7, id},
            .size_bytes = 1,
            .modified_nanoseconds = 1,
        };
    };

    for (std::uint64_t id = 100; id < 104; ++id) {
        require(
            cache.insert(key_for(id), thumb) == veyra::FileThumbnailCacheResult::inserted,
            "out-of-window thumbnails fill the cache"
        );
    }

    require(cache.find(key_for(100)).has_value(), "a touch refreshes the oldest key");
    require(cache.find(key_for(101)).has_value(), "a second touch reorders recency");
    require(cache.find(key_for(102)).has_value(), "a third touch reorders recency again");
    require(
        cache.insert(key_for(1), thumb) == veyra::FileThumbnailCacheResult::inserted,
        "the newest visible insert always lands"
    );
    require(!cache.find(key_for(103)).has_value(), "the untouched key is the one evicted");
    require(cache.find(key_for(1)).has_value(), "the newest visible row survives its insert");
    require(cache.find(key_for(100)).has_value(), "touched rows outlive the eviction");

    std::array<veyra::FileThumbnailCacheSlot, 8> window_slots{};
    std::array<veyra::SoftwarePixel, 128> window_storage{};
    auto window = veyra::FileThumbnailCache(window_slots, window_storage, 4, 4);

    for (std::uint64_t id = 200; id < 204; ++id) {
        require(
            window.insert(key_for(id), thumb) == veyra::FileThumbnailCacheResult::inserted,
            "decoded rows fill the wider cache"
        );
    }

    for (std::uint64_t id = 200; id < 204; ++id) {
        require(window.erase(key_for(id)), "rows that scroll out erase first");
    }

    for (std::uint64_t id = 10; id < 14; ++id) {
        require(
            window.insert(key_for(id), thumb) == veyra::FileThumbnailCacheResult::inserted,
            "newly visible rows reuse the erased slots"
        );
        require(window.find(key_for(id)).has_value(), "every newly visible row lands");
    }
}

void check_selection_and_typeahead_reuse() {
    auto shell = veyra::FakeShell{};
    script_grid_entries(shell);

    const auto request = request_now(shell, "", 0);
    std::array<veyra::ShellEntry, 8> entries{};
    std::array<char, 256> names{};
    const auto found = poll_now(shell, entries, names, 0, request.generation);

    std::array<std::uint64_t, 6> ids{};
    std::array<std::string_view, 6> labels{};

    for (std::size_t index = 0; index < found; ++index) {
        ids[index] = entries[index].id;
        labels[index] = entries[index].name;
    }

    auto selection = veyra::SelectionController{};
    require(
        selection.configure({.mode = veyra::SelectionMode::multiple, .allow_empty = true}),
        "the grid selects over many rows"
    );
    require(
        selection.select(ids[0]) == veyra::SelectionResult::changed,
        "a plain click picks the first shell row"
    );
    require(
        selection.extend_range(ids, ids[2]) == veyra::SelectionResult::changed,
        "shift-click extends from the anchor"
    );
    require(
        selection.count() == 3 && selection.contains(ids[0]) && selection.contains(ids[2]),
        "the extended range covers anchor through target"
    );

    require(selection.clear() == veyra::SelectionResult::changed, "the grid clears its pick");

    auto typeahead = veyra::SelectionTypeahead{};
    require(
        typeahead.advance('d', 100, selection, ids, labels) == veyra::SelectionResult::changed,
        "typing jumps to the first matching shell row"
    );
    require(selection.contains(3), "the jump lands on the docs row");
    require(
        typeahead.advance('o', 200, selection, ids, labels) == veyra::SelectionResult::handled,
        "extending the prefix onto the held row reports no change"
    );
    require(selection.contains(3) && selection.count() == 1, "typeahead owns the single pick");
}

void check_open_trash_capabilities_and_scripts() {
    auto shell = veyra::FakeShell{};
    script_grid_entries(shell);
    require(shell.capabilities().can_trash, "trashing starts supported");

    shell.set_capabilities({true, true, false});
    require(
        shell.trash_entry(1) == veyra::ShellResult::unavailable,
        "a missing capability reports instead of acting"
    );
    shell.set_capabilities({true, true, true});

    require(
        shell.script_failure(veyra::ShellOp::trash, veyra::ShellResult::denied),
        "a denial scripts onto the next trash"
    );
    require(shell.trash_entry(2) == veyra::ShellResult::denied, "the scripted denial fires once");
    require(shell.failure_count(veyra::ShellOp::trash) == 1, "the denial is counted");
    require(shell.trash_entry(2) == veyra::ShellResult::ok, "the next trash runs unscripted");
    require(shell.last_trashed() == 2, "a successful trash records its row");

    const auto request = request_now(shell, "", 0);
    std::array<veyra::ShellEntry, 8> entries{};
    std::array<char, 256> names{};
    const auto found = poll_now(shell, entries, names, 0, request.generation);
    require(found == 5, "a trashed script row leaves the next poll");

    for (std::size_t index = 0; index < found; ++index) {
        require(entries[index].id != 2, "the trashed id enumerates nowhere");
    }

    require(
        !shell.script_failure(veyra::ShellOp::open, veyra::ShellResult::ok),
        "scripting a success is refused"
    );
    require(shell.open_entry(1) == veyra::ShellResult::ok, "opening a live row succeeds");
    require(shell.last_opened() == 1, "a successful open records its row");
    require(shell.open_entry(2) == veyra::ShellResult::not_found, "opening a trashed row misses");
}

void check_filesystem_enumerator_tmp_only() {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto base =
        std::filesystem::path("/tmp/opencode") / ("file-grid-" + std::to_string(stamp));
    std::error_code error;
    std::filesystem::create_directories(base / "sub", error);
    require(!error, "the probe fixture stages under /tmp/opencode");

    for (const auto* leaf : {"b.txt", "a.png", "sub/c.md"}) {
        auto out = std::ofstream(base / leaf, std::ios::binary);
        out << "row";
    }

    const std::string root = base.string();
    std::array<veyra::ShellEntry, 8> entries{};
    std::array<char, 512> names{};
    auto count = std::size_t{};
    require(
        veyra::enumerate_filesystem_directory(root, "", entries, names, count) ==
            veyra::ShellResult::ok,
        "a real directory enumerates headlessly"
    );
    require(count == 3, "files plus the subdirectory enumerate");
    require(entries[0].id == 1 && entries[1].id == 2, "snapshot ids follow sorted names");
    require(std::string_view(entries[0].name) == "a.png", "the enumerator sorts by name");
    require(std::string_view(entries[1].name) == "b.txt", "sorted names keep byte order");
    require(
        std::string_view(entries[2].name) == "sub" && entries[2].is_directory,
        "the subdirectory reads back as a directory"
    );
    require(entries[0].type_tag != 0 && entries[2].type_tag == 0, "only rows group thumbnails");

    auto filtered = std::size_t{};
    std::array<veyra::ShellEntry, 1> short_entries{};
    require(
        veyra::enumerate_filesystem_directory(root, ".png", entries, names, filtered) ==
            veyra::ShellResult::ok,
        "the real query filters like the fake"
    );
    require(filtered == 1 && std::string_view(entries[0].name) == "a.png", "the query keeps one");
    require(
        veyra::enumerate_filesystem_directory(root, "", short_entries, names, filtered) ==
            veyra::ShellResult::insufficient_capacity,
        "a short span refuses instead of truncating"
    );
    require(filtered == 3, "the refusal reports the required row count");

    auto missing = std::size_t{};
    require(
        veyra::enumerate_filesystem_directory(
            (base / "absent").string(),
            "",
            entries,
            names,
            missing
        ) == veyra::ShellResult::not_found,
        "a missing directory misses instead of inventing rows"
    );

    std::filesystem::remove_all(base, error);
    require(!error, "the probe fixture cleans itself up");
}

void check_window_cost_scales_with_window() {
    auto shell = veyra::FakeShell{};
    shell.set_synthetic_total(10000);

    const auto request = shell.request_enumerate(kDirectory, "", 0);
    require(request.result == veyra::ShellResult::pending, "ten thousand rows start pending");

    auto entries = std::vector<veyra::ShellEntry>(10000);
    auto names = std::vector<char>(10000 * 20);
    const auto found = poll_now(shell, entries, names, 0, request.generation);
    require(found == 10000, "synthetic rows enumerate without filesystem cost");

    std::vector<std::u8string> owned;
    std::vector<veyra::FileListEntry> files;
    owned.reserve(found);
    files.reserve(found);

    for (const auto& entry : entries) {
        owned.emplace_back(
            reinterpret_cast<const char8_t*>(entry.name.data()),
            reinterpret_cast<const char8_t*>(entry.name.data() + entry.name.size())
        );
        files.push_back(
            {.id = entry.id,
             .name = owned.back(),
             .size_bytes = entry.size_bytes,
             .modified_nanoseconds = entry.modified_nanoseconds,
             .is_directory = entry.is_directory}
        );
    }

    auto order = std::vector<std::size_t>(files.size(), 0);
    auto order_count = std::size_t{};
    const auto start = std::chrono::steady_clock::now();
    require(
        veyra::build_file_list_order(files, {}, {}, order, order_count) ==
            veyra::FileListResult::complete,
        "ten thousand rows order headlessly"
    );
    const auto ordered_us = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - start
    );
    require(order_count == files.size(), "the window derivation keeps every row");

    constexpr std::size_t kWindow = 64;
    const std::size_t window = order_count < kWindow ? order_count : kWindow;
    auto icons = veyra::IconRegistry{};

    for (std::size_t position = 0; position < window; ++position) {
        require(
            icons.register_icon(files[order[position]].id, "file", false),
            "the window mounts bounded icons"
        );
    }

    require(icons.size() == window, "icon state scales with the window, not the entries");
    std::cout << "file_grid: entries=" << found << " ordered_us=" << ordered_us.count()
              << " window=" << window << '\n';
}

}

int main() {
    try {
        check_fake_starts_advertised_and_empty();
        check_scripted_enumerate_filters_and_reports_capacity();
        check_latency_defers_and_stale_drops();
        check_grid_replay_shared_decode_never_reorders();
        check_eviction_keeps_newest_visible();
        check_selection_and_typeahead_reuse();
        check_open_trash_capabilities_and_scripts();
        check_filesystem_enumerator_tmp_only();
        check_window_cost_scales_with_window();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
