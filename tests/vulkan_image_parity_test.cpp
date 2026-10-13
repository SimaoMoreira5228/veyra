#include "veyra/render/software_renderer.hpp"
#include "veyra/render/vulkan_context.hpp"
#include "veyra/render/vulkan_rect_pipeline.hpp"

#include "vulkan_parity_helpers.hpp"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <new>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

std::atomic<std::size_t> g_allocation_count{0};
std::atomic<bool> g_counting{false};

void note_allocation() noexcept {
    if (g_counting.load(std::memory_order_relaxed)) {
        g_allocation_count.fetch_add(1, std::memory_order_relaxed);
    }
}

}

void* operator new(std::size_t size) {
    note_allocation();

    if (size == 0) {
        size = 1;
    }

    void* result = std::malloc(size);

    if (result == nullptr) {
        throw std::bad_alloc{};
    }

    return result;
}

void operator delete(void* ptr) noexcept {
    std::free(ptr);
}

void operator delete(void* ptr, std::size_t) noexcept {
    std::free(ptr);
}

void* operator new[](std::size_t size) {
    return operator new(size);
}

void operator delete[](void* ptr) noexcept {
    operator delete(ptr);
}

void operator delete[](void* ptr, std::size_t size) noexcept {
    operator delete(ptr, size);
}

namespace {

using veyra::vulkan_parity::check_parity;
using veyra::vulkan_parity::render_reference;
using veyra::vulkan_parity::require;
using veyra::vulkan_parity::SoftwareRooms;
using veyra::vulkan_parity::TestClock;

constexpr std::size_t kWidth = 96;
constexpr std::size_t kHeight = 72;

void check_image_parity(
    veyra::VulkanContext& context,
    veyra::VulkanRectPipeline& pipeline,
    std::span<veyra::SoftwarePixel> expected,
    std::span<veyra::SoftwarePixel> actual,
    SoftwareRooms& rooms
) {
    auto nearest_pixels = std::array<veyra::SoftwarePixel, 24>{};

    for (auto index = std::size_t{}; index < nearest_pixels.size(); ++index) {
        nearest_pixels[index] = veyra::SoftwarePixel{
            .red = static_cast<std::uint8_t>((index * 37) % 256),
            .green = static_cast<std::uint8_t>((index * 91) % 256),
            .blue = static_cast<std::uint8_t>((index * 53) % 256),
            .alpha = 255,
        };
    }

    auto linear_pixels = std::array<veyra::SoftwarePixel, 24>{};

    for (auto index = std::size_t{}; index < linear_pixels.size(); ++index) {
        linear_pixels[index] = veyra::SoftwarePixel{
            .red = static_cast<std::uint8_t>(200 - (index * 17) % 200),
            .green = static_cast<std::uint8_t>(40 + (index * 31) % 180),
            .blue = static_cast<std::uint8_t>(90 + (index * 13) % 120),
            .alpha = 255,
        };
    }

    const auto nearest_tile = veyra::SoftwareImage{
        .width = 6,
        .height = 4,
        .stride = 6,
        .pixels = nearest_pixels,
        .generation = 11,
    };

    const auto linear_tile = veyra::SoftwareImage{
        .width = 6,
        .height = 4,
        .stride = 6,
        .pixels = linear_pixels,
        .generation = 12,
    };

    const std::array<veyra::DisplayCommand, 6> storage{
        veyra::FillRect{
            .bounds = {.width = static_cast<float>(kWidth), .height = static_cast<float>(kHeight)},
            .color = {.red = 240, .green = 240, .blue = 244, .alpha = 255},
        },
        veyra::DrawImage{
            .source = {.width = 6.0F, .height = 4.0F},
            .destination = {.x = 8.0F, .y = 10.0F, .width = 12.0F, .height = 8.0F},
            .image = &nearest_tile,
            .sampling = veyra::SoftwareImageSampling::nearest,
            .opacity = 255,
        },
        veyra::DrawImage{
            .source = {.width = 6.0F, .height = 4.0F},
            .destination = {.x = 14.0F, .y = 14.0F, .width = 24.0F, .height = 16.0F},
            .image = &linear_tile,
            .sampling = veyra::SoftwareImageSampling::linear,
            .opacity = 128,
        },
        veyra::FillRect{
            .bounds = {.x = 30.0F, .y = 26.0F, .width = 40.0F, .height = 20.0F},
            .color = {.red = 210, .green = 60, .blue = 40, .alpha = 160},
        },
        veyra::DrawImage{
            .source = {.width = 2.0F, .height = 2.0F},
            .destination = {.x = 60.0F, .y = 50.0F, .width = 8.0F, .height = 8.0F},
            .image = &nearest_tile,
            .sampling = veyra::SoftwareImageSampling::nearest,
            .opacity = 0,
        },
        veyra::DrawImage{
            .source = {.width = 6.0F, .height = 4.0F},
            .destination = {.x = 200.0F, .y = 10.0F, .width = 6.0F, .height = 4.0F},
            .image = &linear_tile,
            .sampling = veyra::SoftwareImageSampling::linear,
            .opacity = 255,
        },
    };

    const auto scene = std::span<const veyra::DisplayCommand>{storage.data(), storage.size()};

    render_reference(scene, expected, kWidth, kHeight, rooms);

    auto stats = veyra::VulkanRectStats{};

    require(
        veyra::render_vulkan_rects(context, pipeline, scene, actual, stats) ==
            veyra::VulkanRenderResult::complete,
        "mixed rect and image scene completes"
    );
    require(stats.submitted == 4, "opacity-zero and offscreen images never submit");
    require(stats.image_draws >= 2, "both samplings draw through the image pipeline");

    check_parity(context, pipeline, scene, expected, actual, "image", 2);
}

void check_image_reupload(
    veyra::VulkanContext& context,
    veyra::VulkanRectPipeline& pipeline,
    std::span<veyra::SoftwarePixel> expected,
    std::span<veyra::SoftwarePixel> actual,
    SoftwareRooms& rooms
) {
    auto pixels = std::array<veyra::SoftwarePixel, 4>{};

    for (auto& pixel : pixels) {
        pixel = veyra::SoftwarePixel{.red = 200, .alpha = 255};
    }

    auto tile = veyra::SoftwareImage{
        .width = 2,
        .height = 2,
        .stride = 2,
        .pixels = pixels,
        .generation = 21,
    };

    const std::array<veyra::DisplayCommand, 1> storage{
        veyra::DrawImage{
            .source = {.width = 2.0F, .height = 2.0F},
            .destination = {.width = 8.0F, .height = 8.0F},
            .image = &tile,
            .sampling = veyra::SoftwareImageSampling::nearest,
            .opacity = 255,
        },
    };

    const auto scene = std::span<const veyra::DisplayCommand>{storage.data(), storage.size()};

    render_reference(scene, expected, kWidth, kHeight, rooms);

    auto stats = veyra::VulkanRectStats{};

    require(
        veyra::render_vulkan_rects(context, pipeline, scene, actual, stats) ==
            veyra::VulkanRenderResult::complete,
        "first image upload renders"
    );

    check_parity(context, pipeline, scene, expected, actual, "image-first", 1);

    for (auto& pixel : pixels) {
        pixel = veyra::SoftwarePixel{.green = 180, .alpha = 255};
    }

    tile.generation = 22;

    render_reference(scene, expected, kWidth, kHeight, rooms);

    require(
        veyra::render_vulkan_rects(context, pipeline, scene, actual, stats) ==
            veyra::VulkanRenderResult::complete,
        "changed image generation re-uploads"
    );

    check_parity(context, pipeline, scene, expected, actual, "image-second", 1);
}

void check_abandoned_upload_rolls_back(
    veyra::VulkanContext& context,
    veyra::VulkanRectPipeline& pipeline,
    std::span<veyra::SoftwarePixel> expected,
    std::span<veyra::SoftwarePixel> actual,
    SoftwareRooms& rooms
) {
    auto pixels = std::array<veyra::SoftwarePixel, 4>{
        veyra::SoftwarePixel{.red = 200, .alpha = 255},
        veyra::SoftwarePixel{.green = 200, .alpha = 255},
        veyra::SoftwarePixel{.blue = 200, .alpha = 255},
        veyra::SoftwarePixel{.red = 200, .green = 200, .alpha = 255},
    };

    auto tile = veyra::SoftwareImage{
        .width = 2,
        .height = 2,
        .stride = 2,
        .pixels = pixels,
        .generation = 41,
    };

    const std::array<veyra::DisplayCommand, 2> poisoned{
        veyra::DrawImage{
            .source = {.width = 2.0F, .height = 2.0F},
            .destination = {.width = 8.0F, .height = 8.0F},
            .image = &tile,
            .sampling = veyra::SoftwareImageSampling::nearest,
            .opacity = 255,
        },
        veyra::PopClip{},
    };

    auto stats = veyra::VulkanRectStats{};

    require(
        veyra::render_vulkan_rects(
            context,
            pipeline,
            std::span<const veyra::DisplayCommand>(poisoned),
            actual,
            stats
        ) == veyra::VulkanRenderResult::invalid_input,
        "images before a rejected command abandon the frame"
    );

    const std::array<veyra::DisplayCommand, 1> recovered{
        veyra::DrawImage{
            .source = {.width = 2.0F, .height = 2.0F},
            .destination = {.width = 8.0F, .height = 8.0F},
            .image = &tile,
            .sampling = veyra::SoftwareImageSampling::nearest,
            .opacity = 255,
        },
    };

    const auto scene = std::span<const veyra::DisplayCommand>{recovered.data(), recovered.size()};

    render_reference(scene, expected, kWidth, kHeight, rooms);

    require(
        veyra::render_vulkan_rects(context, pipeline, scene, actual, stats) ==
            veyra::VulkanRenderResult::complete,
        "rolled back uploads re-upload on the next frame"
    );

    check_parity(context, pipeline, scene, expected, actual, "image-rollback", 1);
}

void check_image_rejections(
    veyra::VulkanContext& context,
    veyra::VulkanRectPipeline& pipeline,
    std::span<veyra::SoftwarePixel> scratch
) {
    auto stats = veyra::VulkanRectStats{};

    const std::array<veyra::DisplayCommand, 1> missing{
        veyra::DrawImage{
            .source = {.width = 2.0F, .height = 2.0F},
            .destination = {.width = 8.0F, .height = 8.0F},
            .image = nullptr,
            .sampling = veyra::SoftwareImageSampling::nearest,
            .opacity = 255,
        },
    };

    require(
        veyra::render_vulkan_rects(
            context,
            pipeline,
            std::span<const veyra::DisplayCommand>(missing),
            scratch,
            stats
        ) == veyra::VulkanRenderResult::invalid_input,
        "imageless draws are rejected"
    );

    auto pixels = std::array<veyra::SoftwarePixel, 4>{};

    const auto tile = veyra::SoftwareImage{
        .width = 2,
        .height = 2,
        .stride = 2,
        .pixels = pixels,
        .generation = 51,
    };

    const std::array<veyra::DisplayCommand, 1> outside{
        veyra::DrawImage{
            .source = {.x = 8.0F, .y = 8.0F, .width = 2.0F, .height = 2.0F},
            .destination = {.width = 8.0F, .height = 8.0F},
            .image = &tile,
            .sampling = veyra::SoftwareImageSampling::nearest,
            .opacity = 255,
        },
    };

    require(
        veyra::render_vulkan_rects(
            context,
            pipeline,
            std::span<const veyra::DisplayCommand>(outside),
            scratch,
            stats
        ) == veyra::VulkanRenderResult::invalid_input,
        "out-of-image sources are rejected"
    );

    auto huge_pixels = std::vector<veyra::SoftwarePixel>(1024 * 1024);

    const auto huge = veyra::SoftwareImage{
        .width = 1024,
        .height = 1024,
        .stride = 1024,
        .pixels = huge_pixels,
        .generation = 52,
    };

    const std::array<veyra::DisplayCommand, 1> oversized{
        veyra::DrawImage{
            .source = {.width = 1024.0F, .height = 1024.0F},
            .destination = {.width = 8.0F, .height = 8.0F},
            .image = &huge,
            .sampling = veyra::SoftwareImageSampling::nearest,
            .opacity = 255,
        },
    };

    require(
        veyra::render_vulkan_rects(
            context,
            pipeline,
            std::span<const veyra::DisplayCommand>(oversized),
            scratch,
            stats
        ) == veyra::VulkanRenderResult::insufficient_capacity,
        "uploads beyond staging report instead of truncating"
    );
}

void check_clipped_image_parity(
    veyra::VulkanContext& context,
    veyra::VulkanRectPipeline& pipeline,
    std::span<veyra::SoftwarePixel> expected,
    std::span<veyra::SoftwarePixel> actual,
    SoftwareRooms& rooms
) {
    auto pixels = std::array<veyra::SoftwarePixel, 64>{};

    for (auto index = std::size_t{}; index < pixels.size(); ++index) {
        pixels[index] = veyra::SoftwarePixel{
            .red = static_cast<std::uint8_t>(200 - (index * 17) % 200),
            .green = static_cast<std::uint8_t>(40 + (index * 31) % 180),
            .blue = static_cast<std::uint8_t>(90 + (index * 13) % 120),
            .alpha = 255,
        };
    }

    const auto tile = veyra::SoftwareImage{
        .width = 8,
        .height = 8,
        .stride = 8,
        .pixels = pixels,
        .generation = 61,
    };

    const std::array<veyra::DisplayCommand, 3> storage{
        veyra::FillRect{
            .bounds = {.width = static_cast<float>(kWidth), .height = static_cast<float>(kHeight)},
            .color = {.red = 240, .green = 240, .blue = 244, .alpha = 255},
        },
        veyra::DrawImage{
            .source = {.width = 8.0F, .height = 8.0F},
            .destination = {.x = 80.5F, .y = 60.0F, .width = 32.0F, .height = 24.0F},
            .image = &tile,
            .sampling = veyra::SoftwareImageSampling::nearest,
            .opacity = 255,
        },
        veyra::DrawImage{
            .source = {.width = 8.0F, .height = 8.0F},
            .destination = {.x = 8.5F, .y = 8.0F, .width = 16.0F, .height = 16.0F},
            .image = &tile,
            .sampling = veyra::SoftwareImageSampling::linear,
            .opacity = 200,
        },
    };

    const auto scene = std::span<const veyra::DisplayCommand>{storage.data(), storage.size()};

    render_reference(scene, expected, kWidth, kHeight, rooms);

    auto stats = veyra::VulkanRectStats{};

    require(
        veyra::render_vulkan_rects(context, pipeline, scene, actual, stats) ==
            veyra::VulkanRenderResult::complete,
        "clipped and fractional images complete"
    );

    check_parity(context, pipeline, scene, expected, actual, "image-clipped", 2);
}

void check_image_capacity(
    veyra::VulkanContext& context,
    veyra::VulkanRectPipeline& pipeline,
    std::span<veyra::SoftwarePixel> scratch
) {
    constexpr auto tile_count = veyra::kVulkanMaxFrameImages + 1;

    auto tile_pixels = std::vector<std::array<veyra::SoftwarePixel, 4>>(tile_count);
    auto tiles = std::vector<veyra::SoftwareImage>(tile_count);
    auto storage = std::vector<veyra::DisplayCommand>(tile_count);

    for (auto index = std::size_t{}; index < tile_count; ++index) {
        tile_pixels[index] = std::array<veyra::SoftwarePixel, 4>{
            veyra::SoftwarePixel{.red = 200, .alpha = 255},
            veyra::SoftwarePixel{.green = 200, .alpha = 255},
            veyra::SoftwarePixel{.blue = 200, .alpha = 255},
            veyra::SoftwarePixel{.red = 200, .green = 200, .alpha = 255},
        };
        tiles[index] = veyra::SoftwareImage{
            .width = 2,
            .height = 2,
            .stride = 2,
            .pixels = tile_pixels[index],
            .generation = index,
        };
        storage[index] = veyra::DrawImage{
            .source = {.width = 2.0F, .height = 2.0F},
            .destination =
                {
                    .x = static_cast<float>(index % 8 * 4),
                    .y = static_cast<float>(index / 8 * 4),
                    .width = 2.0F,
                    .height = 2.0F,
                },
            .image = &tiles[index],
            .sampling = veyra::SoftwareImageSampling::nearest,
            .opacity = 255,
        };
    }

    auto stats = veyra::VulkanRectStats{};

    require(
        veyra::render_vulkan_rects(
            context,
            pipeline,
            std::span<const veyra::DisplayCommand>(storage),
            scratch,
            stats
        ) == veyra::VulkanRenderResult::insufficient_capacity,
        "frames beyond the image cache report instead of thrashing"
    );

    const std::array<veyra::DisplayCommand, 1> recovery{
        veyra::FillRect{
            .bounds = {.width = 8.0F, .height = 8.0F},
            .color = {.red = 200, .alpha = 255},
        },
    };

    require(
        veyra::render_vulkan_rects(context, pipeline, recovery, scratch, stats) ==
            veyra::VulkanRenderResult::complete,
        "the pipeline stays usable after an image rejection"
    );
    require(stats.submitted == 1, "recovery submits the single rect");
}

void check_steady_images_allocate_nothing(
    veyra::VulkanContext& context,
    veyra::VulkanRectPipeline& pipeline,
    std::span<veyra::SoftwarePixel> expected,
    std::span<veyra::SoftwarePixel> actual,
    SoftwareRooms& rooms
) {
    auto pixels = std::array<veyra::SoftwarePixel, 4>{
        veyra::SoftwarePixel{.red = 200, .alpha = 255},
        veyra::SoftwarePixel{.green = 200, .alpha = 255},
        veyra::SoftwarePixel{.blue = 200, .alpha = 255},
        veyra::SoftwarePixel{.red = 200, .green = 200, .alpha = 255},
    };

    auto tile = veyra::SoftwareImage{
        .width = 2,
        .height = 2,
        .stride = 2,
        .pixels = pixels,
        .generation = 31,
    };

    const std::array<veyra::DisplayCommand, 1> storage{
        veyra::DrawImage{
            .source = {.width = 2.0F, .height = 2.0F},
            .destination = {.width = 8.0F, .height = 8.0F},
            .image = &tile,
            .sampling = veyra::SoftwareImageSampling::nearest,
            .opacity = 255,
        },
    };

    const auto scene = std::span<const veyra::DisplayCommand>{storage.data(), storage.size()};

    render_reference(scene, expected, kWidth, kHeight, rooms);

    auto stats = veyra::VulkanRectStats{};

    require(
        veyra::render_vulkan_rects(context, pipeline, scene, actual, stats) ==
            veyra::VulkanRenderResult::complete,
        "steady window opens clean"
    );

    g_allocation_count.store(0, std::memory_order_relaxed);
    g_counting.store(true, std::memory_order_relaxed);

    for (auto repeat = std::size_t{}; repeat < 60; ++repeat) {
        const auto result = veyra::render_vulkan_rects(context, pipeline, scene, actual, stats);

        if (result != veyra::VulkanRenderResult::complete) {
            g_counting.store(false, std::memory_order_relaxed);

            throw std::runtime_error("steady image repeats stay complete");
        }
    }

    g_counting.store(false, std::memory_order_relaxed);

    std::cout << "vulkan image repeats=60 host_allocations="
              << g_allocation_count.load(std::memory_order_relaxed) << '\n';

    require(g_allocation_count.load(std::memory_order_relaxed) == 0, "cached images allocate");
}

int run() {
    auto context = veyra::VulkanContext{};

    require(
        veyra::create_vulkan_context(context, kWidth, kHeight) == veyra::VulkanContextResult::ready,
        "vulkan context comes up"
    );

    auto pipeline = veyra::VulkanRectPipeline{};

    require(
        veyra::create_vulkan_rect_pipeline(context, pipeline) ==
            veyra::VulkanRenderResult::complete,
        "image-capable pipeline comes up"
    );

    auto rooms = SoftwareRooms{};
    auto expected = std::array<veyra::SoftwarePixel, kWidth * kHeight>{};
    auto actual = std::array<veyra::SoftwarePixel, kWidth * kHeight>{};

    check_image_parity(
        context,
        pipeline,
        std::span<veyra::SoftwarePixel>{expected.data(), expected.size()},
        std::span<veyra::SoftwarePixel>{actual.data(), actual.size()},
        rooms
    );

    check_image_reupload(
        context,
        pipeline,
        std::span<veyra::SoftwarePixel>{expected.data(), expected.size()},
        std::span<veyra::SoftwarePixel>{actual.data(), actual.size()},
        rooms
    );

    check_clipped_image_parity(
        context,
        pipeline,
        std::span<veyra::SoftwarePixel>{expected.data(), expected.size()},
        std::span<veyra::SoftwarePixel>{actual.data(), actual.size()},
        rooms
    );

    check_abandoned_upload_rolls_back(
        context,
        pipeline,
        std::span<veyra::SoftwarePixel>{expected.data(), expected.size()},
        std::span<veyra::SoftwarePixel>{actual.data(), actual.size()},
        rooms
    );

    check_image_rejections(
        context,
        pipeline,
        std::span<veyra::SoftwarePixel>{actual.data(), actual.size()}
    );

    check_image_capacity(
        context,
        pipeline,
        std::span<veyra::SoftwarePixel>{actual.data(), actual.size()}
    );

    check_steady_images_allocate_nothing(
        context,
        pipeline,
        std::span<veyra::SoftwarePixel>{expected.data(), expected.size()},
        std::span<veyra::SoftwarePixel>{actual.data(), actual.size()},
        rooms
    );

    veyra::destroy_vulkan_rect_pipeline(context, pipeline);
    veyra::destroy_vulkan_context(context);

    require(!veyra::vulkan_context_ready(context), "context tears down");

    return 0;
}

}

int main() {
    try {
        return run();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
