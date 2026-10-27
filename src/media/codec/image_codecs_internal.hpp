#ifndef VEYRA_SRC_MEDIA_IMAGE_CODECS_INTERNAL_HPP
#define VEYRA_SRC_MEDIA_IMAGE_CODECS_INTERNAL_HPP

#include "veyra/media/codec/image_codecs.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace veyra::codec_detail {
inline bool has_bytes(std::span<const std::uint8_t> bytes, std::size_t count) noexcept {
    return bytes.size() >= count;
}

inline bool starts_with(std::span<const std::uint8_t> bytes, std::string_view magic) noexcept {
    if (bytes.size() < magic.size()) {
        return false;
    }

    for (auto index = std::size_t{}; index < magic.size(); ++index) {
        if (bytes[index] != static_cast<std::uint8_t>(magic[index])) {
            return false;
        }
    }

    return true;
}

inline std::uint16_t read_u16le(std::span<const std::uint8_t> bytes, std::size_t at) noexcept {
    return static_cast<std::uint16_t>(
        static_cast<std::uint32_t>(bytes[at]) | (static_cast<std::uint32_t>(bytes[at + 1]) << 8)
    );
}

struct GifPreflight {
    std::uint32_t width{};
    std::uint32_t height{};
    std::vector<std::uint32_t> delays_ms;
    bool too_many{};
};

bool gif_walk(
    std::span<const std::uint8_t> bytes,
    std::uint32_t max_frames,
    GifPreflight& preflight
) noexcept;

ImageDecodeResult probe_gif(
    std::span<const std::uint8_t> bytes,
    const ImageDecodeBudget& budget,
    ImageDecodeInfo& info
) noexcept;

ImageDecodeResult decode_gif(
    std::span<const std::uint8_t> bytes,
    const ImageDecodeBudget& budget,
    std::span<ImageFrameTarget> frames,
    std::span<std::uint32_t> delays_ms,
    std::uint32_t& decoded
) noexcept;

ImageDecodeResult probe_still(
    std::span<const std::uint8_t> bytes,
    ImageFormat format,
    const ImageDecodeBudget& budget,
    ImageDecodeInfo& info
) noexcept;

ImageDecodeResult decode_still(
    std::span<const std::uint8_t> bytes,
    std::uint32_t width,
    std::uint32_t height,
    std::span<ImageFrameTarget> frames,
    std::span<std::uint32_t> delays_ms,
    std::uint32_t& decoded
) noexcept;

ImageDecodeResult webp_probe(
    std::span<const std::uint8_t> bytes,
    const ImageDecodeBudget& budget,
    ImageDecodeInfo& info
) noexcept;

ImageDecodeResult webp_decode_frames(
    std::span<const std::uint8_t> bytes,
    std::uint32_t width,
    std::uint32_t height,
    std::uint32_t count,
    std::span<ImageFrameTarget> frames,
    std::span<std::uint32_t> delays_ms,
    std::uint32_t& decoded
) noexcept;

ImageDecodeResult svg_probe(
    std::span<const std::uint8_t> bytes,
    const ImageDecodeBudget& budget,
    ImageDecodeInfo& info
) noexcept;

ImageDecodeResult svg_decode(
    std::span<const std::uint8_t> bytes,
    std::uint32_t width,
    std::uint32_t height,
    std::span<ImageFrameTarget> frames,
    std::span<std::uint32_t> delays_ms,
    std::uint32_t& decoded
) noexcept;

ImageDecodeResult avif_probe(
    std::span<const std::uint8_t> bytes,
    const ImageDecodeBudget& budget,
    ImageDecodeInfo& info
) noexcept;

ImageDecodeResult avif_decode_frames(
    std::span<const std::uint8_t> bytes,
    std::uint32_t width,
    std::uint32_t height,
    std::uint32_t count,
    std::span<ImageFrameTarget> frames,
    std::span<std::uint32_t> delays_ms,
    std::uint32_t& decoded
) noexcept;

bool over_budget(
    const ImageDecodeBudget& budget,
    std::uint32_t width,
    std::uint32_t height,
    std::uint32_t frames
) noexcept;

}

#endif
