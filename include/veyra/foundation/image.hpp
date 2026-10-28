#ifndef VEYRA_FOUNDATION_IMAGE_HPP
#define VEYRA_FOUNDATION_IMAGE_HPP

#include <cstddef>
#include <cstdint>
#include <span>

namespace veyra {

struct SoftwareMask {
    std::size_t width{};
    std::size_t height{};
    std::size_t stride{};
    std::span<const std::uint8_t> coverage;
    std::uint64_t generation{};
};

struct SoftwarePixel {
    std::uint8_t red{};
    std::uint8_t green{};
    std::uint8_t blue{};
    std::uint8_t alpha{255};
};

struct SoftwareImage {
    std::size_t width{};
    std::size_t height{};
    std::size_t stride{};
    std::span<const SoftwarePixel> pixels;
    std::uint64_t generation{};
};

enum class SoftwareImageSampling : std::uint8_t {
    nearest,
    linear,
};

struct SoftwareSurface {
    std::size_t width{};
    std::size_t height{};
    std::size_t stride{};
    std::span<SoftwarePixel> pixels;
};

struct SoftwareClipRect {
    std::size_t left{};
    std::size_t top{};
    std::size_t right{};
    std::size_t bottom{};
};

}

namespace veyra::foundation {
using ::veyra::SoftwareClipRect;
using ::veyra::SoftwareImage;
using ::veyra::SoftwareImageSampling;
using ::veyra::SoftwareMask;
using ::veyra::SoftwarePixel;
using ::veyra::SoftwareSurface;
}

#endif
