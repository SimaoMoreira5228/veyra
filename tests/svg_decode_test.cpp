#include "veyra/media/image_codecs.hpp"

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

std::vector<std::uint8_t> bytes_of(std::string_view text) {
    const auto* first = reinterpret_cast<const std::uint8_t*>(text.data());

    return std::vector<std::uint8_t>(first, first + text.size());
}

void check_solid_rect_rasterizes() {
    const auto document = bytes_of(
        "<svg width=\"8\" height=\"8\">"
        "<rect width=\"8\" height=\"8\" fill=\"#ff0000\"/>"
        "</svg>"
    );

    auto info = veyra::ImageDecodeInfo{};
    require(
        veyra::probe_image(document, veyra::ImageDecodeBudget{}, info) ==
            veyra::ImageDecodeResult::complete,
        "a plain svg probes clean"
    );
    require(
        info.format == veyra::ImageFormat::svg && info.width == 8 && info.height == 8 &&
            info.frames == 1,
        "the svg probe reports kind, size, and a single frame"
    );

    auto pixels = std::array<std::uint8_t, 8 * 8 * 4>{};
    auto targets = std::array<veyra::ImageFrameTarget, 1>{};
    targets[0].pixels = pixels;
    auto delays = std::array<std::uint32_t, 1>{0xFFFFFFFFU};
    auto produced = std::uint32_t{};

    require(
        veyra::decode_image(document, veyra::ImageDecodeBudget{}, targets, delays, produced) ==
                veyra::ImageDecodeResult::complete &&
            produced == 1,
        "a plain svg decodes clean"
    );
    require(
        pixels[0] == 255 && pixels[1] == 0 && pixels[2] == 0 && pixels[3] == 255,
        "the rect covers the canvas in opaque red"
    );
    require(pixels[8 * 8 * 4 - 1] == 255, "the last pixel stays opaque");
    require(delays[0] == 0, "a still svg reports a zero delay");
}

void check_circle_leaves_transparent_corners() {
    const auto document = bytes_of(
        "<svg width=\"8\" height=\"8\">"
        "<circle cx=\"4\" cy=\"4\" r=\"3\" fill=\"#0000ff\"/>"
        "</svg>"
    );

    auto pixels = std::array<std::uint8_t, 8 * 8 * 4>{};
    auto targets = std::array<veyra::ImageFrameTarget, 1>{};
    targets[0].pixels = pixels;
    auto delays = std::array<std::uint32_t, 1>{};
    auto produced = std::uint32_t{};

    require(
        veyra::decode_image(document, veyra::ImageDecodeBudget{}, targets, delays, produced) ==
            veyra::ImageDecodeResult::complete,
        "a circle svg decodes clean"
    );

    const auto corner = 0 * 4;
    require(pixels[corner + 3] == 0, "canvas corners stay transparent around the circle");

    const auto center = (4 * 8 + 4) * 4;
    require(
        pixels[center] == 0 && pixels[center + 1] == 0 && pixels[center + 2] == 255 &&
            pixels[center + 3] == 255,
        "the circle center is opaque blue"
    );
}

void check_viewbox_without_size() {
    const auto document = bytes_of(
        "<svg viewBox=\"0 0 4 4\">"
        "<rect width=\"4\" height=\"4\" fill=\"#00ff00\"/>"
        "</svg>"
    );

    auto info = veyra::ImageDecodeInfo{};
    require(
        veyra::probe_image(document, veyra::ImageDecodeBudget{}, info) ==
            veyra::ImageDecodeResult::complete,
        "a viewBox-only svg probes clean"
    );
    require(
        info.width == 4 && info.height == 4,
        "content bounds stand in for missing width and height"
    );
}

void check_malformed_rejected() {
    const auto truncated = bytes_of("<svg width=\"8\" height=\"8\"><rect");

    auto info = veyra::ImageDecodeInfo{};
    require(
        veyra::probe_image(truncated, veyra::ImageDecodeBudget{}, info) ==
            veyra::ImageDecodeResult::complete,
        "a cut-off svg still probes its declared size"
    );

    auto pixels = std::array<std::uint8_t, 8 * 8 * 4>{};
    auto targets = std::array<veyra::ImageFrameTarget, 1>{};
    targets[0].pixels = pixels;
    auto delays = std::array<std::uint32_t, 1>{};
    auto produced = std::uint32_t{};

    require(
        veyra::decode_image(truncated, veyra::ImageDecodeBudget{}, targets, delays, produced) ==
            veyra::ImageDecodeResult::complete,
        "a cut-off svg decodes its partial document"
    );

    auto touched = false;

    for (const auto byte : pixels) {
        if (byte != 0) {
            touched = true;
        }
    }

    require(!touched, "a shapeless document rasterizes fully transparent");

    const auto oversized = bytes_of("<svg width=\"100000\" height=\"100000\"></svg>");
    require(
        veyra::probe_image(oversized, veyra::ImageDecodeBudget{}, info) ==
            veyra::ImageDecodeResult::too_large,
        "absurd svg dimensions fail the budget before rasterizing"
    );
}

}

int main() {
    try {
        check_solid_rect_rasterizes();
        check_circle_leaves_transparent_corners();
        check_viewbox_without_size();
        check_malformed_rejected();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
