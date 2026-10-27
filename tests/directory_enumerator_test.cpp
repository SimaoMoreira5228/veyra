#include "veyra/files/enumeration/directory_enumerator.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace {

void require(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

struct TemporaryDirectory {
    std::filesystem::path path;

    explicit TemporaryDirectory(std::filesystem::path directory) : path(std::move(directory)) {
    }

    TemporaryDirectory(TemporaryDirectory&& other) noexcept : path(std::move(other.path)) {
        other.path.clear();
    }

    TemporaryDirectory(const TemporaryDirectory&) = delete;
    TemporaryDirectory& operator=(const TemporaryDirectory&) = delete;

    ~TemporaryDirectory() {
        std::error_code error;
        std::filesystem::remove_all(path, error);
    }
};

std::vector<veyra::DirectoryEnumerationBatch> collect(
    veyra::DirectoryEnumerator& enumerator,
    std::uint64_t generation,
    bool expect_error = false
) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    auto batches = std::vector<veyra::DirectoryEnumerationBatch>{};
    auto batch = veyra::DirectoryEnumerationBatch{};
    auto complete = false;
    while (std::chrono::steady_clock::now() < deadline && !complete) {
        if (!enumerator.poll(batch)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }

        require(batch.generation == generation, "only the current generation is delivered");
        if (batch.event == veyra::DirectoryEnumerationEvent::error) {
            require(expect_error, batch.error.message());
            complete = true;
        }
        complete = complete || batch.event == veyra::DirectoryEnumerationEvent::complete;
        batches.push_back(batch);
    }
    require(complete, "enumeration completes before the timeout");
    return batches;
}

TemporaryDirectory make_fixture() {
    static auto sequence = std::atomic<std::uint64_t>{};
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    auto root = std::filesystem::path{};
    for (;;) {
        root = std::filesystem::temp_directory_path() /
            ("veyra-directory-enumerator-" + std::to_string(stamp) + "-" +
             std::to_string(sequence.fetch_add(1, std::memory_order_relaxed)));
        std::error_code error;
        if (std::filesystem::create_directory(root, error)) {
            break;
        }
        if (error && error != std::errc::file_exists) {
            throw std::filesystem::filesystem_error("create temporary directory", root, error);
        }
    }
    auto result = TemporaryDirectory(std::move(root));
    std::filesystem::create_directories(result.path / "many");
    std::filesystem::create_directories(result.path / "latest");
    std::filesystem::create_directories(result.path / "links" / "target-folder");
    std::filesystem::create_directory(result.path / "many" / "folder");

    for (auto index = 0; index < 260; ++index) {
        std::ofstream file(result.path / "many" / ("entry-" + std::to_string(index) + ".dat"));
        require(file.good(), "fixture entry is created");
    }

    std::ofstream latest(result.path / "latest" / "only.txt");
    latest << "veyra";
    require(latest.good(), "latest fixture entry is created");
    std::ofstream unicode(result.path / "latest" / std::filesystem::path{u8"café.txt"});
    require(unicode.good(), "UTF-8 fixture entry is created");
    std::ofstream long_name(result.path / "latest" / "very-long-file-name-with-several-parts.txt");
    require(long_name.good(), "long fixture entry is created");

#ifdef __linux__
    std::ofstream link_target(result.path / "links" / "target.txt");
    link_target << "target";
    require(link_target.good(), "symlink target is created");
    std::filesystem::create_symlink(
        result.path / "links" / "target.txt",
        result.path / "links" / "file-link.txt"
    );
    std::filesystem::create_directory_symlink(
        result.path / "links" / "target-folder",
        result.path / "links" / "folder-link"
    );
    std::filesystem::create_symlink(
        result.path / "links" / "missing.txt",
        result.path / "links" / "broken-link"
    );
#endif
    return result;
}

void check_streaming_and_supersession() {
    auto fixture = make_fixture();
    auto enumerator = veyra::DirectoryEnumerator{};

    const auto stale_generation = enumerator.start(fixture.path / "many");
    const auto latest_generation = enumerator.start(fixture.path / "latest");
    require(latest_generation > stale_generation, "each request gets a newer generation");

    const auto latest_batches = collect(enumerator, latest_generation);
    require(latest_batches.size() == 2, "a small directory produces entries and completion");
    require(
        latest_batches[0].event == veyra::DirectoryEnumerationEvent::entries,
        "entries arrive first"
    );
    const auto& latest_batch = latest_batches[0];
    require(latest_batch.entries.size() == 3, "the newest directory has three entries");
    const auto has_only =
        std::ranges::any_of(latest_batch.entries, [&latest_batch](const auto& entry) {
            return latest_batch.name(entry) == u8"only.txt" && entry.size_bytes == 5;
        });
    const auto has_unicode =
        std::ranges::any_of(latest_batch.entries, [&latest_batch](const auto& entry) {
            return latest_batch.name(entry) == u8"café.txt";
        });
    const auto has_long_name =
        std::ranges::any_of(latest_batch.entries, [&latest_batch](const auto& entry) {
            return latest_batch.name(entry) == u8"very-long-file-name-with-several-parts.txt";
        });
    require(has_only, "file names and sizes are reported");
    require(has_unicode, "non-ASCII entry names use UTF-8");
    require(has_long_name, "long entry names stay intact in shared storage");
    const auto only_entry =
        std::ranges::find_if(latest_batch.entries, [&latest_batch](const auto& entry) {
            return latest_batch.name(entry) == u8"only.txt";
        });
    require(only_entry != latest_batch.entries.end(), "the file entry is available");
    require(
        std::ranges::all_of(
            latest_batch.entries,
            [](const auto& entry) {
                return !entry.metadata_error;
            }
        ),
        "available metadata has no error"
    );
    require(
        latest_batches[1].event == veyra::DirectoryEnumerationEvent::complete,
        "completion is explicit"
    );

    const auto repeated_generation = enumerator.start(fixture.path / "latest");
    const auto repeated_batches = collect(enumerator, repeated_generation);
    const auto& repeated_batch = repeated_batches[0];
    const auto repeated_only =
        std::ranges::find_if(repeated_batch.entries, [&repeated_batch](const auto& entry) {
            return repeated_batch.name(entry) == u8"only.txt";
        });
    require(repeated_only != repeated_batch.entries.end(), "the refreshed file is available");
    if (only_entry->id.valid()) {
        require(repeated_only->id == only_entry->id, "file identity survives a directory refresh");
    } else {
        require(!repeated_only->id.valid(), "unsupported filesystem identities stay invalid");
    }

    const auto streamed_generation = enumerator.start(fixture.path / "many");
    const auto streamed_batches = collect(enumerator, streamed_generation);
    auto names = std::set<std::u8string>{};
    auto entry_batch_count = std::size_t{0};
    auto has_directory = false;
    for (const auto& batch : streamed_batches) {
        if (batch.event != veyra::DirectoryEnumerationEvent::entries) {
            continue;
        }
        ++entry_batch_count;
        require(
            batch.entries.size() <= veyra::DirectoryEnumerator::entries_per_batch,
            "entry batches stay within the configured bound"
        );
        for (const auto& entry : batch.entries) {
            names.emplace(batch.name(entry));
            has_directory =
                has_directory || (batch.name(entry) == u8"folder" && entry.is_directory);
        }
    }
    require(entry_batch_count == 5, "large directories are split into bounded batches");
    require(names.size() == 261, "streaming returns every file and directory");
    require(has_directory, "directories are included with their type");
}

void check_cancel_and_errors() {
    auto fixture = make_fixture();
    auto enumerator = veyra::DirectoryEnumerator{};

    const auto generation = enumerator.start(fixture.path / "many");
    const auto canceled_generation = enumerator.cancel();
    require(canceled_generation > generation, "cancellation advances the generation");
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    auto batch = veyra::DirectoryEnumerationBatch{};
    batch.generation = 42;
    require(!enumerator.poll(batch), "canceled generations do not publish results");
    require(batch.generation == 42, "an empty poll leaves the output unchanged");

    const auto missing_generation = enumerator.start(fixture.path / "missing");
    const auto batches = collect(enumerator, missing_generation, true);
    require(batches.size() == 1, "a missing directory produces one terminal event");
    require(
        batches[0].event == veyra::DirectoryEnumerationEvent::error,
        "filesystem errors are reported"
    );
    require(static_cast<bool>(batches[0].error), "the error event carries the filesystem error");

    const auto relative_generation = enumerator.start("relative-directory");
    const auto relative_batches = collect(enumerator, relative_generation, true);
    require(relative_batches.size() == 1, "relative paths fail before filesystem access");
    require(
        relative_batches[0].error == std::errc::invalid_argument,
        "enumeration requires an absolute path"
    );
}

void check_symbolic_links() {
#ifdef __linux__
    auto fixture = make_fixture();
    auto enumerator = veyra::DirectoryEnumerator{};
    const auto generation = enumerator.start(fixture.path / "links");
    const auto batches = collect(enumerator, generation);
    const auto& batch = batches.front();
    const auto& entries = batch.entries;

    const auto file_link = std::ranges::find_if(entries, [&batch](const auto& entry) {
        return batch.name(entry) == u8"file-link.txt";
    });
    const auto folder_link = std::ranges::find_if(entries, [&batch](const auto& entry) {
        return batch.name(entry) == u8"folder-link";
    });
    const auto broken_link = std::ranges::find_if(entries, [&batch](const auto& entry) {
        return batch.name(entry) == u8"broken-link";
    });

    require(file_link != entries.end(), "file symbolic link is listed");
    require(file_link->is_symlink && !file_link->is_directory, "file links keep their link type");
    require(file_link->id.valid(), "file links retain their target identity");
    require(folder_link != entries.end(), "directory symbolic link is listed");
    require(
        folder_link->is_symlink && folder_link->is_directory,
        "directory links retain both link and target types"
    );
    require(broken_link != entries.end(), "broken symbolic links remain visible");
    require(broken_link->is_symlink, "broken entries retain their link type");
    require(
        static_cast<bool>(broken_link->metadata_error),
        "broken links report missing target metadata"
    );
#endif
}

}

int main() {
    try {
        check_streaming_and_supersession();
        check_cancel_and_errors();
        check_symbolic_links();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
