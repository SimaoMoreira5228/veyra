#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#include "veyra/media/codec/image_codecs.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

void require(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

void append_bytes(void* context, void* data, int size) {
    auto* bytes = static_cast<std::vector<std::uint8_t>*>(context);

    if (size <= 0) {
        return;
    }

    const auto* source = static_cast<const std::uint8_t*>(data);
    bytes->insert(bytes->end(), source, source + size);
}

std::vector<std::uint8_t>
encode_png(std::uint32_t width, std::uint32_t height, std::span<const std::uint8_t> rgba) {
    auto bytes = std::vector<std::uint8_t>{};

    require(
        stbi_write_png_to_func(
            append_bytes,
            &bytes,
            static_cast<int>(width),
            static_cast<int>(height),
            4,
            rgba.data(),
            static_cast<int>(width * 4)
        ) != 0,
        "the png encoder accepts the fixture"
    );

    return bytes;
}

std::vector<std::uint8_t>
encode_jpeg(std::uint32_t width, std::uint32_t height, std::span<const std::uint8_t> rgb) {
    auto bytes = std::vector<std::uint8_t>{};

    require(
        stbi_write_jpg_to_func(
            append_bytes,
            &bytes,
            static_cast<int>(width),
            static_cast<int>(height),
            3,
            rgb.data(),
            95
        ) != 0,
        "the jpeg encoder accepts the fixture"
    );

    return bytes;
}

std::array<std::uint8_t, 16> checker_pixels() {
    return {{
        255,
        0,
        0,
        255,
        0,
        255,
        0,
        255,
        0,
        0,
        255,
        255,
        255,
        255,
        255,
        255,
    }};
}

struct Decoded {
    veyra::ImageDecodeInfo info{};
    std::vector<std::uint8_t> pixels;
    std::vector<std::uint32_t> delays;
    veyra::ImageDecodeResult result{veyra::ImageDecodeResult::invalid_input};
};

Decoded decode_all(
    std::span<const std::uint8_t> bytes,
    const veyra::ImageDecodeBudget& budget,
    std::size_t max_frames
) {
    auto decoded = Decoded{};

    if (veyra::probe_image(bytes, budget, decoded.info) != veyra::ImageDecodeResult::complete) {
        return decoded;
    }

    if (decoded.info.frames == 0 || decoded.info.frames > max_frames) {
        return decoded;
    }

    const auto frame_bytes = static_cast<std::size_t>(decoded.info.width) * decoded.info.height * 4;

    decoded.pixels.resize(frame_bytes * decoded.info.frames);
    decoded.delays.resize(decoded.info.frames, 0xFFFFFFFFU);

    auto targets = std::vector<veyra::ImageFrameTarget>(decoded.info.frames);

    for (auto index = std::size_t{}; index < targets.size(); ++index) {
        targets[index].pixels = std::span<std::uint8_t>{
            decoded.pixels.data() + index * frame_bytes,
            frame_bytes,
        };
    }

    auto produced = std::uint32_t{};
    decoded.result = veyra::decode_image(bytes, budget, targets, decoded.delays, produced);

    require(produced == decoded.info.frames, "every probed frame decodes");

    return decoded;
}

void check_png_round_trip() {
    const auto pattern = checker_pixels();
    const auto bytes = encode_png(2, 2, pattern);
    const auto budget = veyra::ImageDecodeBudget{};
    auto info = veyra::ImageDecodeInfo{};

    require(
        veyra::probe_image(bytes, budget, info) == veyra::ImageDecodeResult::complete,
        "a written png probes clean"
    );
    require(
        info.format == veyra::ImageFormat::png && info.width == 2 && info.height == 2 &&
            info.frames == 1,
        "the png probe reports kind, size, and a single frame"
    );

    const auto decoded = decode_all(bytes, budget, 4);
    require(decoded.result == veyra::ImageDecodeResult::complete, "a written png decodes clean");
    require(
        std::memcmp(decoded.pixels.data(), pattern.data(), pattern.size()) == 0,
        "png pixels round-trip exactly"
    );
    require(decoded.delays[0] == 0, "a still reports a zero delay");
}

void check_jpeg_round_trip() {
    const auto red = std::array<std::uint8_t, 3 * 16>{
        200, 30,  30,  200, 30,  30,  200, 30,  30,  200, 30,  30,  200, 30,  30,  200,
        30,  30,  200, 30,  30,  200, 30,  30,  200, 30,  30,  200, 30,  30,  200, 30,
        30,  200, 30,  30,  200, 30,  30,  200, 30,  30,  200, 30,  30,  200, 30,  30,
    };
    const auto bytes = encode_jpeg(4, 4, red);
    const auto budget = veyra::ImageDecodeBudget{};
    auto info = veyra::ImageDecodeInfo{};

    require(
        veyra::probe_image(bytes, budget, info) == veyra::ImageDecodeResult::complete,
        "a written jpeg probes clean"
    );
    require(
        info.format == veyra::ImageFormat::jpeg && info.width == 4 && info.height == 4,
        "the jpeg probe reports kind and size"
    );

    const auto decoded = decode_all(bytes, budget, 4);
    require(decoded.result == veyra::ImageDecodeResult::complete, "a written jpeg decodes clean");

    const auto center_red = decoded.pixels[(2 * 4 + 2) * 4];

    require(
        center_red > 175 && decoded.pixels[(2 * 4 + 2) * 4 + 3] == 255,
        "jpeg keeps the flat red within tolerance"
    );
}

void check_bmp_pixels() {
    const std::array<std::uint8_t, 70> bmp{{
        'B',  'M',  0x46, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x36, 0x00, 0x00, 0x00,
        0x28, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x01, 0x00,
        0x18, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0x00,
        0x00, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0x00, 0xFF, 0x00, 0x00, 0x00,
    }};

    const auto decoded = decode_all(bmp, veyra::ImageDecodeBudget{}, 4);

    require(
        decoded.result == veyra::ImageDecodeResult::complete &&
            decoded.info.format == veyra::ImageFormat::bmp,
        "a hand-built bmp decodes clean"
    );
    require(
        decoded.pixels[0] == 255 && decoded.pixels[1] == 0 && decoded.pixels[2] == 0 &&
            decoded.pixels[3] == 255,
        "bmp red lands top-left after the bottom-up flip"
    );
    require(
        decoded.pixels[8] == 0 && decoded.pixels[9] == 0 && decoded.pixels[10] == 255,
        "bmp blue lands bottom-left"
    );
}

void check_pnm_pixels() {
    const std::array<std::uint8_t, 23> ppm{
        'P', '6', '\n', '2', ' ', '2', '\n', '2', '5', '5', '\n', 255,
        0,   0,   0,    255, 0,   0,   0,    255, 255, 255, 255,
    };

    const auto decoded = decode_all(ppm, veyra::ImageDecodeBudget{}, 4);

    require(
        decoded.result == veyra::ImageDecodeResult::complete &&
            decoded.info.format == veyra::ImageFormat::pnm,
        "a hand-built ppm decodes clean"
    );
    require(
        decoded.pixels[0] == 255 && decoded.pixels[5] == 255 && decoded.pixels[10] == 255,
        "ppm channels land in order"
    );
}

void check_psd_pixels() {
    const std::array<std::uint8_t, 52> psd{{
        '8',  'B',  'P',  'S',  0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x03, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x02, 0x00, 0x08, 0x00, 0x03,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 255,  0,    0,    255,  0,    255,  0,    255,  0,    0,    255,  255,
    }};

    const auto decoded = decode_all(psd, veyra::ImageDecodeBudget{}, 4);

    require(
        decoded.result == veyra::ImageDecodeResult::complete &&
            decoded.info.format == veyra::ImageFormat::psd,
        "a hand-built psd decodes clean"
    );
    require(
        decoded.pixels[0] == 255 && decoded.pixels[1] == 0 && decoded.pixels[2] == 0 &&
            decoded.pixels[8] == 0 && decoded.pixels[9] == 0 && decoded.pixels[10] == 255,
        "psd planes de-interleave into rgb pixels"
    );
}

void check_animated_gif() {
    const std::array<std::uint8_t, 76> gif{{
        0x47, 0x49, 0x46, 0x38, 0x39, 0x61, 0x02, 0x00, 0x02, 0x00, 0xF0, 0x00, 0x00,
        0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0x21, 0xF9, 0x04, 0x00, 0x0A, 0x00, 0x00,
        0x00, 0x2C, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x02, 0x00, 0x00, 0x08, 0x07,
        0x00, 0x01, 0x04, 0x08, 0x00, 0x20, 0x20, 0x00, 0x21, 0xF9, 0x04, 0x00, 0x14,
        0x00, 0x00, 0x00, 0x2C, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x02, 0x00, 0x00,
        0x08, 0x07, 0x00, 0x03, 0x00, 0x00, 0x10, 0x20, 0x20, 0x00, 0x3B,
    }};

    const auto decoded = decode_all(gif, veyra::ImageDecodeBudget{}, 4);

    require(
        decoded.result == veyra::ImageDecodeResult::complete &&
            decoded.info.format == veyra::ImageFormat::gif && decoded.info.frames == 2,
        "a two-frame gif probes two frames"
    );
    require(
        decoded.delays[0] == 100 && decoded.delays[1] == 200,
        "gif delays convert centiseconds to milliseconds"
    );
    require(
        decoded.pixels[0] == 0 && decoded.pixels[1] == 0 && decoded.pixels[2] == 0 &&
            decoded.pixels[4] == 255 && decoded.pixels[5] == 255 && decoded.pixels[6] == 255,
        "the first frame shows black then white"
    );

    const auto second = decoded.pixels.data() + 16;
    require(second[0] == 255 && second[4] == 0, "the second frame inverts the first");
}

void check_garbage_rejected() {
    const std::string_view junk = "definitely not an image file at all";
    const auto bytes = std::span<const std::uint8_t>{
        reinterpret_cast<const std::uint8_t*>(junk.data()),
        junk.size(),
    };

    auto info = veyra::ImageDecodeInfo{};
    require(
        veyra::probe_image(bytes, veyra::ImageDecodeBudget{}, info) ==
            veyra::ImageDecodeResult::invalid_input,
        "garbage fails the probe"
    );

    auto targets = std::array<veyra::ImageFrameTarget, 1>{};
    auto delays = std::array<std::uint32_t, 1>{};
    auto produced = std::uint32_t{};
    require(
        veyra::decode_image(bytes, veyra::ImageDecodeBudget{}, targets, delays, produced) ==
            veyra::ImageDecodeResult::invalid_input,
        "garbage fails the decode"
    );
    require(produced == 0, "a failed decode produces nothing");
}

void check_truncated_rejected() {
    const auto pattern = checker_pixels();
    const auto full = encode_png(2, 2, pattern);

    auto info = veyra::ImageDecodeInfo{};
    require(
        veyra::probe_image(
            std::span<const std::uint8_t>{full.data(), 20},
            veyra::ImageDecodeBudget{},
            info
        ) == veyra::ImageDecodeResult::invalid_input,
        "a header-cut png fails the probe"
    );

    const std::array<std::uint8_t, 8> cut_gif{'G', 'I', 'F', '8', '9', 'a', 0x02, 0x00};
    require(
        veyra::probe_image(cut_gif, veyra::ImageDecodeBudget{}, info) ==
            veyra::ImageDecodeResult::invalid_input,
        "a cut-off gif header fails the probe"
    );
}

void check_budgets_enforced() {
    const auto pattern = checker_pixels();
    const auto bytes = encode_png(2, 2, pattern);

    auto tight = veyra::ImageDecodeBudget{};
    tight.max_width = 1;
    tight.max_height = 1;

    auto info = veyra::ImageDecodeInfo{};
    require(
        veyra::probe_image(bytes, tight, info) == veyra::ImageDecodeResult::too_large,
        "a 2x2 image exceeds a 1x1 budget at probe time"
    );

    tight = veyra::ImageDecodeBudget{};
    tight.max_frames = 0;
    require(
        veyra::probe_image(bytes, tight, info) == veyra::ImageDecodeResult::too_large,
        "a zero frame budget rejects even a still"
    );

    const std::array<std::uint8_t, 56> big_header{
        'B',  'M',  0x46, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x36, 0x00, 0x00, 0x00,
        0x28, 0x00, 0x00, 0x00, 0x20, 0x4E, 0x00, 0x00, 0x20, 0x4E, 0x00, 0x00, 0x01, 0x00,
        0x18, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    };
    require(
        veyra::probe_image(big_header, veyra::ImageDecodeBudget{}, info) ==
            veyra::ImageDecodeResult::too_large,
        "a lying bitmap header is caught without touching pixel data"
    );
}

void check_output_sizes_enforced() {
    const auto pattern = checker_pixels();
    const auto bytes = encode_png(2, 2, pattern);
    const auto budget = veyra::ImageDecodeBudget{};

    auto tiny = std::array<std::uint8_t, 15>{};
    auto targets = std::array<veyra::ImageFrameTarget, 1>{};
    targets[0].pixels = tiny;
    auto delays = std::array<std::uint32_t, 1>{0xFFFFFFFFU};
    auto produced = std::uint32_t{};

    require(
        veyra::decode_image(bytes, budget, targets, delays, produced) ==
            veyra::ImageDecodeResult::insufficient_output,
        "a one-byte-short buffer is rejected"
    );
    require(produced == 0, "a rejected decode produces nothing");

    auto no_frames = std::span<veyra::ImageFrameTarget>{};
    require(
        veyra::decode_image(bytes, budget, no_frames, delays, produced) ==
            veyra::ImageDecodeResult::insufficient_frames,
        "decoding with no frame slots is rejected"
    );

    auto no_delays = std::span<std::uint32_t>{};
    auto roomy = std::array<std::uint8_t, 16>{};
    targets[0].pixels = roomy;
    require(
        veyra::decode_image(bytes, budget, targets, no_delays, produced) ==
            veyra::ImageDecodeResult::insufficient_output,
        "decoding with no delay slots is rejected"
    );
}

void check_unknown_and_unsupported() {
    const std::string_view text = "PK\x03\x04 not really a zip either";
    const auto bytes = std::span<const std::uint8_t>{
        reinterpret_cast<const std::uint8_t*>(text.data()),
        text.size(),
    };

    auto info = veyra::ImageDecodeInfo{};
    require(
        veyra::probe_image(bytes, veyra::ImageDecodeBudget{}, info) ==
            veyra::ImageDecodeResult::invalid_input,
        "an unknown container fails the probe"
    );

    const std::array<std::uint8_t, 12> riff{
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

#ifdef VEYRA_WITH_WEBP
    require(
        veyra::probe_image(riff, veyra::ImageDecodeBudget{}, info) ==
            veyra::ImageDecodeResult::invalid_input,
        "a bare riff header fails the probe even where webp decodes"
    );
#else
    require(
        veyra::probe_image(riff, veyra::ImageDecodeBudget{}, info) ==
            veyra::ImageDecodeResult::unsupported_format,
        "webp sniffs as webp even where it is not built in"
    );
#endif

#ifdef VEYRA_WITH_SVG
    const std::string_view svg_text = "<svg width=\"8\" height=\"8\"></svg>";
    const auto svg_doc = std::span<const std::uint8_t>{
        reinterpret_cast<const std::uint8_t*>(svg_text.data()),
        svg_text.size(),
    };
    require(
        veyra::probe_image(svg_doc, veyra::ImageDecodeBudget{}, info) ==
                veyra::ImageDecodeResult::complete &&
            info.format == veyra::ImageFormat::svg,
        "svg probes clean where it is built in"
    );
#else
    const std::string_view svg_text = "<svg width=\"8\" height=\"8\"></svg>";
    const auto svg_doc = std::span<const std::uint8_t>{
        reinterpret_cast<const std::uint8_t*>(svg_text.data()),
        svg_text.size(),
    };
    require(
        veyra::probe_image(svg_doc, veyra::ImageDecodeBudget{}, info) ==
            veyra::ImageDecodeResult::unsupported_format,
        "svg sniffs as svg even where it is not built in"
    );
#endif

    const std::array<std::uint8_t, 12> avif{
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
#ifdef VEYRA_WITH_AVIF
    require(
        veyra::probe_image(avif, veyra::ImageDecodeBudget{}, info) ==
            veyra::ImageDecodeResult::invalid_input,
        "a bare ftyp header fails the probe even where avif decodes"
    );
#else
    require(
        veyra::probe_image(avif, veyra::ImageDecodeBudget{}, info) ==
            veyra::ImageDecodeResult::unsupported_format,
        "avif sniffs as avif even where it is not built in"
    );
#endif
}

}

int main() {
    try {
        check_png_round_trip();
        check_jpeg_round_trip();
        check_bmp_pixels();
        check_pnm_pixels();
        check_psd_pixels();
        check_animated_gif();
        check_garbage_rejected();
        check_truncated_rejected();
        check_budgets_enforced();
        check_output_sizes_enforced();
        check_unknown_and_unsupported();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
