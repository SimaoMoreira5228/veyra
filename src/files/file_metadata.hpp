#ifndef VEYRA_FILES_FILE_METADATA_HPP
#define VEYRA_FILES_FILE_METADATA_HPP

#include "veyra/files/file_id.hpp"

#include <cstdint>
#include <filesystem>
#include <system_error>

namespace veyra {

struct FileMetadata {
    FileId id;
    std::uint64_t size_bytes{};
    std::int64_t modified_nanoseconds{};
    bool is_directory{};
    std::error_code error;
};

FileMetadata read_file_metadata(const std::filesystem::path& path) noexcept;

}

#endif
