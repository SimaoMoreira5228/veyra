#include "veyra/files/file_list.hpp"
#include "veyra/files/file_thumbnail_cache.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;

struct Options {
    std::size_t entries{100'000};
    std::size_t warmup{10};
    std::size_t queries{100};
};

std::size_t parse_count(
    std::string_view value,
    std::string_view option,
    std::size_t maximum,
    bool allow_zero = false
) {
    auto count = std::size_t{};
    const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), count);
    if (error != std::errc{} || end != value.data() + value.size() || (!allow_zero && count == 0) ||
        count > maximum) {
        throw std::invalid_argument("invalid value for " + std::string(option));
    }
    return count;
}

Options parse_options(int argc, char** argv) {
    auto options = Options{};
    for (auto index = 1; index < argc; ++index) {
        const auto option = std::string_view(argv[index]);
        if (index + 1 >= argc) {
            throw std::invalid_argument("missing value for " + std::string(option));
        }

        const auto value = std::string_view(argv[++index]);
        if (option == "--entries") {
            options.entries = parse_count(value, option, 1'000'000);
        } else if (option == "--warmup") {
            options.warmup = parse_count(value, option, 1'000'000, true);
        } else if (option == "--queries") {
            options.queries = parse_count(value, option, 1'000'000);
        } else {
            throw std::invalid_argument("unknown option " + std::string(option));
        }
    }
    return options;
}

double percentile(std::span<const double> sorted_times, std::size_t percent) {
    const auto last = sorted_times.size() - 1;
    const auto index = last / 100 * percent + last % 100 * percent / 100;
    return sorted_times[index];
}

int run(const Options& options) {
    std::vector<std::u8string> names;
    names.reserve(options.entries);
    for (std::size_t index = 0; index < options.entries; ++index) {
        auto index_text = std::array<char, 32>{};
        const auto [end, error] =
            std::to_chars(index_text.data(), index_text.data() + index_text.size(), index);
        if (error != std::errc{}) {
            throw std::runtime_error("could not format benchmark entry name");
        }

        auto name = std::u8string{};
        name.reserve(9 + static_cast<std::size_t>(end - index_text.data()));
        name.append(u8"file-");
        for (auto digit = index_text.data(); digit != end; ++digit) {
            name.push_back(static_cast<char8_t>(*digit));
        }
        name.append(u8".dat");
        names.push_back(std::move(name));
    }

    std::vector<veyra::FileListEntry> entries;
    entries.reserve(options.entries);
    for (std::size_t index = 0; index < options.entries; ++index) {
        entries.push_back({
            .id = static_cast<std::uint64_t>(index + 1),
            .name = names[index],
            .size_bytes = static_cast<std::uint64_t>(index * 17),
            .modified_nanoseconds = static_cast<std::int64_t>(options.entries - index),
            .is_directory = index % 16 == 0,
        });
    }
    std::reverse(entries.begin(), entries.end());

    auto rows_by_name = entries;
    std::sort(rows_by_name.begin(), rows_by_name.end(), [](const auto& left, const auto& right) {
        return left.name < right.name;
    });
    auto refreshed_entries = std::vector<veyra::FileListEntry>{};
    refreshed_entries.reserve(options.entries);
    for (std::size_t index = 0; index < rows_by_name.size(); ++index) {
        if (index % 10 != 0 || rows_by_name.size() == 1) {
            refreshed_entries.push_back(rows_by_name[index]);
        }
    }
    const auto added_count = std::max(std::size_t{1}, options.entries / 10);
    auto added_names = std::vector<std::u8string>{};
    added_names.reserve(added_count);
    for (std::size_t index = 0; index < added_count; ++index) {
        auto index_text = std::array<char, 32>{};
        const auto [end, error] =
            std::to_chars(index_text.data(), index_text.data() + index_text.size(), index);
        if (error != std::errc{}) {
            throw std::runtime_error("could not format refreshed entry name");
        }
        auto name = std::u8string{u8"new-file-"};
        for (auto digit = index_text.data(); digit != end; ++digit) {
            name.push_back(static_cast<char8_t>(*digit));
        }
        name.append(u8".dat");
        added_names.push_back(std::move(name));
    }
    for (const auto& name : added_names) {
        refreshed_entries.push_back({
            .name = name,
        });
    }
    std::sort(
        refreshed_entries.begin(),
        refreshed_entries.end(),
        [](const auto& left, const auto& right) {
            return left.name < right.name;
        }
    );

    std::vector<std::size_t> output(options.entries);
    std::vector<double> times(options.queries);
    auto output_count = std::size_t{0};
    const auto sort = veyra::FileListSort{
        .key = veyra::FileListSortKey::modified_time,
        .direction = veyra::FileListSortDirection::descending,
    };
    const auto run_query = [&]() {
        const auto result =
            veyra::build_file_list_order(entries, u8"file-9", sort, output, output_count);
        if (result != veyra::FileListResult::complete) {
            throw std::runtime_error("file list query failed");
        }
    };

    for (std::size_t index = 0; index < options.warmup; ++index) {
        run_query();
    }

    std::uint64_t checksum = 0;
    for (auto& elapsed : times) {
        const auto begin = Clock::now();
        run_query();
        const auto end = Clock::now();
        elapsed = std::chrono::duration<double, std::micro>(end - begin).count();
        checksum += output_count + output[0];
    }
    std::sort(times.begin(), times.end());

    const auto run_sorted_order = [&]() {
        const auto result = veyra::build_file_list_order(
            rows_by_name,
            {},
            {
                .key = veyra::FileListSortKey::name,
                .directories_first = false,
                .entries_are_sorted = true,
            },
            output,
            output_count
        );
        if (result != veyra::FileListResult::complete || output_count != options.entries) {
            throw std::runtime_error("sorted file list order failed");
        }
    };

    run_sorted_order();
    for (std::size_t index = 0; index < output_count; ++index) {
        if (output[index] != index) {
            throw std::runtime_error("sorted file list fixture is not in name order");
        }
    }

    for (std::size_t index = 0; index < options.warmup; ++index) {
        run_sorted_order();
    }
    auto sorted_order_times = std::vector<double>(options.queries);
    auto sorted_order_checksum = std::uint64_t{0};
    for (auto& elapsed : sorted_order_times) {
        const auto begin = Clock::now();
        run_sorted_order();
        const auto end = Clock::now();
        elapsed = std::chrono::duration<double, std::micro>(end - begin).count();
        sorted_order_checksum += output_count + output[0];
    }
    std::sort(sorted_order_times.begin(), sorted_order_times.end());

    const auto run_full_order = [&]() {
        const auto result = veyra::build_file_list_order(entries, {}, sort, output, output_count);
        if (result != veyra::FileListResult::complete || output_count != options.entries) {
            throw std::runtime_error("unfiltered file list order failed");
        }
    };
    for (std::size_t index = 0; index < options.warmup; ++index) {
        run_full_order();
    }
    auto full_order_times = std::vector<double>(options.queries);
    auto full_order_checksum = std::uint64_t{0};
    for (auto& elapsed : full_order_times) {
        const auto begin = Clock::now();
        run_full_order();
        const auto end = Clock::now();
        elapsed = std::chrono::duration<double, std::micro>(end - begin).count();
        full_order_checksum += output_count + output[0];
    }
    std::sort(full_order_times.begin(), full_order_times.end());

    const auto all_entries_result =
        veyra::build_file_list_order(entries, {}, sort, output, output_count);
    if (all_entries_result != veyra::FileListResult::complete) {
        throw std::runtime_error("unfiltered file list query failed");
    }

    std::vector<std::uint64_t> selection_storage(options.entries);
    auto selection = veyra::FileListSelection(selection_storage);
    const auto visible_order = std::span<const std::size_t>(output.data(), output_count);
    const auto anchor_position = visible_order.size() / 2;
    const auto range_end = std::min(visible_order.size() - 1, anchor_position + 1023);
    std::vector<double> selection_times(options.queries);
    const auto anchor_result = selection.update(entries, visible_order, anchor_position);
    if (anchor_result != veyra::FileListSelectionResult::complete) {
        throw std::runtime_error("file list selection anchor failed");
    }

    const auto run_selection = [&]() {
        const auto result = selection.update(
            entries,
            visible_order,
            range_end,
            {
                .extend_range = true,
            }
        );
        if (result != veyra::FileListSelectionResult::complete) {
            throw std::runtime_error("file list range selection failed");
        }
    };
    for (std::size_t index = 0; index < options.warmup; ++index) {
        run_selection();
    }

    std::uint64_t selection_checksum = 0;
    for (auto& elapsed : selection_times) {
        const auto begin = Clock::now();
        run_selection();
        const auto end = Clock::now();
        elapsed = std::chrono::duration<double, std::micro>(end - begin).count();
        selection_checksum += selection.ids().size() + selection.ids().front();
    }
    std::sort(selection_times.begin(), selection_times.end());

    const auto run_select_visible = [&]() {
        if (selection.select_visible(entries, visible_order) !=
            veyra::FileListSelectionResult::complete) {
            throw std::runtime_error("visible file selection failed");
        }
    };
    for (std::size_t index = 0; index < options.warmup; ++index) {
        run_select_visible();
    }
    auto visible_selection_times = std::vector<double>(options.queries);
    auto visible_selection_checksum = std::uint64_t{0};
    for (auto& elapsed : visible_selection_times) {
        const auto begin = Clock::now();
        run_select_visible();
        const auto end = Clock::now();
        elapsed = std::chrono::duration<double, std::micro>(end - begin).count();
        visible_selection_checksum += selection.ids().size() + selection.ids().front();
    }
    std::sort(visible_selection_times.begin(), visible_selection_times.end());

    auto visible_by_id = std::vector<std::size_t>(options.entries);
    for (std::size_t index = 0; index < visible_by_id.size(); ++index) {
        visible_by_id[index] = visible_by_id.size() - index - 1;
    }
    const auto visible_by_id_order = std::span<const std::size_t>(visible_by_id);
    const auto run_select_visible_by_id = [&]() {
        if (selection.select_visible(entries, visible_by_id_order) !=
            veyra::FileListSelectionResult::complete) {
            throw std::runtime_error("ID-ordered file selection failed");
        }
    };
    for (std::size_t index = 0; index < options.warmup; ++index) {
        run_select_visible_by_id();
    }
    auto visible_by_id_times = std::vector<double>(options.queries);
    auto visible_by_id_checksum = std::uint64_t{0};
    for (auto& elapsed : visible_by_id_times) {
        const auto begin = Clock::now();
        run_select_visible_by_id();
        const auto end = Clock::now();
        elapsed = std::chrono::duration<double, std::micro>(end - begin).count();
        visible_by_id_checksum += selection.ids().size() + selection.ids().front();
    }
    std::sort(visible_by_id_times.begin(), visible_by_id_times.end());

    auto entries_by_id = entries;
    std::sort(entries_by_id.begin(), entries_by_id.end(), [](const auto& left, const auto& right) {
        return left.id < right.id;
    });
    entries_by_id.erase(
        std::remove_if(
            entries_by_id.begin(),
            entries_by_id.end(),
            [](const auto& entry) {
                return entry.id % 10 == 0;
            }
        ),
        entries_by_id.end()
    );
    auto refresh_selection = [&]() {
        selection.clear();
        if (selection.update(entries, visible_order, anchor_position) !=
            veyra::FileListSelectionResult::complete) {
            throw std::runtime_error("file list selection reset failed");
        }
        if (selection.update(
                entries,
                visible_order,
                range_end,
                {
                    .extend_range = true,
                }
            ) != veyra::FileListSelectionResult::complete) {
            throw std::runtime_error("file list range reset failed");
        }
    };
    refresh_selection();
    const auto run_reconciliation = [&]() {
        if (selection.reconcile(entries_by_id) != veyra::FileListSelectionResult::complete) {
            throw std::runtime_error("file list selection reconciliation failed");
        }
    };
    auto reconciliation_times = std::vector<double>(options.queries);
    for (std::size_t index = 0; index < options.warmup; ++index) {
        run_reconciliation();
        refresh_selection();
    }

    auto reconciliation_checksum = std::uint64_t{0};
    for (auto& elapsed : reconciliation_times) {
        const auto begin = Clock::now();
        run_reconciliation();
        const auto end = Clock::now();
        elapsed = std::chrono::duration<double, std::micro>(end - begin).count();
        reconciliation_checksum += selection.ids().size() + selection.ids().front();
        refresh_selection();
    }
    std::sort(reconciliation_times.begin(), reconciliation_times.end());

    auto row_id_reconciliation_times = std::vector<double>(options.queries);
    auto row_id_reconciliation_checksum = std::uint64_t{0};
    const auto run_row_id_reconciliation = [&]() {
        auto next_id = static_cast<std::uint64_t>(options.entries + 1);
        if (veyra::reconcile_file_list_row_ids(rows_by_name, refreshed_entries, next_id) !=
            veyra::FileListRowIdResult::complete) {
            throw std::runtime_error("file list row ID reconciliation failed");
        }
    };
    for (std::size_t index = 0; index < options.warmup; ++index) {
        run_row_id_reconciliation();
    }
    for (auto& elapsed : row_id_reconciliation_times) {
        const auto begin = Clock::now();
        run_row_id_reconciliation();
        const auto end = Clock::now();
        elapsed = std::chrono::duration<double, std::micro>(end - begin).count();
        for (const auto& entry : refreshed_entries) {
            row_id_reconciliation_checksum += entry.id;
        }
    }
    std::sort(row_id_reconciliation_times.begin(), row_id_reconciliation_times.end());

    auto thumbnail_slots = std::array<veyra::FileThumbnailCacheSlot, 1024>{};
    auto thumbnail_pixel_storage = std::array<veyra::SoftwarePixel, 1024>{};
    auto thumbnail_cache =
        veyra::FileThumbnailCache(thumbnail_slots, thumbnail_pixel_storage, 1, 1);
    const auto thumbnail_source = std::array{
        veyra::SoftwarePixel{.red = 37, .alpha = 255},
    };
    auto thumbnail_keys = std::array<veyra::FileThumbnailKey, 16>{};
    for (std::size_t index = 0; index < thumbnail_keys.size(); ++index) {
        auto& key = thumbnail_keys[index];
        key = {
            .file_id = veyra::FileId{1, index + 1},
            .size_bytes = 1024 + index,
            .modified_nanoseconds = static_cast<std::int64_t>(index + 1),
        };
        const auto result = thumbnail_cache.insert(
            key,
            {
                .image = {
                    .width = 1,
                    .height = 1,
                    .stride = 1,
                    .pixels = thumbnail_source,
                },
            }
        );
        if (result != veyra::FileThumbnailCacheResult::inserted) {
            throw std::runtime_error("file thumbnail cache setup failed");
        }
    }

    constexpr auto thumbnail_lookups_per_query = std::size_t{100'000};
    auto thumbnail_times = std::vector<double>(options.queries);
    const auto run_thumbnail_query = [&]() {
        auto checksum = std::uint64_t{0};
        for (std::size_t index = 0; index < thumbnail_lookups_per_query; ++index) {
            const auto& key = thumbnail_keys[index % thumbnail_keys.size()];
            const auto cached = thumbnail_cache.find(key);
            if (!cached) {
                throw std::runtime_error("file thumbnail cache lookup missed");
            }
            checksum += key.file_id.object + cached->image.pixels[0].red;
        }
        return checksum;
    };
    for (std::size_t index = 0; index < options.warmup; ++index) {
        static_cast<void>(run_thumbnail_query());
    }

    std::uint64_t thumbnail_checksum = 0;
    for (auto& elapsed : thumbnail_times) {
        const auto begin = Clock::now();
        thumbnail_checksum += run_thumbnail_query();
        const auto end = Clock::now();
        elapsed = std::chrono::duration<double, std::micro>(end - begin).count();
    }
    std::sort(thumbnail_times.begin(), thumbnail_times.end());

    std::cout << "scenario,platform,compiler,build_config,entries,warmup_queries,measured_queries,"
                 "p50_us,p95_us,p99_us,max_us,checksum\n";
    std::cout << "file_list_filter_sort," << VEYRA_BUILD_PLATFORM << ',' << VEYRA_BUILD_COMPILER
              << ',' << VEYRA_BUILD_CONFIG << ',' << options.entries << ',' << options.warmup << ','
              << options.queries << ',' << percentile(times, 50) << ',' << percentile(times, 95)
              << ',' << percentile(times, 99) << ',' << times.back() << ',' << checksum << '\n';
    std::cout << "file_list_sorted_order," << VEYRA_BUILD_PLATFORM << ',' << VEYRA_BUILD_COMPILER
              << ',' << VEYRA_BUILD_CONFIG << ',' << options.entries << ',' << options.warmup << ','
              << options.queries << ',' << percentile(sorted_order_times, 50) << ','
              << percentile(sorted_order_times, 95) << ',' << percentile(sorted_order_times, 99)
              << ',' << sorted_order_times.back() << ',' << sorted_order_checksum << '\n';
    std::cout << "file_list_full_order," << VEYRA_BUILD_PLATFORM << ',' << VEYRA_BUILD_COMPILER
              << ',' << VEYRA_BUILD_CONFIG << ',' << options.entries << ',' << options.warmup << ','
              << options.queries << ',' << percentile(full_order_times, 50) << ','
              << percentile(full_order_times, 95) << ',' << percentile(full_order_times, 99) << ','
              << full_order_times.back() << ',' << full_order_checksum << '\n';
    std::cout << "file_list_range_selection," << VEYRA_BUILD_PLATFORM << ',' << VEYRA_BUILD_COMPILER
              << ',' << VEYRA_BUILD_CONFIG << ',' << options.entries << ',' << options.warmup << ','
              << options.queries << ',' << percentile(selection_times, 50) << ','
              << percentile(selection_times, 95) << ',' << percentile(selection_times, 99) << ','
              << selection_times.back() << ',' << selection_checksum << '\n';
    std::cout << "file_list_visible_selection," << VEYRA_BUILD_PLATFORM << ','
              << VEYRA_BUILD_COMPILER << ',' << VEYRA_BUILD_CONFIG << ',' << options.entries << ','
              << options.warmup << ',' << options.queries << ','
              << percentile(visible_selection_times, 50) << ','
              << percentile(visible_selection_times, 95) << ','
              << percentile(visible_selection_times, 99) << ',' << visible_selection_times.back()
              << ',' << visible_selection_checksum << '\n';
    std::cout << "file_list_visible_selection_by_id," << VEYRA_BUILD_PLATFORM << ','
              << VEYRA_BUILD_COMPILER << ',' << VEYRA_BUILD_CONFIG << ',' << options.entries << ','
              << options.warmup << ',' << options.queries << ','
              << percentile(visible_by_id_times, 50) << ',' << percentile(visible_by_id_times, 95)
              << ',' << percentile(visible_by_id_times, 99) << ',' << visible_by_id_times.back()
              << ',' << visible_by_id_checksum << '\n';
    std::cout << "file_list_selection_reconciliation," << VEYRA_BUILD_PLATFORM << ','
              << VEYRA_BUILD_COMPILER << ',' << VEYRA_BUILD_CONFIG << ',' << options.entries << ','
              << options.warmup << ',' << options.queries << ','
              << percentile(reconciliation_times, 50) << ',' << percentile(reconciliation_times, 95)
              << ',' << percentile(reconciliation_times, 99) << ',' << reconciliation_times.back()
              << ',' << reconciliation_checksum << '\n';
    std::cout << "file_list_row_id_reconciliation," << VEYRA_BUILD_PLATFORM << ','
              << VEYRA_BUILD_COMPILER << ',' << VEYRA_BUILD_CONFIG << ',' << options.entries << ','
              << options.warmup << ',' << options.queries << ','
              << percentile(row_id_reconciliation_times, 50) << ','
              << percentile(row_id_reconciliation_times, 95) << ','
              << percentile(row_id_reconciliation_times, 99) << ','
              << row_id_reconciliation_times.back() << ',' << row_id_reconciliation_checksum
              << '\n';
    std::cout << "file_thumbnail_cache_lookup," << VEYRA_BUILD_PLATFORM << ','
              << VEYRA_BUILD_COMPILER << ',' << VEYRA_BUILD_CONFIG << ',' << options.entries << ','
              << options.warmup << ',' << options.queries << ',' << percentile(thumbnail_times, 50)
              << ',' << percentile(thumbnail_times, 95) << ',' << percentile(thumbnail_times, 99)
              << ',' << thumbnail_times.back() << ',' << thumbnail_checksum << '\n';
    return 0;
}

}

int main(int argc, char** argv) {
    try {
        const auto options = parse_options(argc, argv);
        return run(options);
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
