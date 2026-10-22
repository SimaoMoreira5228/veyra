#include "allocation_counter.hpp"
#include "veyra/render/software_renderer.hpp"
#include "veyra/render/vulkan_context.hpp"
#include "veyra/render/vulkan_present.hpp"
#include "veyra/render/vulkan_rect_pipeline.hpp"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <span>
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

constexpr std::size_t kWidth = 96;
constexpr std::size_t kHeight = 72;
constexpr std::size_t kSmallWidth = 48;
constexpr std::size_t kSmallHeight = 36;
constexpr std::uint64_t kAcquireTimeoutNs = 1'000'000'000ULL;

void build_scene(
    std::vector<veyra::DisplayCommand>& storage,
    std::size_t width,
    std::size_t height
) {
    storage.clear();
    storage.push_back(
        veyra::FillRect{
            .bounds = {.width = static_cast<float>(width), .height = static_cast<float>(height)},
            .color = {.red = 12, .green = 20, .blue = 28, .alpha = 255},
        }
    );
    storage.push_back(
        veyra::FillRect{
            .bounds =
                {.y = 10.0F,
                 .width = static_cast<float>(width),
                 .height = static_cast<float>(height) / 4.0F},
            .color = {.red = 200, .green = 40, .blue = 30, .alpha = 255},
        }
    );
    storage.push_back(
        veyra::FillRect{
            .bounds = {.x = 8.0F, .y = 20.0F, .width = 40.0F, .height = 30.0F},
            .color = {.red = 30, .green = 140, .blue = 60, .alpha = 255},
        }
    );
    storage.push_back(veyra::PushTranslation{.offset = {.x = 4.0F, .y = 6.0F}});
    storage.push_back(
        veyra::FillRect{
            .bounds = {.x = 10.0F, .y = 8.0F, .width = 22.0F, .height = 12.0F},
            .color = {.red = 230, .green = 180, .blue = 20, .alpha = 255},
        }
    );
    storage.push_back(veyra::PopTranslation{});
}

void render_offscreen(
    veyra::VulkanContext& context,
    veyra::VulkanRectPipeline& pipeline,
    std::span<const veyra::DisplayCommand> scene,
    std::vector<veyra::SoftwarePixel>& pixels
) {
    pixels.assign(context.width * context.height, veyra::SoftwarePixel{});
    auto stats = veyra::VulkanRectStats{};

    require(
        veyra::render_vulkan_rects(
            context,
            pipeline,
            scene,
            std::span<veyra::SoftwarePixel>(pixels),
            stats
        ) == veyra::VulkanRenderResult::complete,
        "offscreen reference renders"
    );
}

void present_scene(veyra::VulkanPresenter& presenter, std::vector<veyra::SoftwarePixel>& pixels) {
    auto image = std::uint32_t{};

    require(
        veyra::acquire_vulkan_frame(presenter, kAcquireTimeoutNs, image) ==
            veyra::VulkanPresentResult::ready,
        "acquire returns a swapchain image"
    );

    auto stats = veyra::VulkanPresentStats{};

    require(
        veyra::present_vulkan_frame(presenter, image, &stats) == veyra::VulkanPresentResult::ready,
        "present copies the current target and presents it"
    );
    require(stats.image == image, "present stats name the presented image");

    pixels.assign(presenter.width * presenter.height, veyra::SoftwarePixel{});

    require(
        veyra::read_vulkan_presenter_pixels(presenter, std::span<veyra::SoftwarePixel>(pixels)) ==
            veyra::VulkanPresentResult::ready,
        "presented pixels read back"
    );
}

void require_pixels_equal(
    std::span<const veyra::SoftwarePixel> want,
    std::span<const veyra::SoftwarePixel> got,
    std::string_view stage
) {
    require(want.size() == got.size(), "parity compares equal extents");

    auto mismatches = std::size_t{};

    for (auto i = std::size_t{}; i < want.size(); ++i) {
        if (std::memcmp(&want[i], &got[i], sizeof(veyra::SoftwarePixel)) != 0) {
            ++mismatches;
        }
    }

    if (mismatches != 0) {
        std::cout << "vulkan present stage=" << stage << " mismatched_pixels=" << mismatches
                  << '\n';
    }

    require(mismatches == 0, "presented pixels match the offscreen render byte-exact");
}

void check_invalid_input() {
    auto missing = veyra::VulkanContext{};
    auto presenter = veyra::VulkanPresenter{};
    auto image = std::uint32_t{};

    require(
        veyra::create_vulkan_presenter(missing, kWidth, kHeight, nullptr, presenter) ==
            veyra::VulkanPresentResult::invalid_input,
        "a missing context cannot present"
    );
    require(
        veyra::acquire_vulkan_frame(presenter, kAcquireTimeoutNs, image) ==
            veyra::VulkanPresentResult::invalid_input,
        "acquire on a missing presenter is rejected"
    );
    require(
        veyra::present_vulkan_frame(presenter, 0, nullptr) ==
            veyra::VulkanPresentResult::invalid_input,
        "present on a missing presenter is rejected"
    );

    auto pixels = std::vector<veyra::SoftwarePixel>(kWidth * kHeight);

    require(
        veyra::read_vulkan_presenter_pixels(presenter, std::span<veyra::SoftwarePixel>(pixels)) ==
            veyra::VulkanPresentResult::invalid_input,
        "readback on a missing presenter is rejected"
    );
    require(
        veyra::resize_vulkan_presenter(presenter, kWidth, kHeight) ==
            veyra::VulkanPresentResult::invalid_input,
        "resize on a missing presenter is rejected"
    );
    require(
        veyra::recover_vulkan_presenter(presenter) == veyra::VulkanPresentResult::invalid_input,
        "recover on a missing presenter is rejected"
    );
    require(!veyra::vulkan_presenter_ready(presenter), "a missing presenter never reads ready");
    require(
        !veyra::vulkan_presenter_device_lost(presenter),
        "a missing presenter reads as not lost"
    );
    require(
        veyra::vulkan_presenter_generation(presenter) == 0,
        "a missing presenter reports generation zero"
    );

    veyra::destroy_vulkan_presenter(presenter);

    auto context = veyra::VulkanContext{};

    require(
        veyra::create_vulkan_context(context, kWidth, kHeight) == veyra::VulkanContextResult::ready,
        "vulkan context comes up"
    );
    require(
        veyra::create_vulkan_presenter(context, 0, kHeight, nullptr, presenter) ==
            veyra::VulkanPresentResult::invalid_input,
        "a zero-width presenter is rejected"
    );
    require(
        veyra::create_vulkan_presenter(context, kWidth, 0, nullptr, presenter) ==
            veyra::VulkanPresentResult::invalid_input,
        "a zero-height presenter is rejected"
    );

    veyra::destroy_vulkan_context(context);
}

void check_present_parity(
    veyra::VulkanContext& context,
    veyra::VulkanRectPipeline& pipeline,
    veyra::VulkanPresenter& presenter,
    std::span<const veyra::DisplayCommand> scene
) {
    auto offscreen = std::vector<veyra::SoftwarePixel>{};
    auto presented = std::vector<veyra::SoftwarePixel>{};

    render_offscreen(context, pipeline, scene, offscreen);
    present_scene(presenter, presented);
    require_pixels_equal(offscreen, presented, "acquire-present-readback");

    std::cout << "vulkan present pixels=" << presented.size()
              << " generation=" << veyra::vulkan_presenter_generation(presenter) << '\n';
}

void check_recreate_round_trip(
    veyra::VulkanContext& context,
    veyra::VulkanRectPipeline& pipeline,
    veyra::VulkanPresenter& presenter,
    std::span<const veyra::DisplayCommand> scene
) {
    auto before = std::vector<veyra::SoftwarePixel>{};
    auto after = std::vector<veyra::SoftwarePixel>{};

    render_offscreen(context, pipeline, scene, before);
    present_scene(presenter, before);

    veyra::destroy_vulkan_presenter(presenter);
    require(!veyra::vulkan_presenter_ready(presenter), "a torn-down presenter never reads ready");
    require(
        !veyra::vulkan_presenter_device_lost(presenter),
        "a torn-down presenter reads as not lost"
    );

    require(
        veyra::create_vulkan_presenter(context, kWidth, kHeight, nullptr, presenter) ==
            veyra::VulkanPresentResult::ready,
        "the presenter recreates headless"
    );
    require(veyra::vulkan_presenter_ready(presenter), "a recreated presenter reads ready");
    require(
        veyra::vulkan_presenter_generation(presenter) >= 1,
        "a recreated presenter carries a live generation"
    );

    render_offscreen(context, pipeline, scene, after);
    present_scene(presenter, after);
    require_pixels_equal(before, after, "destroy-create round-trip");
}

void check_resize(
    veyra::VulkanContext& context,
    veyra::VulkanRectPipeline& pipeline,
    veyra::VulkanPresenter& presenter
) {
    const auto before = veyra::vulkan_presenter_generation(presenter);

    require(
        veyra::resize_vulkan_presenter(presenter, 0, kSmallHeight) ==
            veyra::VulkanPresentResult::invalid_input,
        "a zero-width resize is rejected"
    );
    require(
        veyra::vulkan_presenter_generation(presenter) == before,
        "a rejected resize leaves the generation alone"
    );
    require(
        veyra::resize_vulkan_presenter(presenter, kSmallWidth, kSmallHeight) ==
            veyra::VulkanPresentResult::ready,
        "resize recreates the swapchain"
    );
    require(
        veyra::vulkan_presenter_generation(presenter) == before + 1,
        "a resize bumps the generation once"
    );
    require(
        context.width == kSmallWidth && context.height == kSmallHeight,
        "resize keeps the borrowed target in sync"
    );
    require(!veyra::vulkan_presenter_device_lost(presenter), "a resize is not device loss");

    auto scene = std::vector<veyra::DisplayCommand>{};
    auto offscreen = std::vector<veyra::SoftwarePixel>{};
    auto presented = std::vector<veyra::SoftwarePixel>{};

    build_scene(scene, kSmallWidth, kSmallHeight);
    render_offscreen(context, pipeline, scene, offscreen);
    present_scene(presenter, presented);
    require_pixels_equal(offscreen, presented, "resized swapchain");
}

void check_recover(
    veyra::VulkanContext& context,
    veyra::VulkanRectPipeline& pipeline,
    veyra::VulkanPresenter& presenter
) {
    const auto before = veyra::vulkan_presenter_generation(presenter);

    require(!veyra::vulkan_presenter_device_lost(presenter), "a healthy presenter is not lost");
    require(
        veyra::recover_vulkan_presenter(presenter) == veyra::VulkanPresentResult::ready,
        "recover rebuilds a healthy swapchain"
    );
    require(
        veyra::vulkan_presenter_generation(presenter) == before + 1,
        "a recover bumps the generation once"
    );

    auto scene = std::vector<veyra::DisplayCommand>{};
    auto offscreen = std::vector<veyra::SoftwarePixel>{};
    auto presented = std::vector<veyra::SoftwarePixel>{};

    build_scene(scene, context.width, context.height);
    render_offscreen(context, pipeline, scene, offscreen);
    present_scene(presenter, presented);
    require_pixels_equal(offscreen, presented, "recovered swapchain");
}

void check_bad_image_rejected(veyra::VulkanPresenter& presenter) {
    require(
        veyra::present_vulkan_frame(presenter, 1U << 28U, nullptr) ==
            veyra::VulkanPresentResult::invalid_input,
        "presenting an unknown image is rejected"
    );

    auto short_span = std::vector<veyra::SoftwarePixel>(1);

    require(
        veyra::read_vulkan_presenter_pixels(
            presenter,
            std::span<veyra::SoftwarePixel>(short_span)
        ) == veyra::VulkanPresentResult::invalid_input,
        "a short readback span is rejected"
    );
}

void check_steady_frames_allocate_nothing(veyra::VulkanPresenter& presenter) {
    auto pixels = std::vector<veyra::SoftwarePixel>(presenter.width * presenter.height);
    auto image = std::uint32_t{};

    require(
        veyra::acquire_vulkan_frame(presenter, kAcquireTimeoutNs, image) ==
            veyra::VulkanPresentResult::ready,
        "steady window opens clean"
    );
    require(
        veyra::present_vulkan_frame(presenter, image, nullptr) == veyra::VulkanPresentResult::ready,
        "steady present completes"
    );
    require(
        veyra::read_vulkan_presenter_pixels(presenter, std::span<veyra::SoftwarePixel>(pixels)) ==
            veyra::VulkanPresentResult::ready,
        "steady readback completes"
    );

    g_allocation_count.store(0, std::memory_order_relaxed);
    g_counting.store(true, std::memory_order_relaxed);

    for (auto repeat = std::size_t{}; repeat < 60; ++repeat) {
        auto round_tripped = true;

        if (veyra::acquire_vulkan_frame(presenter, kAcquireTimeoutNs, image) !=
            veyra::VulkanPresentResult::ready) {
            round_tripped = false;
        }

        if (round_tripped &&
            veyra::present_vulkan_frame(presenter, image, nullptr) !=
                veyra::VulkanPresentResult::ready) {
            round_tripped = false;
        }

        if (round_tripped &&
            veyra::read_vulkan_presenter_pixels(
                presenter,
                std::span<veyra::SoftwarePixel>(pixels)
            ) != veyra::VulkanPresentResult::ready) {
            round_tripped = false;
        }

        if (!round_tripped) {
            g_counting.store(false, std::memory_order_relaxed);

            throw std::runtime_error("steady present repeats stay complete");
        }
    }

    g_counting.store(false, std::memory_order_relaxed);

    std::cout << "vulkan present repeats=60 host_allocations="
              << g_allocation_count.load(std::memory_order_relaxed) << '\n';

    require(
        g_allocation_count.load(std::memory_order_relaxed) == 0,
        "present repeats allocate nothing"
    );
}

int run() {
    check_invalid_input();

    auto context = veyra::VulkanContext{};

    require(
        veyra::create_vulkan_context(context, kWidth, kHeight) == veyra::VulkanContextResult::ready,
        "vulkan context comes up"
    );

    auto pipeline = veyra::VulkanRectPipeline{};

    require(
        veyra::create_vulkan_rect_pipeline(context, pipeline) ==
            veyra::VulkanRenderResult::complete,
        "pipeline comes up"
    );

    auto presenter = veyra::VulkanPresenter{};

    require(
        veyra::create_vulkan_presenter(context, kWidth, kHeight, nullptr, presenter) ==
            veyra::VulkanPresentResult::ready,
        "the headless presenter comes up"
    );
    require(
        veyra::create_vulkan_presenter(context, kWidth, kHeight, nullptr, presenter) ==
            veyra::VulkanPresentResult::invalid_input,
        "creating over a live presenter is rejected"
    );
    require(veyra::vulkan_presenter_ready(presenter), "a live presenter reads ready");
    require(!veyra::vulkan_presenter_device_lost(presenter), "a fresh presenter is not lost");

    auto scene = std::vector<veyra::DisplayCommand>{};

    build_scene(scene, kWidth, kHeight);
    check_present_parity(context, pipeline, presenter, scene);
    check_recreate_round_trip(context, pipeline, presenter, scene);
    check_resize(context, pipeline, presenter);
    check_recover(context, pipeline, presenter);
    check_bad_image_rejected(presenter);
    check_steady_frames_allocate_nothing(presenter);

    veyra::destroy_vulkan_presenter(presenter);
    veyra::destroy_vulkan_rect_pipeline(context, pipeline);
    veyra::destroy_vulkan_context(context);

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
