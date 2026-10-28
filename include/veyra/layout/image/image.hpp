#ifndef VEYRA_LAYOUT_IMAGE_HPP
#define VEYRA_LAYOUT_IMAGE_HPP

#include "veyra/layout/image/image_fit.hpp"

#include <cstdint>
#include <string_view>

namespace veyra {

enum class ImageLoadState : std::uint8_t {
    empty,
    loading,
    ready,
    broken,
};

enum class ImageSourceKind : std::uint8_t {
    local,
    remote,
};

enum class ImageBrokenReason : std::uint8_t {
    none,
    decode_failed,
    invalid_dimensions,
    replaced,
};

enum class ImageRequestResult : std::uint8_t {
    started,
    restarted,
    rejected,
    remote_not_allowed,
};

struct ImageRequest {
    ImageRequestResult result{ImageRequestResult::rejected};
    std::uint64_t generation{};
};

struct ImageIntrinsicUpdate {
    bool accepted{};
    bool refit{};
};

class ImageController {
  public:
    ImageController() noexcept = default;

    ImageRequest load(std::uint64_t source_id, ImageSourceKind kind, bool allow_remote) noexcept;
    ImageRequest reload() noexcept;
    void clear() noexcept;

    bool complete(std::uint64_t generation) noexcept;
    bool fail(std::uint64_t generation, ImageBrokenReason reason) noexcept;
    ImageIntrinsicUpdate
    set_intrinsic_size(std::uint64_t generation, float width, float height) noexcept;

    ImageLoadState state() const noexcept;
    std::uint64_t source_id() const noexcept;
    std::uint64_t generation() const noexcept;
    bool is_current(std::uint64_t generation) const noexcept;
    bool has_intrinsic() const noexcept;
    ImageSize intrinsic_size() const noexcept;
    ImageBrokenReason broken_reason() const noexcept;

  private:
    static std::uint64_t next_generation(std::uint64_t generation) noexcept;

    ImageLoadState state_{ImageLoadState::empty};
    ImageBrokenReason reason_{ImageBrokenReason::none};
    std::uint64_t source_{};
    std::uint64_t generation_{};
    ImageSize intrinsic_{};
    bool has_intrinsic_{};
};

}

namespace veyra::layout {
using ::veyra::ImageBrokenReason;
using ::veyra::ImageController;
using ::veyra::ImageIntrinsicUpdate;
using ::veyra::ImageLoadState;
using ::veyra::ImageRequest;
using ::veyra::ImageRequestResult;
using ::veyra::ImageSourceKind;
}

namespace veyra::layout::image {
using ::veyra::ImageBrokenReason;
using ::veyra::ImageController;
using ::veyra::ImageIntrinsicUpdate;
using ::veyra::ImageLoadState;
using ::veyra::ImageRequest;
using ::veyra::ImageRequestResult;
using ::veyra::ImageSourceKind;
}

#endif
