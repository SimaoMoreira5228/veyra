#ifndef VEYRA_MEDIA_IMAGE_CODECS_HPP
#define VEYRA_MEDIA_IMAGE_CODECS_HPP

#include <cstddef>
#include <cstdint>
#include <span>

namespace veyra {

enum class ImageFormat : std::uint8_t {
    unknown,
    jpeg,
    png,
    gif,
    bmp,
    tga,
    psd,
    hdr,
    pic,
    pnm,
    svg,
    webp,
    avif,
};

enum class ImageDecodeResult : std::uint8_t {
    complete,
    invalid_input,
    unsupported_format,
    too_large,
    insufficient_frames,
    insufficient_output,
};

struct ImageDecodeInfo {
    ImageFormat format{ImageFormat::unknown};
    std::uint32_t width{};
    std::uint32_t height{};
    std::uint32_t frames{};
};

struct ImageDecodeBudget {
    std::uint32_t max_width{8192};
    std::uint32_t max_height{8192};
    std::uint32_t max_frames{64};
};

struct ImageFrameTarget {
    std::span<std::uint8_t> pixels{};
};

ImageDecodeResult probe_image(
    std::span<const std::uint8_t> bytes,
    const ImageDecodeBudget& budget,
    ImageDecodeInfo& info
) noexcept;

ImageDecodeResult decode_image(
    std::span<const std::uint8_t> bytes,
    const ImageDecodeBudget& budget,
    std::span<ImageFrameTarget> frames,
    std::span<std::uint32_t> delays_ms,
    std::uint32_t& decoded_frames
) noexcept;

}

namespace veyra::media {
using ::veyra::ImageDecodeBudget;
using ::veyra::ImageDecodeInfo;
using ::veyra::ImageDecodeResult;
using ::veyra::ImageFormat;
using ::veyra::ImageFrameTarget;
using ::veyra::decode_image;
using ::veyra::probe_image;
}

namespace veyra::media::codec {
using ::veyra::ImageDecodeBudget;
using ::veyra::ImageDecodeInfo;
using ::veyra::ImageDecodeResult;
using ::veyra::ImageFormat;
using ::veyra::ImageFrameTarget;
using ::veyra::decode_image;
using ::veyra::probe_image;
}

#endif
