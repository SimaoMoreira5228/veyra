#include "veyra/render/display_batches.hpp"
#include "veyra/render/display_command_hash.hpp"
#include "veyra/render/e3_scenes.hpp"
#include "veyra/render/software_renderer.hpp"

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

void require(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

constexpr std::array<veyra::E3Scene, 8> kAllScenes{
    veyra::E3Scene::shadowed_boxes_600,
    veyra::E3Scene::row_scroll_1000,
    veyra::E3Scene::virtual_rows_100k,
    veyra::E3Scene::dashboard_12,
    veyra::E3Scene::changing_text,
    veyra::E3Scene::chat_cjk_arabic,
    veyra::E3Scene::translucent_overlays,
    veyra::E3Scene::explorer_1m,
};

constexpr std::uint64_t kSeed = 0x9E3779B97F4A7C15ULL;

void check_names_round_trip() {
    for (const auto scene : kAllScenes) {
        const auto name = veyra::e3_scene_name(scene);

        require(!name.empty(), "every scene has a name");

        auto parsed = veyra::E3Scene::shadowed_boxes_600;

        require(veyra::e3_parse_scene(name, parsed), "every scene name parses");
        require(parsed == scene, "scene names round-trip");
    }

    auto parsed = veyra::E3Scene::shadowed_boxes_600;

    require(!veyra::e3_parse_scene("", parsed), "empty names are rejected");
    require(!veyra::e3_parse_scene("shadowed-boxes-601", parsed), "unknown names are rejected");
}

void check_complete_and_deterministic() {
    for (const auto scene : kAllScenes) {
        const auto needed = veyra::e3_max_commands(scene);

        require(needed > 0, "every scene has a nonzero command budget");

        auto first_commands = std::vector<veyra::DisplayCommand>(needed);
        auto second_commands = std::vector<veyra::DisplayCommand>(needed);
        auto first_scratch = veyra::E3Scratch{};
        auto second_scratch = veyra::E3Scratch{};

        const auto first = veyra::e3_build_scene(
            scene,
            kSeed,
            7,
            std::span<veyra::DisplayCommand>(first_commands),
            first_scratch
        );

        require(first.status == veyra::E3BuildStatus::complete, "scene builds complete");
        require(first.produced == needed, "scene fills its whole budget");

        const auto second = veyra::e3_build_scene(
            scene,
            kSeed,
            7,
            std::span<veyra::DisplayCommand>(second_commands),
            second_scratch
        );

        require(second.content_hash == first.content_hash, "same inputs rebuild the same scene");
        require(
            veyra::hash_display_commands(std::span<const veyra::DisplayCommand>(first_commands)) ==
                first.content_hash,
            "reported hash matches a fresh hash pass"
        );
    }
}

void check_frames_advance_scroll() {
    auto early_commands =
        std::vector<veyra::DisplayCommand>(veyra::e3_max_commands(veyra::E3Scene::row_scroll_1000));
    auto late_commands =
        std::vector<veyra::DisplayCommand>(veyra::e3_max_commands(veyra::E3Scene::row_scroll_1000));
    auto early_scratch = veyra::E3Scratch{};
    auto late_scratch = veyra::E3Scratch{};

    const auto early = veyra::e3_build_scene(
        veyra::E3Scene::row_scroll_1000,
        kSeed,
        0,
        std::span<veyra::DisplayCommand>(early_commands),
        early_scratch
    );
    const auto late = veyra::e3_build_scene(
        veyra::E3Scene::row_scroll_1000,
        kSeed,
        1,
        std::span<veyra::DisplayCommand>(late_commands),
        late_scratch
    );

    require(
        early.status == veyra::E3BuildStatus::complete &&
            late.status == veyra::E3BuildStatus::complete,
        "scroll frames build"
    );
    require(early.content_hash != late.content_hash, "advancing frames scrolls the window");
}

void check_truncation_and_invalid_scene() {
    auto scratch = veyra::E3Scratch{};
    auto empty = std::span<veyra::DisplayCommand>{};

    const auto empty_result =
        veyra::e3_build_scene(veyra::E3Scene::dashboard_12, kSeed, 0, empty, scratch);

    require(empty_result.status == veyra::E3BuildStatus::truncated, "empty output truncates");
    require(empty_result.produced == 0, "truncation produces nothing");
    require(empty_result.content_hash == 0, "truncation hashes nothing");

    const auto needed = veyra::e3_max_commands(veyra::E3Scene::dashboard_12);
    auto short_commands = std::vector<veyra::DisplayCommand>(needed - 1);

    const auto short_result = veyra::e3_build_scene(
        veyra::E3Scene::dashboard_12,
        kSeed,
        0,
        std::span<veyra::DisplayCommand>(short_commands),
        scratch
    );

    require(short_result.status == veyra::E3BuildStatus::truncated, "short output truncates");
    require(short_result.produced == 0, "short output produces nothing");

    auto roomy_commands =
        std::vector<veyra::DisplayCommand>(veyra::e3_max_commands(veyra::E3Scene::explorer_1m));

    const auto invalid_result = veyra::e3_build_scene(
        static_cast<veyra::E3Scene>(255),
        kSeed,
        0,
        std::span<veyra::DisplayCommand>(roomy_commands),
        scratch
    );

    require(
        invalid_result.status == veyra::E3BuildStatus::invalid_scene,
        "unknown scenes are rejected"
    );
    require(invalid_result.produced == 0, "rejected scenes produce nothing");
}

void check_software_renders_every_scene() {
    auto pixels = std::vector<veyra::SoftwarePixel>(veyra::kE3Width * veyra::kE3Height);
    auto clips = std::array<veyra::SoftwareClipRect, 1>{};

    const auto surface = veyra::SoftwareSurface{
        .width = veyra::kE3Width,
        .height = veyra::kE3Height,
        .stride = veyra::kE3Width,
        .pixels = pixels,
    };

    const auto workspace = veyra::SoftwareRenderWorkspace{
        .clips = clips,
    };

    for (const auto scene : kAllScenes) {
        auto commands = std::vector<veyra::DisplayCommand>(veyra::e3_max_commands(scene));
        auto scratch = veyra::E3Scratch{};

        const auto built = veyra::e3_build_scene(
            scene,
            kSeed,
            3,
            std::span<veyra::DisplayCommand>(commands),
            scratch
        );

        require(built.status == veyra::E3BuildStatus::complete, "rendered scene builds");

        std::fill(pixels.begin(), pixels.end(), veyra::SoftwarePixel{});

        const auto rendered = veyra::render_software(
            std::span<const veyra::DisplayCommand>(commands),
            surface,
            workspace
        );

        require(rendered == veyra::SoftwareRenderResult::complete, "every scene renders");
    }
}

void check_batches_cover_primitives() {
    auto commands = std::vector<veyra::DisplayCommand>(
        veyra::e3_max_commands(veyra::E3Scene::shadowed_boxes_600)
    );
    auto scratch = veyra::E3Scratch{};

    const auto built = veyra::e3_build_scene(
        veyra::E3Scene::shadowed_boxes_600,
        kSeed,
        0,
        std::span<veyra::DisplayCommand>(commands),
        scratch
    );

    require(built.status == veyra::E3BuildStatus::complete, "batched scene builds");

    auto runs = std::vector<veyra::BatchRun>(built.produced);
    auto scopes = std::array<std::uint64_t, 8>{};
    auto produced = std::size_t{};

    const auto analyzed = veyra::analyze_display_batches(
        std::span<const veyra::DisplayCommand>(commands),
        runs,
        scopes,
        produced
    );

    require(analyzed == veyra::BatchAnalysisResult::complete, "shadowed boxes analyze");
    require(produced >= 2, "shadows and cards form separate batches");

    auto overlay_commands = std::vector<veyra::DisplayCommand>(
        veyra::e3_max_commands(veyra::E3Scene::translucent_overlays)
    );
    auto overlay_scratch = veyra::E3Scratch{};

    const auto overlay_built = veyra::e3_build_scene(
        veyra::E3Scene::translucent_overlays,
        kSeed,
        0,
        std::span<veyra::DisplayCommand>(overlay_commands),
        overlay_scratch
    );

    auto overlay_runs = std::vector<veyra::BatchRun>(overlay_built.produced);
    auto overlay_produced = std::size_t{};

    require(
        veyra::analyze_display_batches(
            std::span<const veyra::DisplayCommand>(overlay_commands),
            overlay_runs,
            scopes,
            overlay_produced
        ) == veyra::BatchAnalysisResult::complete,
        "translucent overlays analyze"
    );
    require(overlay_produced >= 3, "opaque, translucent, and image runs stay separate");
}

void check_steady_rebuilds_allocate_nothing() {
    auto commands =
        std::vector<veyra::DisplayCommand>(veyra::e3_max_commands(veyra::E3Scene::explorer_1m));
    auto scratch = veyra::E3Scratch{};

    const auto warm = veyra::e3_build_scene(
        veyra::E3Scene::explorer_1m,
        kSeed,
        0,
        std::span<veyra::DisplayCommand>(commands),
        scratch
    );

    require(warm.status == veyra::E3BuildStatus::complete, "steady window opens clean");

    g_allocation_count.store(0, std::memory_order_relaxed);
    g_counting.store(true, std::memory_order_relaxed);

    for (auto repeat = std::size_t{0}; repeat < 60; ++repeat) {
        const auto result = veyra::e3_build_scene(
            veyra::E3Scene::explorer_1m,
            kSeed,
            repeat,
            std::span<veyra::DisplayCommand>(commands),
            scratch
        );

        if (result.status != veyra::E3BuildStatus::complete) {
            g_counting.store(false, std::memory_order_relaxed);

            throw std::runtime_error("steady rebuilds stay complete");
        }

        if (veyra::hash_display_commands(std::span<const veyra::DisplayCommand>(commands)) == 0) {
            g_counting.store(false, std::memory_order_relaxed);

            throw std::runtime_error("steady rebuilds hash to something");
        }
    }

    g_counting.store(false, std::memory_order_relaxed);

    std::cout << "e3 steady host_allocations=" << g_allocation_count.load(std::memory_order_relaxed)
              << '\n';

    require(g_allocation_count.load(std::memory_order_relaxed) == 0, "steady rebuilds allocate");
}

int run() {
    check_names_round_trip();
    check_complete_and_deterministic();
    check_frames_advance_scroll();
    check_truncation_and_invalid_scene();
    check_software_renders_every_scene();
    check_batches_cover_primitives();
    check_steady_rebuilds_allocate_nothing();

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
