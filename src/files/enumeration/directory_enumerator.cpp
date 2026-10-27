#include "veyra/files/enumeration/directory_enumerator.hpp"
#include "file_metadata.hpp"

#include <limits>
#include <new>
#include <string_view>
#include <utility>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace veyra {
namespace {

std::error_code append_filename(
    std::vector<char8_t>& storage,
    const std::filesystem::path& path,
    std::size_t& offset,
    std::size_t& length
) {
    const auto& native = path.native();
#ifdef _WIN32
    const auto separator = native.find_last_of(L"\\/");
#else
    const auto separator = native.find_last_of('/');
#endif
    const auto component_offset = separator == native.npos ? 0 : separator + 1;
    offset = storage.size();

#ifdef _WIN32
    const auto component_length = native.size() - component_offset;
    if (component_length > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        return std::make_error_code(std::errc::filename_too_long);
    }

    const auto* component = native.data() + component_offset;
    const auto required = WideCharToMultiByte(
        CP_UTF8,
        WC_ERR_INVALID_CHARS,
        component,
        static_cast<int>(component_length),
        nullptr,
        0,
        nullptr,
        nullptr
    );
    if (required <= 0) {
        return {static_cast<int>(GetLastError()), std::system_category()};
    }
    const auto required_size = static_cast<std::size_t>(required);
    if (required_size > storage.max_size() - offset) {
        return std::make_error_code(std::errc::value_too_large);
    }

    storage.resize(offset + required_size);
    auto* output = reinterpret_cast<char*>(storage.data() + offset);
    const auto written = WideCharToMultiByte(
        CP_UTF8,
        WC_ERR_INVALID_CHARS,
        component,
        static_cast<int>(component_length),
        output,
        required,
        nullptr,
        nullptr
    );
    if (written != required) {
        storage.resize(offset);
        return {static_cast<int>(GetLastError()), std::system_category()};
    }
    length = required_size;
#else
    const auto component = std::string_view{native}.substr(component_offset);
    if (component.size() > storage.max_size() - offset) {
        return std::make_error_code(std::errc::value_too_large);
    }
    storage.insert(storage.end(), component.begin(), component.end());
    length = component.size();
#endif
    return {};
}

}

std::u8string_view DirectoryEnumerationBatch::name(const DirectoryEntry& entry) const noexcept {
    if (entry.name_length == 0 || entry.name_offset > name_storage.size() ||
        entry.name_length > name_storage.size() - entry.name_offset) {
        return {};
    }
    return {name_storage.data() + entry.name_offset, entry.name_length};
}

DirectoryEnumerator::DirectoryEnumerator() :
    worker_([this](const std::stop_token& token) {
        run(token);
    }) {
}

DirectoryEnumerator::~DirectoryEnumerator() {
    worker_.request_stop();
    work_available_.notify_all();
    queue_space_available_.notify_all();
}

std::uint64_t DirectoryEnumerator::advance_generation() noexcept {
    auto next = generation_.load(std::memory_order_relaxed) + 1;
    if (next == 0) {
        next = 1;
    }
    generation_.store(next, std::memory_order_release);
    return next;
}

void DirectoryEnumerator::clear_queue() noexcept {
    for (auto& batch : queue_) {
        batch.generation = 0;
        batch.event = DirectoryEnumerationEvent::entries;
        batch.entries.clear();
        batch.name_storage.clear();
        batch.error.clear();
    }
    queue_head_ = 0;
    queue_size_ = 0;
}

std::uint64_t DirectoryEnumerator::start(std::filesystem::path directory) {
    std::lock_guard lock(mutex_);
    const auto generation = advance_generation();
    pending_request_.emplace(
        Request{
            .generation = generation,
            .directory = std::move(directory),
        }
    );
    clear_queue();
    queue_space_available_.notify_all();
    work_available_.notify_one();
    return generation;
}

std::uint64_t DirectoryEnumerator::cancel() noexcept {
    std::lock_guard lock(mutex_);
    const auto generation = advance_generation();
    pending_request_.reset();
    clear_queue();
    queue_space_available_.notify_all();
    return generation;
}

bool DirectoryEnumerator::poll(DirectoryEnumerationBatch& output) {
    std::lock_guard lock(mutex_);
    if (queue_size_ == 0) {
        return false;
    }

    auto& batch = queue_[queue_head_];
    output.generation = batch.generation;
    output.event = batch.event;
    output.error = batch.error;
    output.entries.swap(batch.entries);
    output.name_storage.swap(batch.name_storage);
    batch.entries.clear();
    batch.name_storage.clear();
    batch.generation = 0;
    batch.event = DirectoryEnumerationEvent::entries;
    batch.error.clear();
    queue_head_ = (queue_head_ + 1) % queued_batch_capacity;
    --queue_size_;
    queue_space_available_.notify_one();
    return true;
}

bool DirectoryEnumerator::publish(
    DirectoryEnumerationBatch& batch,
    const std::stop_token& stop_token
) noexcept {
    std::unique_lock lock(mutex_);
    const auto has_space = queue_space_available_.wait(lock, stop_token, [this, &batch] {
        return queue_size_ < queued_batch_capacity ||
            generation_.load(std::memory_order_acquire) != batch.generation;
    });
    if (!has_space || stop_token.stop_requested() ||
        generation_.load(std::memory_order_acquire) != batch.generation) {
        return false;
    }

    const auto index = (queue_head_ + queue_size_) % queued_batch_capacity;
    auto& slot = queue_[index];
    slot.generation = batch.generation;
    slot.event = batch.event;
    slot.error = batch.error;
    slot.entries.swap(batch.entries);
    slot.name_storage.swap(batch.name_storage);
    ++queue_size_;
    return true;
}

void DirectoryEnumerator::run(const std::stop_token& stop_token) noexcept {
    while (!stop_token.stop_requested()) {
        auto request = Request{};
        {
            std::unique_lock lock(mutex_);
            const auto has_request = work_available_.wait(lock, stop_token, [this] {
                return pending_request_.has_value();
            });
            if (!has_request || stop_token.stop_requested()) {
                return;
            }
            if (!pending_request_) {
                continue;
            }
            request = std::move(*pending_request_);
            pending_request_.reset();

            auto best_buffer_index = std::size_t{0};
            for (std::size_t index = 1; index < queued_batch_capacity; ++index) {
                if (queue_[index].entries.capacity() >
                    queue_[best_buffer_index].entries.capacity()) {
                    best_buffer_index = index;
                }
            }
            if (queue_[best_buffer_index].entries.capacity() > working_batch_.entries.capacity()) {
                working_batch_.entries.swap(queue_[best_buffer_index].entries);
                working_batch_.name_storage.swap(queue_[best_buffer_index].name_storage);
            }
        }

        enumerate(request, stop_token);
    }
}

void DirectoryEnumerator::enumerate(
    const Request& request,
    const std::stop_token& stop_token
) noexcept {
    auto& batch = working_batch_;
    batch.generation = request.generation;
    batch.event = DirectoryEnumerationEvent::entries;
    batch.entries.clear();
    batch.name_storage.clear();
    batch.error.clear();
    try {
        if (!request.directory.is_absolute()) {
            batch.event = DirectoryEnumerationEvent::error;
            batch.error = std::make_error_code(std::errc::invalid_argument);
            publish(batch, stop_token);
            return;
        }

        std::error_code error;
        auto iterator = std::filesystem::directory_iterator(request.directory, error);
        if (error) {
            batch.event = DirectoryEnumerationEvent::error;
            batch.error = error;
            publish(batch, stop_token);
            return;
        }

        const auto end = std::filesystem::directory_iterator{};
        batch.entries.reserve(entries_per_batch);
        batch.name_storage.reserve(entries_per_batch * 32);
        while (iterator != end) {
            if (stop_token.stop_requested() ||
                generation_.load(std::memory_order_acquire) != request.generation) {
                return;
            }

            const auto& item = *iterator;
            const auto metadata = read_file_metadata(item.path());
            auto symlink_error = std::error_code{};
            const auto is_symlink = item.is_symlink(symlink_error);
            auto name_offset = std::size_t{0};
            auto name_length = std::size_t{0};
            const auto name_error =
                append_filename(batch.name_storage, item.path(), name_offset, name_length);
            if (name_error) {
                batch.event = DirectoryEnumerationEvent::error;
                batch.entries.clear();
                batch.name_storage.clear();
                batch.error = name_error;
                publish(batch, stop_token);
                return;
            }
            auto entry = DirectoryEntry{
                .name_offset = name_offset,
                .name_length = name_length,
                .id = metadata.id,
                .size_bytes = metadata.size_bytes,
                .modified_nanoseconds = metadata.modified_nanoseconds,
                .is_symlink = is_symlink,
                .is_directory = metadata.is_directory,
                .metadata_error = metadata.error ? metadata.error : symlink_error,
            };
            batch.entries.push_back(entry);

            if (batch.entries.size() == entries_per_batch) {
                batch.event = DirectoryEnumerationEvent::entries;
                if (!publish(batch, stop_token)) {
                    return;
                }
                batch.entries.clear();
                batch.name_storage.clear();
            }

            if (stop_token.stop_requested() ||
                generation_.load(std::memory_order_acquire) != request.generation) {
                return;
            }

            iterator.increment(error);
            if (error) {
                if (!batch.entries.empty()) {
                    batch.event = DirectoryEnumerationEvent::entries;
                    if (!publish(batch, stop_token)) {
                        return;
                    }
                }
                batch.event = DirectoryEnumerationEvent::error;
                batch.error = error;
                batch.entries.clear();
                batch.name_storage.clear();
                publish(batch, stop_token);
                return;
            }
        }

        if (!batch.entries.empty()) {
            batch.event = DirectoryEnumerationEvent::entries;
            if (!publish(batch, stop_token)) {
                return;
            }
        }

        batch.event = DirectoryEnumerationEvent::complete;
        batch.error.clear();
        batch.entries.clear();
        publish(batch, stop_token);
    } catch (const std::filesystem::filesystem_error& error) {
        batch.event = DirectoryEnumerationEvent::error;
        batch.entries.clear();
        batch.name_storage.clear();
        batch.error = error.code();
        publish(batch, stop_token);
    } catch (const std::bad_alloc&) {
        batch.event = DirectoryEnumerationEvent::error;
        batch.entries.clear();
        batch.name_storage.clear();
        batch.error = std::make_error_code(std::errc::not_enough_memory);
        publish(batch, stop_token);
    } catch (...) {
        batch.event = DirectoryEnumerationEvent::error;
        batch.entries.clear();
        batch.name_storage.clear();
        batch.error = std::make_error_code(std::errc::io_error);
        publish(batch, stop_token);
    }
}

}
