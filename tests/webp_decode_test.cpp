#include "veyra/media/image_codecs.hpp"

#include <webp/encode.h>
#include <webp/mux.h>

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

void require_mux_ok(WebPMuxError status, std::string_view message) {
    if (status != WEBP_MUX_OK) {
        throw std::runtime_error(std::string(message));
    }
}

std::vector<std::uint8_t>
encode_still(std::uint32_t width, std::uint32_t height, std::span<const std::uint8_t> rgba) {
    std::uint8_t* raw = nullptr;

    const auto raw_size = WebPEncodeLosslessRGBA(
        rgba.data(),
        static_cast<int>(width),
        static_cast<int>(height),
        static_cast<int>(width * 4),
        &raw
    );
    require(raw != nullptr, "the lossless encoder accepts the still");

    const WebPData raw_chunk{raw, raw_size};
    WebPMux* mux = WebPMuxNew();
    require(mux != nullptr, "the muxer starts");
    require_mux_ok(WebPMuxSetImage(mux, &raw_chunk, 1), "the still bitstream enters the container");

    WebPData assembled{};
    require_mux_ok(WebPMuxAssemble(mux, &assembled), "the still container assembles");
    WebPMuxDelete(mux);
    WebPFree(raw);

    auto bytes = std::vector<std::uint8_t>{assembled.bytes, assembled.bytes + assembled.size};
    WebPDataClear(&assembled);

    return bytes;
}

std::vector<std::uint8_t> encode_animation(
    std::uint32_t width,
    std::uint32_t height,
    std::span<const std::uint8_t> first,
    std::span<const std::uint8_t> second,
    int second_ms
) {
    WebPAnimEncoderOptions options{};
    WebPAnimEncoderOptionsInit(&options);

    WebPAnimEncoder* encoder =
        WebPAnimEncoderNew(static_cast<int>(width), static_cast<int>(height), &options);
    require(encoder != nullptr, "the animation encoder starts");

    WebPConfig config{};
    require(WebPConfigInit(&config) != 0, "the encoder config initializes");
    config.lossless = 1;
    config.method = 0;
    require(WebPValidateConfig(&config) != 0, "the encoder config validates");

    WebPPicture first_picture{};
    require(WebPPictureInit(&first_picture) != 0, "the first frame picture initializes");
    first_picture.width = static_cast<int>(width);
    first_picture.height = static_cast<int>(height);
    require(
        WebPPictureImportRGBA(&first_picture, first.data(), static_cast<int>(width * 4)) != 0,
        "the first frame imports"
    );
    require(
        WebPAnimEncoderAdd(encoder, &first_picture, 0, &config) != 0,
        "the first animation frame is accepted"
    );
    WebPPictureFree(&first_picture);

    WebPPicture second_picture{};
    require(WebPPictureInit(&second_picture) != 0, "the second frame picture initializes");
    second_picture.width = static_cast<int>(width);
    second_picture.height = static_cast<int>(height);
    require(
        WebPPictureImportRGBA(&second_picture, second.data(), static_cast<int>(width * 4)) != 0,
        "the second frame imports"
    );
    require(
        WebPAnimEncoderAdd(encoder, &second_picture, second_ms, &config) != 0,
        "the second animation frame is accepted"
    );
    WebPPictureFree(&second_picture);
    require(
        WebPAnimEncoderAdd(encoder, nullptr, second_ms + 100, nullptr) != 0,
        "the animation stream terminates"
    );

    WebPData assembled{};
    require(WebPAnimEncoderAssemble(encoder, &assembled) != 0, "the animation assembles");
    WebPAnimEncoderDelete(encoder);

    auto bytes = std::vector<std::uint8_t>{assembled.bytes, assembled.bytes + assembled.size};
    WebPDataClear(&assembled);

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
        info.format == veyra::ImageFormat::webp && info.width == 4 && info.height == 4 &&
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
    require(frame == pattern, "lossless webp pixels round-trip exactly");
    require(delays[0] == 0, "a still reports a zero delay");
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
        info.format == veyra::ImageFormat::webp && info.frames == 2,
        "the animation probe reports two frames"
    );

    auto first = std::array<std::uint8_t, 64>{};
    auto second = std::array<std::uint8_t, 64>{};
    auto targets = std::array<veyra::ImageFrameTarget, 2>{};
    targets[0].pixels = first;
    targets[1].pixels = second;
    auto delays = std::array<std::uint32_t, 2>{0xFFFFFFFFU, 0xFFFFFFFFU};
    auto produced = std::uint32_t{};

    require(
        veyra::decode_image(bytes, budget, targets, delays, produced) ==
                veyra::ImageDecodeResult::complete &&
            produced == 2,
        "an encoded animation decodes clean"
    );
    require(delays[0] == 100, "the first animation frame spans to its timestamp");
    require(delays[1] == 100, "the second animation frame keeps its timestamp");

    for (auto index = std::size_t{}; index < first.size(); ++index) {
        const auto want = red[index];
        const auto got = first[index];
        const auto drift = want > got ? want - got : got - want;
        require(drift <= 3, "animated frames survive blending within tolerance");

        const auto want_blue = blue[index];
        const auto got_blue = second[index];
        const auto drift_blue = want_blue > got_blue ? want_blue - got_blue : got_blue - want_blue;
        require(drift_blue <= 3, "the second animation keeps its colors within tolerance");
    }
}

void check_truncated_rejected() {
    const std::array<std::uint8_t, 12> cut{
        'R',
        'I',
        'F',
        'F',
        0x04,
        0x00,
        0x00,
        0x00,
        'W',
        'E',
        'B',
        'P',
    };

    auto info = veyra::ImageDecodeInfo{};
    require(
        veyra::probe_image(cut, veyra::ImageDecodeBudget{}, info) ==
            veyra::ImageDecodeResult::invalid_input,
        "a bare riff header fails the probe"
    );
}

}

int main() {
    try {
        check_lossless_still_round_trip();
        check_animated_round_trip();
        check_truncated_rejected();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
