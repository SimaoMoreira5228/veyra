#include "veyra/media/codec/image_codecs.hpp"

#include "image_codecs_internal.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string_view>

namespace veyra {
namespace {

ImageFormat sniff_format(std::span<const std::uint8_t> bytes) noexcept {
    if (codec_detail::starts_with(bytes, std::string_view{"\xFF\xD8\xFF", 3})) {
        return ImageFormat::jpeg;
    }

    if (codec_detail::starts_with(bytes, std::string_view{"\x89PNG\r\n\x1A\n", 8})) {
        return ImageFormat::png;
    }

    if (codec_detail::starts_with(bytes, "GIF87a") || codec_detail::starts_with(bytes, "GIF89a")) {
        return ImageFormat::gif;
    }

    if (codec_detail::starts_with(bytes, "BM")) {
        return ImageFormat::bmp;
    }

    if (codec_detail::has_bytes(bytes, 12) && codec_detail::starts_with(bytes, "RIFF") &&
        std::string_view{
            reinterpret_cast<const char*>(bytes.data() + 8),
            4,
        } == "WEBP") {
        return ImageFormat::webp;
    }

    if (codec_detail::starts_with(bytes, "8BPS")) {
        return ImageFormat::psd;
    }

    if (codec_detail::starts_with(bytes, "#?RADIANCE") ||
        codec_detail::starts_with(bytes, "#?RGBE")) {
        return ImageFormat::hdr;
    }

    if (bytes.size() >= 3 && bytes[0] == 'P' && bytes[1] >= '1' && bytes[1] <= '6' &&
        (bytes[2] == ' ' || bytes[2] == '\t' || bytes[2] == '\n' || bytes[2] == '\r' ||
         bytes[2] == '#')) {
        return ImageFormat::pnm;
    }

    if (codec_detail::has_bytes(bytes, 26) &&
        std::string_view{reinterpret_cast<const char*>(bytes.data() + bytes.size() - 18), 18} ==
            "TRUEVISION-XFILE.\0") {
        return ImageFormat::tga;
    }

    if (codec_detail::has_bytes(bytes, 12) && codec_detail::starts_with(bytes.subspan(4), "ftyp")) {
        const auto brand = std::string_view{
            reinterpret_cast<const char*>(bytes.data() + 8),
            4,
        };

        if (brand == "avif" || brand == "avis") {
            return ImageFormat::avif;
        }
    }

    auto cursor = std::size_t{};

    while (cursor < bytes.size() && cursor < 1024) {
        const auto byte = bytes[cursor];

        if (byte != ' ' && byte != '\t' && byte != '\n' && byte != '\r') {
            break;
        }

        ++cursor;
    }

    const auto head = bytes.subspan(cursor);

    if (codec_detail::starts_with(head, "<svg")) {
        return ImageFormat::svg;
    }

    if (codec_detail::starts_with(head, "<?xml") && head.size() > 5) {
        const auto window = head.size() < 1024 ? head.size() : 1024;

        for (auto index = std::size_t{5}; index + 4 <= window; ++index) {
            if (head[index] == '<' && head[index + 1] == 's' && head[index + 2] == 'v' &&
                head[index + 3] == 'g') {
                return ImageFormat::svg;
            }
        }
    }

    return ImageFormat::unknown;
}

}

namespace codec_detail {

bool over_budget(
    const ImageDecodeBudget& budget,
    std::uint32_t width,
    std::uint32_t height,
    std::uint32_t frames
) noexcept {
    if (width == 0 || height == 0 || frames == 0) {
        return true;
    }

    if (width > budget.max_width || height > budget.max_height) {
        return true;
    }

    if (frames > budget.max_frames) {
        return true;
    }

    const auto pixels = static_cast<std::uint64_t>(width) * static_cast<std::uint64_t>(height);

    return pixels > static_cast<std::uint64_t>(budget.max_width) *
        static_cast<std::uint64_t>(budget.max_height);
}

}

ImageDecodeResult probe_image(
    std::span<const std::uint8_t> bytes,
    const ImageDecodeBudget& budget,
    ImageDecodeInfo& info
) noexcept {
    if (bytes.empty() || bytes.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        return ImageDecodeResult::invalid_input;
    }

    const auto format = sniff_format(bytes);

#ifdef VEYRA_WITH_AVIF
    if (format == ImageFormat::avif) {
        return codec_detail::avif_probe(bytes, budget, info);
    }
#else
    if (format == ImageFormat::avif) {
        return ImageDecodeResult::unsupported_format;
    }
#endif

#ifdef VEYRA_WITH_SVG
    if (format == ImageFormat::svg) {
        return codec_detail::svg_probe(bytes, budget, info);
    }
#else
    if (format == ImageFormat::svg) {
        return ImageDecodeResult::unsupported_format;
    }
#endif

#ifdef VEYRA_WITH_WEBP
    if (format == ImageFormat::webp) {
        return codec_detail::webp_probe(bytes, budget, info);
    }
#else
    if (format == ImageFormat::webp) {
        return ImageDecodeResult::unsupported_format;
    }
#endif

    if (format == ImageFormat::gif) {
        return codec_detail::probe_gif(bytes, budget, info);
    }

    return codec_detail::probe_still(bytes, format, budget, info);
}

ImageDecodeResult decode_image(
    std::span<const std::uint8_t> bytes,
    const ImageDecodeBudget& budget,
    std::span<ImageFrameTarget> frames,
    std::span<std::uint32_t> delays_ms,
    std::uint32_t& decoded
) noexcept {
    decoded = 0;

    if (bytes.empty() || bytes.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        return ImageDecodeResult::invalid_input;
    }

    const auto format = sniff_format(bytes);

#ifdef VEYRA_WITH_AVIF
    if (format == ImageFormat::avif) {
        auto probed = ImageDecodeInfo{};
        const auto probed_status = codec_detail::avif_probe(bytes, budget, probed);

        if (probed_status != ImageDecodeResult::complete) {
            return probed_status;
        }

        return codec_detail::avif_decode_frames(
            bytes,
            probed.width,
            probed.height,
            probed.frames,
            frames,
            delays_ms,
            decoded
        );
    }
#else
    if (format == ImageFormat::avif) {
        return ImageDecodeResult::unsupported_format;
    }
#endif

#ifdef VEYRA_WITH_SVG
    if (format == ImageFormat::svg) {
        auto probed = ImageDecodeInfo{};
        const auto probed_status = codec_detail::svg_probe(bytes, budget, probed);

        if (probed_status != ImageDecodeResult::complete) {
            return probed_status;
        }

        return codec_detail::svg_decode(
            bytes,
            probed.width,
            probed.height,
            frames,
            delays_ms,
            decoded
        );
    }
#else
    if (format == ImageFormat::svg) {
        return ImageDecodeResult::unsupported_format;
    }
#endif

#ifdef VEYRA_WITH_WEBP
    if (format == ImageFormat::webp) {
        auto probed = ImageDecodeInfo{};
        const auto probed_status = codec_detail::webp_probe(bytes, budget, probed);

        if (probed_status != ImageDecodeResult::complete) {
            return probed_status;
        }

        return codec_detail::webp_decode_frames(
            bytes,
            probed.width,
            probed.height,
            probed.frames,
            frames,
            delays_ms,
            decoded
        );
    }
#else
    if (format == ImageFormat::webp) {
        return ImageDecodeResult::unsupported_format;
    }
#endif

    if (format == ImageFormat::gif) {
        auto probed = ImageDecodeInfo{};
        const auto probed_status = codec_detail::probe_gif(bytes, budget, probed);

        if (probed_status != ImageDecodeResult::complete) {
            return probed_status;
        }

        return codec_detail::decode_gif(bytes, budget, frames, delays_ms, decoded);
    }

    auto probed = ImageDecodeInfo{};
    const auto probed_status = codec_detail::probe_still(bytes, format, budget, probed);

    if (probed_status != ImageDecodeResult::complete) {
        return probed_status;
    }

    return codec_detail::decode_still(
        bytes,
        probed.width,
        probed.height,
        frames,
        delays_ms,
        decoded
    );
}

}
