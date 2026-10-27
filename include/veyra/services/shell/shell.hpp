#ifndef VEYRA_SERVICES_SHELL_HPP
#define VEYRA_SERVICES_SHELL_HPP

#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace veyra {

enum class ShellResult : std::uint8_t {
    ok,
    pending,
    invalid_input,
    not_found,
    denied,
    unavailable,
    insufficient_capacity,
    overflow,
    stale,
};

enum class ShellOp : std::uint8_t {
    enumerate,
    open,
    trash,
};

struct ShellCapabilities {
    bool can_enumerate{};
    bool can_open{};
    bool can_trash{};
};

struct ShellEntry {
    std::uint64_t id{};
    std::string_view name{};
    std::uint64_t size_bytes{};
    std::int64_t modified_nanoseconds{};
    bool is_directory{};
    bool is_symlink{};
    std::uint32_t type_tag{};
};

struct ShellScript {
    std::uint64_t id{};
    std::string_view name{};
    std::uint64_t size_bytes{};
    std::int64_t modified_nanoseconds{};
    bool is_directory{};
    bool is_symlink{};
    std::uint32_t type_tag{};
};

struct ShellEnumerateRequest {
    ShellResult result{ShellResult::pending};
    std::uint64_t generation{};
};

class FakeShell {
  public:
    static constexpr std::size_t kScripts = 64;
    static constexpr std::size_t kTrashed = 16;
    static constexpr std::size_t kQueryCapacity = 128;

    FakeShell() noexcept = default;

    FakeShell(const FakeShell&) = delete;
    FakeShell& operator=(const FakeShell&) = delete;
    FakeShell(FakeShell&&) = delete;
    FakeShell& operator=(FakeShell&&) = delete;

    void reset() noexcept;

    ShellCapabilities capabilities() const noexcept;
    void set_capabilities(ShellCapabilities capabilities) noexcept;

    bool script_entry(ShellScript entry) noexcept;
    void set_synthetic_total(std::size_t total) noexcept;
    void set_latency_ms(std::uint64_t latency_ms) noexcept;
    bool script_failure(ShellOp op, ShellResult result) noexcept;

    ShellEnumerateRequest request_enumerate(
        std::string_view directory,
        std::string_view query,
        std::uint64_t now_ms
    ) noexcept;
    ShellResult poll_enumerate(
        std::span<ShellEntry> entries,
        std::span<char> names,
        std::size_t& count,
        std::uint64_t now_ms,
        std::uint64_t generation
    ) noexcept;
    ShellResult open_entry(std::uint64_t id) noexcept;
    ShellResult trash_entry(std::uint64_t id) noexcept;

    bool has_pending() const noexcept;
    std::uint64_t pending_generation() const noexcept;
    std::size_t entry_count() const noexcept;
    std::size_t synthetic_total() const noexcept;
    std::uint64_t last_opened() const noexcept;
    std::uint64_t last_trashed() const noexcept;
    std::size_t failure_count(ShellOp op) const noexcept;

  private:
    static constexpr std::size_t kOpCount = 3;
    static constexpr std::size_t kSyntheticMax = 100000;

    static std::size_t op_index(ShellOp op) noexcept;
    static bool contains(std::string_view text, std::string_view query) noexcept;
    static std::size_t synthetic_name(
        std::size_t index,
        std::span<char> output,
        std::uint32_t& type_tag,
        bool& is_directory
    ) noexcept;

    bool known_id(std::uint64_t id) const noexcept;
    bool trashed_id(std::uint64_t id) const noexcept;
    std::size_t script_index(std::uint64_t id) const noexcept;
    ShellResult
    fill(std::span<ShellEntry> entries, std::span<char> names, std::size_t& count) const noexcept;

    std::array<ShellScript, kScripts> scripts_{};
    std::size_t script_count_{};
    std::size_t synthetic_total_{};
    std::uint64_t latency_ms_{};
    ShellCapabilities capabilities_{true, true, true};
    std::array<ShellResult, kOpCount> failures_{};
    std::array<bool, kOpCount> armed_{};
    std::array<std::size_t, kOpCount> failure_counts_{};
    bool has_pending_{};
    std::uint64_t pending_generation_{};
    std::uint64_t next_generation_{1};
    std::uint64_t ready_at_{};
    std::array<char, kQueryCapacity> query_storage_{};
    std::size_t query_length_{};
    std::array<std::uint64_t, kTrashed> trashed_{};
    std::size_t trashed_count_{};
    std::uint64_t last_opened_{};
    std::uint64_t last_trashed_{};
};

template <typename Backend>
concept ShellBackend = requires(
    Backend& backend,
    const Backend& observer,
    std::string_view directory,
    std::string_view query,
    std::span<ShellEntry> entries,
    std::span<char> names,
    std::size_t& count,
    std::uint64_t now_ms,
    std::uint64_t generation,
    std::uint64_t id
) {
    { observer.capabilities() } noexcept -> std::same_as<ShellCapabilities>;
    {
        backend.request_enumerate(directory, query, now_ms)
    } noexcept -> std::same_as<ShellEnumerateRequest>;
    {
        backend.poll_enumerate(entries, names, count, now_ms, generation)
    } noexcept -> std::same_as<ShellResult>;
    { backend.open_entry(id) } noexcept -> std::same_as<ShellResult>;
    { backend.trash_entry(id) } noexcept -> std::same_as<ShellResult>;
};

static_assert(ShellBackend<FakeShell>);

ShellResult enumerate_filesystem_directory(
    std::string_view directory,
    std::string_view query,
    std::span<ShellEntry> entries,
    std::span<char> names,
    std::size_t& count
) noexcept;

}

#endif
