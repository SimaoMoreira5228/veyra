#include "veyra/media/image_codecs.hpp"

#include <avif/avif.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace {

void require(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

void require_ok(avifResult result, std::string_view message) {
    if (result != AVIF_RESULT_OK) {
        throw std::runtime_error(std::string(message));
    }
}

avifImage*
make_image(std::uint32_t width, std::uint32_t height, std::span<const std::uint8_t> rgba) {
    avifImage* image = avifImageCreate(width, height, 8, AVIF_PIXEL_FORMAT_YUV444);
    require(image != nullptr, "the image allocates");
    image->matrixCoefficients = AVIF_MATRIX_COEFFICIENTS_IDENTITY;

    avifRGBImage rgb{};
    avifRGBImageSetDefaults(&rgb, image);
    rgb.format = AVIF_RGB_FORMAT_RGBA;
    rgb.depth = 8;
    rgb.pixels = const_cast<std::uint8_t*>(rgba.data());
    rgb.rowBytes = width * 4;
    require_ok(avifImageRGBToYUV(image, &rgb), "rgba converts to yuv444");

    return image;
}

avifEncoder* make_lossless_encoder() {
    avifEncoder* encoder = avifEncoderCreate();
    require(encoder != nullptr, "the encoder starts");
    encoder->quality = AVIF_QUALITY_LOSSLESS;
    encoder->qualityAlpha = AVIF_QUALITY_LOSSLESS;
    encoder->speed = AVIF_SPEED_FASTEST;

    return encoder;
}

std::vector<std::uint8_t>
encode_still(std::uint32_t width, std::uint32_t height, std::span<const std::uint8_t> rgba) {
    avifImage* image = make_image(width, height, rgba);
    avifEncoder* encoder = make_lossless_encoder();

    avifRWData output = AVIF_DATA_EMPTY;
    require_ok(avifEncoderWrite(encoder, image, &output), "the still encodes");

    auto bytes = std::vector<std::uint8_t>{output.data, output.data + output.size};
    avifRWDataFree(&output);
    avifEncoderDestroy(encoder);
    avifImageDestroy(image);

    return bytes;
}

std::vector<std::uint8_t> encode_animation(
    std::uint32_t width,
    std::uint32_t height,
    std::span<const std::uint8_t> first,
    std::span<const std::uint8_t> second,
    std::uint64_t frame_duration
) {
    avifImage* red = make_image(width, height, first);
    avifImage* blue = make_image(width, height, second);
    avifEncoder* encoder = make_lossless_encoder();
    encoder->timescale = 1000;

    require_ok(
        avifEncoderAddImage(encoder, red, frame_duration, AVIF_ADD_IMAGE_FLAG_FORCE_KEYFRAME),
        "the first animation frame encodes"
    );
    require_ok(
        avifEncoderAddImage(encoder, blue, frame_duration, AVIF_ADD_IMAGE_FLAG_NONE),
        "the second animation frame encodes"
    );

    avifRWData output = AVIF_DATA_EMPTY;
    require_ok(avifEncoderFinish(encoder, &output), "the animation finishes");

    auto bytes = std::vector<std::uint8_t>{output.data, output.data + output.size};
    avifRWDataFree(&output);
    avifEncoderDestroy(encoder);
    avifImageDestroy(red);
    avifImageDestroy(blue);

    return bytes;
}

void check_lossless_still_round_trip() {
    const auto pattern = std::array<std::uint8_t, 64>{
        255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 255, 255, 255, 255,
        255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 255, 255, 255, 255,
        255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 255, 255, 255, 255,
        255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 255, 255, 255, 255,
    };
    const auto bytes = encode_still(4, 4, pattern);
    const auto budget = veyra::ImageDecodeBudget{};
    auto info = veyra::ImageDecodeInfo{};

    require(
        veyra::probe_image(bytes, budget, info) == veyra::ImageDecodeResult::complete,
        "an encoded still probes clean"
    );
    require(
        info.format == veyra::ImageFormat::avif && info.width == 4 && info.height == 4 &&
            info.frames == 1,
        "the still probe reports kind, size, and one frame"
    );

    auto frame = std::array<std::uint8_t, 64>{};
    auto targets = std::array<veyra::ImageFrameTarget, 1>{};
    targets[0].pixels = frame;
    auto delays = std::array<std::uint32_t, 1>{0xFFFFFFFFU};
    auto produced = std::uint32_t{};

    require(
        veyra::decode_image(bytes, budget, targets, delays, produced) ==
                veyra::ImageDecodeResult::complete &&
            produced == 1,
        "an encoded still decodes clean"
    );
    require(frame == pattern, "lossless avif pixels round-trip exactly");
    require(delays[0] == 0, "a still avif reports a zero delay");
}

void check_animated_round_trip() {
    auto red = std::array<std::uint8_t, 64>{};
    auto blue = std::array<std::uint8_t, 64>{};

    for (auto index = std::size_t{}; index < red.size(); index += 4) {
        red[index] = 200;
        red[index + 3] = 255;
        blue[index + 2] = 200;
        blue[index + 3] = 255;
    }

    const auto bytes = encode_animation(4, 4, red, blue, 100);
    const auto budget = veyra::ImageDecodeBudget{};
    auto info = veyra::ImageDecodeInfo{};

    require(
        veyra::probe_image(bytes, budget, info) == veyra::ImageDecodeResult::complete,
        "an encoded animation probes clean"
    );
    require(
        info.format == veyra::ImageFormat::avif && info.frames == 2,
        "the animation probe reports two frames"
    );

    auto first = std::array<std::uint8_t, 64>{};
    auto second = std::array<std::uint8_t, 64>{};
    auto targets = std::array<veyra::ImageFrameTarget, 2>{};
    targets[0].pixels = first;
    targets[1].pixels = second;
    auto delays = std::array<std::uint32_t, 2>{};
    auto produced = std::uint32_t{};

    require(
        veyra::decode_image(bytes, budget, targets, delays, produced) ==
                veyra::ImageDecodeResult::complete &&
            produced == 2,
        "an encoded animation decodes clean"
    );
    require(first == red, "the first animation frame round-trips exactly");
    require(second == blue, "the second animation frame round-trips exactly");
    require(delays[0] == 100 && delays[1] == 100, "animation delays report each frame duration");
}

void check_truncated_rejected() {
    const std::array<std::uint8_t, 12> cut{
        0x00,
        0x00,
        0x00,
        0x0C,
        'f',
        't',
        'y',
        'p',
        'a',
        'v',
        'i',
        'f',
    };

    auto info = veyra::ImageDecodeInfo{};
    require(
        veyra::probe_image(cut, veyra::ImageDecodeBudget{}, info) ==
            veyra::ImageDecodeResult::invalid_input,
        "a bare ftyp header fails the probe"
    );
}

void check_budget_enforced() {
    const auto pattern = std::array<std::uint8_t, 64>{
        255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 255, 255, 255, 255,
        255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 255, 255, 255, 255,
        255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 255, 255, 255, 255,
        255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 255, 255, 255, 255,
    };
    const auto bytes = encode_still(4, 4, pattern);

    auto budget = veyra::ImageDecodeBudget{};
    budget.max_width = 1;

    auto info = veyra::ImageDecodeInfo{};
    require(
        veyra::probe_image(bytes, budget, info) == veyra::ImageDecodeResult::too_large,
        "a still wider than budget fails the probe before decoding"
    );
}

}

int main() {
    try {
        check_lossless_still_round_trip();
        check_animated_round_trip();
        check_truncated_rejected();
        check_budget_enforced();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
