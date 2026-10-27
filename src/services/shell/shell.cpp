#include "veyra/services/shell/shell.hpp"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

namespace veyra {

void FakeShell::reset() noexcept {
    for (auto& script : scripts_) {
        script = {};
    }

    script_count_ = 0;
    synthetic_total_ = 0;
    latency_ms_ = 0;
    capabilities_ = {true, true, true};

    for (std::size_t index = 0; index < kOpCount; ++index) {
        failures_[index] = ShellResult::ok;
        armed_[index] = false;
        failure_counts_[index] = 0;
    }

    has_pending_ = false;
    pending_generation_ = 0;
    next_generation_ = 1;
    ready_at_ = 0;
    query_length_ = 0;

    for (auto& id : trashed_) {
        id = 0;
    }

    trashed_count_ = 0;
    last_opened_ = 0;
    last_trashed_ = 0;
}

ShellCapabilities FakeShell::capabilities() const noexcept {
    return capabilities_;
}

void FakeShell::set_capabilities(ShellCapabilities capabilities) noexcept {
    capabilities_ = capabilities;
}

bool FakeShell::script_entry(ShellScript entry) noexcept {
    if (entry.id == 0 || entry.name.empty()) {
        return false;
    }

    if (script_index(entry.id) != script_count_) {
        return false;
    }

    if (script_count_ == kScripts) {
        return false;
    }

    scripts_[script_count_] = entry;
    ++script_count_;

    return true;
}

void FakeShell::set_synthetic_total(std::size_t total) noexcept {
    synthetic_total_ = total > kSyntheticMax ? kSyntheticMax : total;
}

void FakeShell::set_latency_ms(std::uint64_t latency_ms) noexcept {
    latency_ms_ = latency_ms;
}

bool FakeShell::script_failure(ShellOp op, ShellResult result) noexcept {
    const std::size_t index = op_index(op);

    if (index == kOpCount) {
        return false;
    }

    if (result == ShellResult::ok || result == ShellResult::pending) {
        return false;
    }

    failures_[index] = result;
    armed_[index] = true;

    return true;
}

ShellEnumerateRequest FakeShell::request_enumerate(
    std::string_view directory,
    std::string_view query,
    std::uint64_t now_ms
) noexcept {
    if (directory.empty()) {
        return {ShellResult::invalid_input, 0};
    }

    if (query.size() >= kQueryCapacity) {
        return {ShellResult::invalid_input, 0};
    }

    if (!capabilities_.can_enumerate) {
        return {ShellResult::unavailable, 0};
    }

    const std::size_t op = op_index(ShellOp::enumerate);

    if (armed_[op]) {
        armed_[op] = false;
        ++failure_counts_[op];

        return {failures_[op], 0};
    }

    for (std::size_t index = 0; index < query.size(); ++index) {
        query_storage_[index] = query[index];
    }

    query_length_ = query.size();
    has_pending_ = true;
    ready_at_ = now_ms + latency_ms_;
    pending_generation_ = next_generation_;
    ++next_generation_;

    return {ShellResult::pending, pending_generation_};
}

ShellResult FakeShell::poll_enumerate(
    std::span<ShellEntry> entries,
    std::span<char> names,
    std::size_t& count,
    std::uint64_t now_ms,
    std::uint64_t generation
) noexcept {
    if (!has_pending_) {
        count = 0;

        return ShellResult::invalid_input;
    }

    if (generation != pending_generation_) {
        count = 0;

        return ShellResult::stale;
    }

    if (now_ms < ready_at_) {
        count = 0;

        return ShellResult::pending;
    }

    const ShellResult filled = fill(entries, names, count);

    if (filled == ShellResult::ok) {
        has_pending_ = false;
    }

    return filled;
}

ShellResult FakeShell::open_entry(std::uint64_t id) noexcept {
    if (id == 0) {
        return ShellResult::invalid_input;
    }

    if (!capabilities_.can_open) {
        return ShellResult::unavailable;
    }

    const std::size_t op = op_index(ShellOp::open);

    if (armed_[op]) {
        armed_[op] = false;
        ++failure_counts_[op];

        return failures_[op];
    }

    if (!known_id(id)) {
        return ShellResult::not_found;
    }

    last_opened_ = id;

    return ShellResult::ok;
}

ShellResult FakeShell::trash_entry(std::uint64_t id) noexcept {
    if (id == 0) {
        return ShellResult::invalid_input;
    }

    if (!capabilities_.can_trash) {
        return ShellResult::unavailable;
    }

    const std::size_t op = op_index(ShellOp::trash);

    if (armed_[op]) {
        armed_[op] = false;
        ++failure_counts_[op];

        return failures_[op];
    }

    const std::size_t script = script_index(id);

    if (script != script_count_) {
        for (std::size_t cursor = script + 1; cursor < script_count_; ++cursor) {
            scripts_[cursor - 1] = scripts_[cursor];
        }

        scripts_[script_count_ - 1] = {};
        --script_count_;
        last_trashed_ = id;

        return ShellResult::ok;
    }

    if (id > synthetic_total_ || trashed_id(id)) {
        return ShellResult::not_found;
    }

    if (trashed_count_ == kTrashed) {
        return ShellResult::overflow;
    }

    trashed_[trashed_count_] = id;
    ++trashed_count_;
    last_trashed_ = id;

    return ShellResult::ok;
}

bool FakeShell::has_pending() const noexcept {
    return has_pending_;
}

std::uint64_t FakeShell::pending_generation() const noexcept {
    return pending_generation_;
}

std::size_t FakeShell::entry_count() const noexcept {
    return script_count_;
}

std::size_t FakeShell::synthetic_total() const noexcept {
    return synthetic_total_;
}

std::uint64_t FakeShell::last_opened() const noexcept {
    return last_opened_;
}

std::uint64_t FakeShell::last_trashed() const noexcept {
    return last_trashed_;
}

std::size_t FakeShell::failure_count(ShellOp op) const noexcept {
    const std::size_t index = op_index(op);

    if (index == kOpCount) {
        return 0;
    }

    return failure_counts_[index];
}

std::size_t FakeShell::op_index(ShellOp op) noexcept {
    const std::size_t index = static_cast<std::size_t>(op);

    if (index >= kOpCount) {
        return kOpCount;
    }

    return index;
}

bool FakeShell::contains(std::string_view text, std::string_view query) noexcept {
    if (query.empty()) {
        return true;
    }

    if (query.size() > text.size()) {
        return false;
    }

    const std::size_t last = text.size() - query.size();

    for (std::size_t start = 0; start <= last; ++start) {
        bool match = true;

        for (std::size_t offset = 0; offset < query.size(); ++offset) {
            if (text[start + offset] != query[offset]) {
                match = false;
                break;
            }
        }

        if (match) {
            return true;
        }
    }

    return false;
}

std::size_t FakeShell::synthetic_name(
    std::size_t index,
    std::span<char> output,
    std::uint32_t& type_tag,
    bool& is_directory
) noexcept {
    if (index % 8 == 7) {
        type_tag = 0;
        is_directory = true;
        const int written = std::snprintf(output.data(), output.size(), "dir-%06zu", index);

        if (written < 0 || static_cast<std::size_t>(written) >= output.size()) {
            return 0;
        }

        return static_cast<std::size_t>(written);
    }

    constexpr std::string_view extensions[] = {".txt", ".png", ".md"};
    const std::string_view extension = extensions[index % 3];
    type_tag = static_cast<std::uint32_t>(index % 3 + 1);
    is_directory = false;
    const int written = std::snprintf(
        output.data(),
        output.size(),
        "file-%06zu%.*s",
        index,
        static_cast<int>(extension.size()),
        extension.data()
    );

    if (written < 0 || static_cast<std::size_t>(written) >= output.size()) {
        return 0;
    }

    return static_cast<std::size_t>(written);
}

bool FakeShell::known_id(std::uint64_t id) const noexcept {
    if (script_index(id) != script_count_) {
        return true;
    }

    if (id == 0 || id > synthetic_total_) {
        return false;
    }

    return !trashed_id(id);
}

bool FakeShell::trashed_id(std::uint64_t id) const noexcept {
    for (std::size_t index = 0; index < trashed_count_; ++index) {
        if (trashed_[index] == id) {
            return true;
        }
    }

    return false;
}

std::size_t FakeShell::script_index(std::uint64_t id) const noexcept {
    for (std::size_t index = 0; index < script_count_; ++index) {
        if (scripts_[index].id == id) {
            return index;
        }
    }

    return script_count_;
}

ShellResult FakeShell::fill(
    std::span<ShellEntry> entries,
    std::span<char> names,
    std::size_t& count
) const noexcept {
    const std::string_view query(query_storage_.data(), query_length_);
    std::array<char, 32> probe{};
    std::size_t matches = 0;
    std::size_t bytes = 0;

    for (std::size_t index = 0; index < script_count_; ++index) {
        if (!contains(scripts_[index].name, query)) {
            continue;
        }

        ++matches;
        bytes += scripts_[index].name.size();
    }

    for (std::size_t index = 0; index < synthetic_total_; ++index) {
        const std::uint64_t id = index + 1;

        if (trashed_id(id)) {
            continue;
        }

        std::uint32_t tag = 0;
        bool directory = false;
        const std::size_t length = synthetic_name(index, probe, tag, directory);

        if (length == 0) {
            count = 0;

            return ShellResult::overflow;
        }

        if (!contains({probe.data(), length}, query)) {
            continue;
        }

        ++matches;
        bytes += length;
    }

    if (matches > entries.size() || bytes > names.size()) {
        count = matches;

        return ShellResult::insufficient_capacity;
    }

    std::size_t placed = 0;
    std::size_t offset = 0;

    for (std::size_t index = 0; index < script_count_; ++index) {
        const ShellScript& script = scripts_[index];

        if (!contains(script.name, query)) {
            continue;
        }

        for (std::size_t cursor = 0; cursor < script.name.size(); ++cursor) {
            names[offset + cursor] = script.name[cursor];
        }

        entries[placed] = {
            .id = script.id,
            .name = {names.data() + offset, script.name.size()},
            .size_bytes = script.size_bytes,
            .modified_nanoseconds = script.modified_nanoseconds,
            .is_directory = script.is_directory,
            .is_symlink = script.is_symlink,
            .type_tag = script.type_tag,
        };
        ++placed;
        offset += script.name.size();
    }

    for (std::size_t index = 0; index < synthetic_total_; ++index) {
        const std::uint64_t id = index + 1;

        if (trashed_id(id)) {
            continue;
        }

        std::uint32_t tag = 0;
        bool directory = false;
        const std::span<char> slot = names.subspan(offset);
        const std::size_t length = synthetic_name(index, slot, tag, directory);

        if (length == 0 || !contains({slot.data(), length}, query)) {
            continue;
        }

        entries[placed] = {
            .id = id,
            .name = {slot.data(), length},
            .size_bytes = 100 + index,
            .modified_nanoseconds = 1000 + static_cast<std::int64_t>(index),
            .is_directory = directory,
            .is_symlink = false,
            .type_tag = tag,
        };
        ++placed;
        offset += length;
    }

    count = placed;

    return ShellResult::ok;
}

namespace {

std::uint32_t extension_tag(std::string_view extension) noexcept {
    if (extension.empty()) {
        return 1;
    }

    std::uint32_t hash = 2166136261U;

    for (const char byte : extension) {
        hash ^= static_cast<std::uint32_t>(static_cast<unsigned char>(byte));
        hash *= 16777619U;
    }

    return hash == 0 ? 1 : hash;
}

bool name_contains(std::string_view text, std::string_view query) noexcept {
    if (query.empty()) {
        return true;
    }

    if (query.size() > text.size()) {
        return false;
    }

    const std::size_t last = text.size() - query.size();

    for (std::size_t start = 0; start <= last; ++start) {
        bool match = true;

        for (std::size_t offset = 0; offset < query.size(); ++offset) {
            if (text[start + offset] != query[offset]) {
                match = false;
                break;
            }
        }

        if (match) {
            return true;
        }
    }

    return false;
}

struct CollectedEntry {
    std::string name;
    std::uint64_t size_bytes{};
    std::int64_t modified_nanoseconds{};
    bool is_directory{};
    bool is_symlink{};
    std::uint32_t type_tag{};
};

ShellResult collect_directory(
    const std::string& directory,
    std::string_view query,
    std::vector<CollectedEntry>& collected
) {
    std::error_code error;

    if (!std::filesystem::is_directory(directory, error) || error) {
        return ShellResult::not_found;
    }

    std::filesystem::directory_iterator cursor(directory, error);

    if (error) {
        return ShellResult::not_found;
    }

    const std::filesystem::directory_iterator done;

    for (; cursor != done; cursor.increment(error)) {
        if (error) {
            return ShellResult::unavailable;
        }

        const std::string name = cursor->path().filename().string();

        if (name.empty() || !name_contains(name, query)) {
            continue;
        }

        const auto link_state = cursor->symlink_status(error);

        if (error) {
            continue;
        }

        const auto file_state = cursor->status(error);

        if (error) {
            continue;
        }

        const bool is_link = std::filesystem::is_symlink(link_state);
        const bool is_directory = std::filesystem::is_directory(file_state);
        std::uint64_t size = 0;
        std::int64_t modified = 0;

        if (!is_directory) {
            size = cursor->file_size(error);

            if (error) {
                size = 0;
                error.clear();
            }
        }

        const auto stamped = cursor->last_write_time(error);

        if (!error) {
            modified = static_cast<std::int64_t>(stamped.time_since_epoch().count());
        } else {
            error.clear();
        }

        std::uint32_t tag = 0;

        if (!is_directory) {
            tag = extension_tag(cursor->path().extension().string());
        }

        collected.push_back({name, size, modified, is_directory, is_link, tag});
    }

    return ShellResult::ok;
}

}

ShellResult enumerate_filesystem_directory(
    std::string_view directory,
    std::string_view query,
    std::span<ShellEntry> entries,
    std::span<char> names,
    std::size_t& count
) noexcept {
    count = 0;

    if (directory.empty() || directory.size() > 4096) {
        return ShellResult::invalid_input;
    }

    try {
        const std::string owned(directory);
        auto collected = std::vector<CollectedEntry>{};
        const ShellResult scanned = collect_directory(owned, query, collected);

        if (scanned != ShellResult::ok) {
            return scanned;
        }

        std::sort(collected.begin(), collected.end(), [](const auto& left, const auto& right) {
            return left.name < right.name;
        });

        std::size_t bytes = 0;

        for (const auto& entry : collected) {
            bytes += entry.name.size();
        }

        if (collected.size() > entries.size() || bytes > names.size()) {
            count = collected.size();

            return ShellResult::insufficient_capacity;
        }

        std::size_t offset = 0;

        for (std::size_t index = 0; index < collected.size(); ++index) {
            const auto& found = collected[index];

            for (std::size_t cursor = 0; cursor < found.name.size(); ++cursor) {
                names[offset + cursor] = found.name[cursor];
            }

            entries[index] = {
                .id = index + 1,
                .name = {names.data() + offset, found.name.size()},
                .size_bytes = found.size_bytes,
                .modified_nanoseconds = found.modified_nanoseconds,
                .is_directory = found.is_directory,
                .is_symlink = found.is_symlink,
                .type_tag = found.type_tag,
            };
            offset += found.name.size();
        }

        count = collected.size();

        return ShellResult::ok;
    } catch (...) {
        count = 0;

        return ShellResult::unavailable;
    }
}

}
