#include "image_codecs_internal.hpp"

#include <avif/avif.h>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace veyra {
namespace codec_detail {

ImageDecodeResult avif_probe(
    std::span<const std::uint8_t> bytes,
    const ImageDecodeBudget& budget,
    ImageDecodeInfo& info
) noexcept {
    avifDecoder* decoder = avifDecoderCreate();

    if (decoder == nullptr) {
        return ImageDecodeResult::invalid_input;
    }

    auto result = ImageDecodeResult::invalid_input;

    if (avifDecoderSetIOMemory(decoder, bytes.data(), bytes.size()) == AVIF_RESULT_OK &&
        avifDecoderParse(decoder) == AVIF_RESULT_OK && decoder->image != nullptr &&
        decoder->imageCount > 0) {
        const auto width = decoder->image->width;
        const auto height = decoder->image->height;
        const auto frames = static_cast<std::uint32_t>(decoder->imageCount);

        if (width > 0 && height > 0 && !over_budget(budget, width, height, frames)) {
            info = ImageDecodeInfo{
                .format = ImageFormat::avif,
                .width = width,
                .height = height,
                .frames = frames,
            };
            result = ImageDecodeResult::complete;
        } else {
            result = ImageDecodeResult::too_large;
        }
    }

    avifDecoderDestroy(decoder);

    return result;
}

ImageDecodeResult avif_decode_frames(
    std::span<const std::uint8_t> bytes,
    std::uint32_t width,
    std::uint32_t height,
    std::uint32_t count,
    std::span<ImageFrameTarget> frames,
    std::span<std::uint32_t> delays_ms,
    std::uint32_t& decoded
) noexcept {
    decoded = 0;

    if (count == 0 || frames.size() < count || delays_ms.size() < count) {
        return count > frames.size() ? ImageDecodeResult::insufficient_frames
                                     : ImageDecodeResult::insufficient_output;
    }

    const auto frame_bytes = static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4;

    for (auto index = std::size_t{}; index < count; ++index) {
        if (frames[index].pixels.size() != frame_bytes) {
            return ImageDecodeResult::insufficient_output;
        }
    }

    avifDecoder* decoder = avifDecoderCreate();

    if (decoder == nullptr) {
        return ImageDecodeResult::invalid_input;
    }

    auto result = ImageDecodeResult::invalid_input;

    if (avifDecoderSetIOMemory(decoder, bytes.data(), bytes.size()) == AVIF_RESULT_OK &&
        avifDecoderParse(decoder) == AVIF_RESULT_OK) {
        if (count == 1) {
            delays_ms[0] = 0;
        } else {
            auto starts_ms = std::vector<std::uint64_t>(count, 0);
            auto end_ms = std::uint64_t{};

            for (auto index = std::uint32_t{}; index < count; ++index) {
                avifImageTiming timing{};

                if (avifDecoderNthImageTiming(decoder, index, &timing) == AVIF_RESULT_OK &&
                    timing.timescale != 0) {
                    starts_ms[index] = timing.ptsInTimescales * 1000 / timing.timescale;

                    if (index + 1 == count) {
                        end_ms = (timing.ptsInTimescales + timing.durationInTimescales) * 1000 /
                            timing.timescale;
                    }
                }
            }

            for (auto index = std::uint32_t{}; index < count; ++index) {
                const auto stop_ms = index + 1 < count ? starts_ms[index + 1] : end_ms;
                delays_ms[index] = stop_ms > starts_ms[index]
                    ? static_cast<std::uint32_t>(stop_ms - starts_ms[index])
                    : 0;
            }
        }

        result = ImageDecodeResult::complete;

        for (auto index = std::uint32_t{}; index < count; ++index) {
            if (avifDecoderNextImage(decoder) != AVIF_RESULT_OK || decoder->image == nullptr) {
                result = ImageDecodeResult::invalid_input;

                break;
            }

            avifRGBImage rgb{};
            avifRGBImageSetDefaults(&rgb, decoder->image);
            rgb.format = AVIF_RGB_FORMAT_RGBA;
            rgb.depth = 8;
            rgb.pixels = frames[index].pixels.data();
            rgb.rowBytes = width * 4;

            if (avifImageYUVToRGB(decoder->image, &rgb) != AVIF_RESULT_OK) {
                result = ImageDecodeResult::invalid_input;

                break;
            }

            ++decoded;
        }
    }

    avifDecoderDestroy(decoder);

    return result;
}

}
}
