#define STBI_NO_STDIO
#define STBI_ASSERT(x) ((void)0)
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include "image_codecs_internal.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <mutex>
#include <string_view>
#include <vector>

namespace veyra {
namespace {

std::mutex stb_guard{};

}

namespace codec_detail {

ImageDecodeResult decode_gif_frames(
    std::span<const std::uint8_t> bytes,
    const GifPreflight& found,
    std::span<ImageFrameTarget> frames,
    std::span<std::uint32_t> delays_ms,
    std::uint32_t& decoded
) noexcept;

bool gif_skip_blocks(std::span<const std::uint8_t> bytes, std::size_t& position) noexcept {
    while (true) {
        if (position >= bytes.size()) {
            return false;
        }

        const auto count = bytes[position++];

        if (count == 0) {
            return true;
        }

        if (position + count > bytes.size()) {
            return false;
        }

        position += count;
    }
}

bool gif_walk(
    std::span<const std::uint8_t> bytes,
    std::uint32_t max_frames,
    GifPreflight& preflight
) noexcept {
    if (!has_bytes(bytes, 13) || (!starts_with(bytes, "GIF87a") && !starts_with(bytes, "GIF89a"))) {
        return false;
    }

    preflight.width = read_u16le(bytes, 6);
    preflight.height = read_u16le(bytes, 8);

    auto position = std::size_t{13};
    const auto packed = bytes[10];

    if ((packed & 0x80U) != 0) {
        const auto entries = static_cast<std::size_t>(1) << ((packed & 0x07U) + 1);
        const auto table = entries * 3;

        if (position + table < position || position + table > bytes.size()) {
            return false;
        }

        position += table;
    }

    auto pending_ms = std::uint32_t{};

    while (true) {
        if (position >= bytes.size()) {
            return false;
        }

        const auto marker = bytes[position++];

        if (marker == 0x3B) {
            return true;
        }

        if (marker == 0x21) {
            if (position >= bytes.size()) {
                return false;
            }

            const auto label = bytes[position++];

            if (label == 0xF9) {
                if (position >= bytes.size() || bytes[position] < 4) {
                    return false;
                }

                const auto block_size = static_cast<std::size_t>(bytes[position]);

                if (position + 1 + block_size < position + 1 ||
                    position + 1 + block_size > bytes.size()) {
                    return false;
                }

                pending_ms = static_cast<std::uint32_t>(read_u16le(bytes, position + 2)) * 10U;
                position += 1 + block_size;

                if (!gif_skip_blocks(bytes, position)) {
                    return false;
                }

                continue;
            }

            if (label == 0x01) {
                if (position + 12 > bytes.size() || position + 12 < position) {
                    return false;
                }

                position += 12;
            } else if (label == 0xFF) {
                if (position >= bytes.size()) {
                    return false;
                }

                const auto app_size = bytes[position++];

                if (position + app_size < position || position + app_size > bytes.size()) {
                    return false;
                }

                position += app_size;
            }

            if (!gif_skip_blocks(bytes, position)) {
                return false;
            }

            continue;
        }

        if (marker != 0x2C) {
            return false;
        }

        if (position + 9 > bytes.size() || position + 9 < position) {
            return false;
        }

        const auto color_packed = bytes[position + 8];

        position += 9;

        if ((color_packed & 0x80U) != 0) {
            const auto entries = static_cast<std::size_t>(1) << ((color_packed & 0x07U) + 1);
            const auto table = entries * 3;

            if (position + table < position || position + table > bytes.size()) {
                return false;
            }

            position += table;
        }

        if (position >= bytes.size()) {
            return false;
        }

        ++position;

        if (!gif_skip_blocks(bytes, position)) {
            return false;
        }

        if (preflight.delays_ms.size() > max_frames) {
            preflight.too_many = true;

            return true;
        }

        preflight.delays_ms.push_back(pending_ms);
        pending_ms = 0;
    }
}

ImageDecodeResult probe_gif(
    std::span<const std::uint8_t> bytes,
    const ImageDecodeBudget& budget,
    ImageDecodeInfo& info
) noexcept {
    auto found = GifPreflight{};

    if (!gif_walk(bytes, budget.max_frames, found)) {
        return ImageDecodeResult::invalid_input;
    }

    if (found.too_many ||
        over_budget(
            budget,
            found.width,
            found.height,
            static_cast<std::uint32_t>(found.delays_ms.size())
        )) {
        return ImageDecodeResult::too_large;
    }

    info = ImageDecodeInfo{
        .format = ImageFormat::gif,
        .width = found.width,
        .height = found.height,
        .frames = static_cast<std::uint32_t>(found.delays_ms.size()),
    };

    return ImageDecodeResult::complete;
}

ImageDecodeResult decode_gif(
    std::span<const std::uint8_t> bytes,
    const ImageDecodeBudget& budget,
    std::span<ImageFrameTarget> frames,
    std::span<std::uint32_t> delays_ms,
    std::uint32_t& decoded
) noexcept {
    auto found = GifPreflight{};

    if (!gif_walk(bytes, budget.max_frames, found)) {
        return ImageDecodeResult::invalid_input;
    }

    if (found.too_many ||
        over_budget(
            budget,
            found.width,
            found.height,
            static_cast<std::uint32_t>(found.delays_ms.size())
        )) {
        return ImageDecodeResult::too_large;
    }

    return decode_gif_frames(bytes, found, frames, delays_ms, decoded);
}

ImageDecodeResult decode_gif_frames(
    std::span<const std::uint8_t> bytes,
    const GifPreflight& found,
    std::span<ImageFrameTarget> frames,
    std::span<std::uint32_t> delays_ms,
    std::uint32_t& decoded
) noexcept {
    const auto count = static_cast<std::uint32_t>(found.delays_ms.size());

    if (frames.size() < count || delays_ms.size() < count) {
        return count > frames.size() ? ImageDecodeResult::insufficient_frames
                                     : ImageDecodeResult::insufficient_output;
    }

    const auto frame_bytes =
        static_cast<std::size_t>(found.width) * static_cast<std::size_t>(found.height) * 4;

    for (auto index = std::size_t{}; index < count; ++index) {
        if (frames[index].pixels.size() != frame_bytes) {
            return ImageDecodeResult::insufficient_output;
        }
    }

    if (bytes.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        return ImageDecodeResult::invalid_input;
    }

    auto decoded_width = 0;
    auto decoded_height = 0;
    auto decoded_count = 0;
    auto decoded_comp = 0;
    int* delays = nullptr;

    std::lock_guard<std::mutex> held{stb_guard};
    std::uint8_t* pixels = stbi_load_gif_from_memory(
        bytes.data(),
        static_cast<int>(bytes.size()),
        &delays,
        &decoded_width,
        &decoded_height,
        &decoded_count,
        &decoded_comp,
        4
    );

    if (pixels == nullptr) {
        return ImageDecodeResult::invalid_input;
    }

    if (decoded_width < 0 || decoded_height < 0 || decoded_count < 0 ||
        static_cast<std::uint32_t>(decoded_width) != found.width ||
        static_cast<std::uint32_t>(decoded_height) != found.height ||
        static_cast<std::uint32_t>(decoded_count) != count) {
        stbi_image_free(pixels);

        if (delays != nullptr) {
            stbi_image_free(delays);
        }

        return ImageDecodeResult::invalid_input;
    }

    for (auto index = std::size_t{}; index < count; ++index) {
        std::memcpy(frames[index].pixels.data(), pixels + index * frame_bytes, frame_bytes);
        delays_ms[index] = found.delays_ms[index];
    }

    stbi_image_free(pixels);

    if (delays != nullptr) {
        stbi_image_free(delays);
    }

    decoded = count;

    return ImageDecodeResult::complete;
}

ImageDecodeResult probe_still(
    std::span<const std::uint8_t> bytes,
    ImageFormat format,
    const ImageDecodeBudget& budget,
    ImageDecodeInfo& info
) noexcept {
    std::lock_guard<std::mutex> held{stb_guard};
    auto width = 0;
    auto height = 0;
    auto comp = 0;

    if (stbi_info_from_memory(
            bytes.data(),
            static_cast<int>(bytes.size()),
            &width,
            &height,
            &comp
        ) == 0 ||
        width <= 0 || height <= 0) {
        return ImageDecodeResult::invalid_input;
    }

    if (over_budget(
            budget,
            static_cast<std::uint32_t>(width),
            static_cast<std::uint32_t>(height),
            1
        )) {
        return ImageDecodeResult::too_large;
    }

    info = ImageDecodeInfo{
        .format = format,
        .width = static_cast<std::uint32_t>(width),
        .height = static_cast<std::uint32_t>(height),
        .frames = 1,
    };

    return ImageDecodeResult::complete;
}

ImageDecodeResult decode_still(
    std::span<const std::uint8_t> bytes,
    std::uint32_t width,
    std::uint32_t height,
    std::span<ImageFrameTarget> frames,
    std::span<std::uint32_t> delays_ms,
    std::uint32_t& decoded
) noexcept {
    if (frames.empty()) {
        return ImageDecodeResult::insufficient_frames;
    }

    if (delays_ms.empty()) {
        return ImageDecodeResult::insufficient_output;
    }

    const auto frame_bytes = static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4;

    if (frames[0].pixels.size() != frame_bytes) {
        return ImageDecodeResult::insufficient_output;
    }

    if (bytes.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        return ImageDecodeResult::invalid_input;
    }

    auto decoded_width = 0;
    auto decoded_height = 0;
    auto decoded_comp = 0;

    std::lock_guard<std::mutex> held{stb_guard};
    std::uint8_t* pixels = stbi_load_from_memory(
        bytes.data(),
        static_cast<int>(bytes.size()),
        &decoded_width,
        &decoded_height,
        &decoded_comp,
        4
    );

    if (pixels == nullptr) {
        return ImageDecodeResult::invalid_input;
    }

    if (decoded_width < 0 || decoded_height < 0 ||
        static_cast<std::uint32_t>(decoded_width) != width ||
        static_cast<std::uint32_t>(decoded_height) != height) {
        stbi_image_free(pixels);

        return ImageDecodeResult::invalid_input;
    }

    std::memcpy(frames[0].pixels.data(), pixels, frame_bytes);
    stbi_image_free(pixels);
    delays_ms[0] = 0;
    decoded = 1;

    return ImageDecodeResult::complete;
}

}

}
