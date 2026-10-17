#define NANOSVG_IMPLEMENTATION
#include <nanosvg.h>

#define NANOSVGRAST_IMPLEMENTATION
#include <nanosvgrast.h>

#include "veyra/media/image_codecs.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

namespace veyra::codec_detail {

ImageDecodeResult svg_probe(
    std::span<const std::uint8_t> bytes,
    const ImageDecodeBudget& budget,
    ImageDecodeInfo& info
) noexcept {
    if (bytes.empty() || bytes.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        return ImageDecodeResult::invalid_input;
    }

    auto document = std::vector<char>(bytes.size() + 1, '\0');

    std::memcpy(document.data(), bytes.data(), bytes.size());

    NSVGimage* parsed = nsvgParse(document.data(), "px", 96.0F);

    if (parsed == nullptr || parsed->width <= 0.0F || parsed->height <= 0.0F) {
        nsvgDelete(parsed);

        return ImageDecodeResult::invalid_input;
    }

    const auto width = static_cast<std::uint32_t>(parsed->width);
    const auto height = static_cast<std::uint32_t>(parsed->height);
    nsvgDelete(parsed);

    if (width == 0 || height == 0 || width > budget.max_width || height > budget.max_height) {
        return ImageDecodeResult::too_large;
    }

    constexpr auto raster_limit = static_cast<std::uint64_t>(std::numeric_limits<int>::max()) / 4;

    if (static_cast<std::uint64_t>(width) > raster_limit ||
        static_cast<std::uint64_t>(height) > raster_limit) {
        return ImageDecodeResult::too_large;
    }

    info = ImageDecodeInfo{
        .format = ImageFormat::svg,
        .width = width,
        .height = height,
        .frames = 1,
    };

    return ImageDecodeResult::complete;
}

ImageDecodeResult svg_decode(
    std::span<const std::uint8_t> bytes,
    std::uint32_t width,
    std::uint32_t height,
    std::span<ImageFrameTarget> frames,
    std::span<std::uint32_t> delays_ms,
    std::uint32_t& decoded
) noexcept {
    decoded = 0;

    if (frames.size() < 1 || delays_ms.empty()) {
        return ImageDecodeResult::insufficient_output;
    }

    const auto frame_bytes = static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4;

    if (frames[0].pixels.size() != frame_bytes) {
        return ImageDecodeResult::insufficient_output;
    }

    auto document = std::vector<char>(bytes.size() + 1, '\0');

    std::memcpy(document.data(), bytes.data(), bytes.size());

    NSVGimage* parsed = nsvgParse(document.data(), "px", 96.0F);

    if (parsed == nullptr || parsed->width <= 0.0F || parsed->height <= 0.0F) {
        nsvgDelete(parsed);

        return ImageDecodeResult::invalid_input;
    }

    const auto scale_x = static_cast<float>(width) / parsed->width;
    const auto scale_y = static_cast<float>(height) / parsed->height;
    const auto scale = scale_x < scale_y ? scale_x : scale_y;
    const auto shift_x = (static_cast<float>(width) - parsed->width * scale) * 0.5F;
    const auto shift_y = (static_cast<float>(height) - parsed->height * scale) * 0.5F;

    NSVGrasterizer* rasterizer = nsvgCreateRasterizer();

    if (rasterizer == nullptr) {
        nsvgDelete(parsed);

        return ImageDecodeResult::invalid_input;
    }

    std::memset(frames[0].pixels.data(), 0, frame_bytes);
    nsvgRasterize(
        rasterizer,
        parsed,
        shift_x,
        shift_y,
        scale,
        frames[0].pixels.data(),
        static_cast<int>(width),
        static_cast<int>(height),
        static_cast<int>(width * 4)
    );
    nsvgDeleteRasterizer(rasterizer);
    nsvgDelete(parsed);

    delays_ms[0] = 0;
    decoded = 1;

    return ImageDecodeResult::complete;
}

}
