#include "image_codecs_internal.hpp"

#include <webp/decode.h>
#include <webp/demux.h>

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace veyra {
namespace codec_detail {

bool webp_canvas(
    std::span<const std::uint8_t> bytes,
    std::uint32_t& width,
    std::uint32_t& height,
    std::uint32_t& frames
) noexcept {
    const auto data = WebPData{
        .bytes = bytes.data(),
        .size = bytes.size(),
    };

    WebPDemuxer* demuxer = WebPDemux(&data);

    if (demuxer == nullptr) {
        return false;
    }

    const auto count = WebPDemuxGetI(demuxer, WEBP_FF_FRAME_COUNT);
    const auto canvas_width = static_cast<int>(WebPDemuxGetI(demuxer, WEBP_FF_CANVAS_WIDTH));
    const auto canvas_height = static_cast<int>(WebPDemuxGetI(demuxer, WEBP_FF_CANVAS_HEIGHT));

    if (count == 0 || canvas_width <= 0 || canvas_height <= 0) {
        WebPDemuxDelete(demuxer);

        return false;
    }

    width = static_cast<std::uint32_t>(canvas_width);
    height = static_cast<std::uint32_t>(canvas_height);
    frames = count;
    WebPDemuxDelete(demuxer);

    return true;
}

ImageDecodeResult webp_probe(
    std::span<const std::uint8_t> bytes,
    const ImageDecodeBudget& budget,
    ImageDecodeInfo& info
) noexcept {
    auto width = std::uint32_t{};
    auto height = std::uint32_t{};
    auto frames = std::uint32_t{};

    if (!webp_canvas(bytes, width, height, frames)) {
        return ImageDecodeResult::invalid_input;
    }

    if (over_budget(budget, width, height, frames)) {
        return ImageDecodeResult::too_large;
    }

    info = ImageDecodeInfo{
        .format = ImageFormat::webp,
        .width = width,
        .height = height,
        .frames = frames,
    };

    return ImageDecodeResult::complete;
}

ImageDecodeResult webp_decode_frames(
    std::span<const std::uint8_t> bytes,
    std::uint32_t width,
    std::uint32_t height,
    std::uint32_t count,
    std::span<ImageFrameTarget> frames,
    std::span<std::uint32_t> delays_ms,
    std::uint32_t& decoded
) noexcept {
    const auto data = WebPData{
        .bytes = bytes.data(),
        .size = bytes.size(),
    };

    const auto row_bytes = static_cast<std::size_t>(width) * 4;
    const auto frame_bytes = row_bytes * static_cast<std::size_t>(height);

    if (count > frames.size()) {
        return ImageDecodeResult::insufficient_frames;
    }

    if (delays_ms.size() < count) {
        return ImageDecodeResult::insufficient_output;
    }

    for (const auto& frame : frames) {
        if (frame.pixels.size() != frame_bytes) {
            return ImageDecodeResult::insufficient_output;
        }
    }

    WebPAnimDecoderOptions options{};
    WebPAnimDecoderOptionsInit(&options);
    options.color_mode = MODE_RGBA;

    WebPAnimDecoder* decoder = WebPAnimDecoderNew(&data, &options);

    if (decoder == nullptr) {
        return ImageDecodeResult::invalid_input;
    }

    WebPAnimInfo info{};

    if (WebPAnimDecoderGetInfo(decoder, &info) == 0 || info.canvas_width <= 0 ||
        info.canvas_height <= 0 || static_cast<std::uint32_t>(info.canvas_width) != width ||
        static_cast<std::uint32_t>(info.canvas_height) != height) {
        WebPAnimDecoderDelete(decoder);

        return ImageDecodeResult::invalid_input;
    }

    auto previous_ms = 0;
    decoded = 0;

    while (WebPAnimDecoderHasMoreFrames(decoder) != 0) {
        if (decoded >= count) {
            WebPAnimDecoderDelete(decoder);

            return ImageDecodeResult::insufficient_frames;
        }

        std::uint8_t* rgba{};
        int timestamp_ms = 0;

        if (WebPAnimDecoderGetNext(decoder, &rgba, &timestamp_ms) == 0) {
            WebPAnimDecoderDelete(decoder);

            return ImageDecodeResult::invalid_input;
        }

        std::memcpy(frames[decoded].pixels.data(), rgba, frame_bytes);
        delays_ms[decoded] =
            static_cast<std::uint32_t>(timestamp_ms > previous_ms ? timestamp_ms - previous_ms : 0);
        previous_ms = timestamp_ms;
        ++decoded;
    }

    WebPAnimDecoderDelete(decoder);

    return ImageDecodeResult::complete;
}

}

}
