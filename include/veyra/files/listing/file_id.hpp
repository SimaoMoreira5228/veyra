#ifndef VEYRA_FILES_FILE_ID_HPP
#define VEYRA_FILES_FILE_ID_HPP

#include <compare>
#include <cstdint>

namespace veyra {

struct FileId {
    std::uint64_t volume{};
    std::uint64_t object{};

    constexpr FileId() noexcept = default;
    constexpr FileId(std::uint64_t volume_id, std::uint64_t object_id) noexcept :
        volume(volume_id), object(object_id) {
    }

    constexpr bool valid() const noexcept {
        return object != 0;
    }

    friend constexpr auto operator<=>(const FileId&, const FileId&) = default;
};

}

namespace veyra::files {
using ::veyra::FileId;
}

namespace veyra::files::listing {
using ::veyra::FileId;
}

#endif
