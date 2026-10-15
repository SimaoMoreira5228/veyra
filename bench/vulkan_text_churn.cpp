#include "veyra/render/software_renderer.hpp"
#include "veyra/render/vulkan_context.hpp"
#include "veyra/render/vulkan_rect_pipeline.hpp"
#include "veyra/text/glyph_run.hpp"
#include "veyra/text/shaped_run_cache.hpp"
#include "veyra/text/shaper.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;

constexpr std::size_t kBenchWidth = 1280;
constexpr std::size_t kBenchHeight = 720;
constexpr std::size_t kLineStep = 16;
constexpr std::size_t kMaxLineBytes = 128;
constexpr std::size_t kMaxLineGlyphs = 64;
constexpr std::size_t kAtlasWidth = 512;
constexpr std::size_t kAtlasHeight = 512;
constexpr std::size_t kAtlasSlots = 2048;
constexpr std::size_t kCacheSlots = 64;
constexpr std::size_t kCacheTextBytes = 128;
constexpr std::size_t kCacheGlyphs = 64;
constexpr std::uint64_t kAtlasFace = 7;
constexpr std::uint32_t kAtlasPixelSize = 16;

struct Options {
    std::size_t lines{40};
    std::size_t fresh_per_frame{2};
    std::size_t frames{200};
    std::size_t warmup{20};
};

std::size_t parse_size(const char* value) {
    const std::string_view text(value);
    std::size_t parsed = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), parsed);
    if (error != std::errc{} || end != text.data() + text.size()) {
        throw std::invalid_argument("expected a non-negative integer: " + std::string(text));
    }
    return parsed;
}

Options parse_options(int argc, char** argv) {
    Options options;
    for (int index = 1; index < argc; ++index) {
        const std::string_view argument(argv[index]);
        if (index + 1 >= argc) {
            throw std::invalid_argument("missing value for " + std::string(argument));
        }
        const auto value = argv[++index];
        if (argument == "--lines") {
            options.lines = parse_size(value);
        } else if (argument == "--fresh-per-frame") {
            options.fresh_per_frame = parse_size(value);
        } else if (argument == "--frames") {
            options.frames = parse_size(value);
        } else if (argument == "--warmup") {
            options.warmup = parse_size(value);
        } else {
            throw std::invalid_argument("unknown option: " + std::string(argument));
        }
    }
    if (options.lines == 0 || options.lines > 44 || options.fresh_per_frame == 0 ||
        options.fresh_per_frame > options.lines || options.frames == 0 || options.frames > 100000 ||
        options.warmup > 100000) {
        throw std::invalid_argument("counts must be positive and fit the 720p window");
    }
    return options;
}

double percentile(const std::vector<double>& sorted_times, std::size_t percent) {
    const auto last = sorted_times.size() - 1;
    const auto index = last / 100 * percent + last % 100 * percent / 100;
    return sorted_times[index];
}

void require(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

hb_bool_t map_codepoint(hb_font_t*, void*, hb_codepoint_t codepoint, hb_codepoint_t* glyph, void*) {
    *glyph = codepoint;
    return true;
}

class SyntheticFont {
  public:
    SyntheticFont() :
        font_(hb_font_create(hb_face_get_empty()), hb_font_destroy),
        functions_(hb_font_funcs_create(), hb_font_funcs_destroy) {
        hb_font_funcs_set_nominal_glyph_func(functions_.get(), map_codepoint, nullptr, nullptr);
        hb_font_funcs_make_immutable(functions_.get());
        hb_font_set_funcs(font_.get(), functions_.get(), nullptr, nullptr);
        hb_font_set_scale(font_.get(), 1000, 1000);
    }

    hb_font_t* get() const noexcept {
        return font_.get();
    }

  private:
    std::unique_ptr<hb_font_t, decltype(&hb_font_destroy)> font_;
    std::unique_ptr<hb_font_funcs_t, decltype(&hb_font_funcs_destroy)> functions_;
};

std::size_t stream_line_text(std::uint64_t stream_line, std::span<char> output) {
    const auto length = static_cast<std::size_t>(24 + (stream_line * 13) % 17);
    auto cursor = std::size_t{};

    for (std::size_t position = 0; position < length; ++position) {
        const auto variant =
            static_cast<std::uint32_t>(stream_line * 31 + position * 7 + stream_line);
        if (variant % 6 == 0) {
            const auto codepoint = 0x4E00U + (variant % 2096U);
            output[cursor++] = static_cast<char>(0xE0U | (codepoint >> 12U));
            output[cursor++] = static_cast<char>(0x80U | ((codepoint >> 6U) & 0x3FU));
            output[cursor++] = static_cast<char>(0x80U | (codepoint & 0x3FU));
        } else {
            output[cursor++] = static_cast<char>(0x20U + (variant % 95U));
        }
    }

    return cursor;
}

std::uint8_t churn_byte(std::uint32_t id, std::size_t row, std::size_t column) noexcept {
    return static_cast<std::uint8_t>((id * 31 + row * 13 + column * 7) % 251);
}

struct VariantResult {
    std::string_view name;
    std::vector<double> cpu_ns;
    std::vector<double> gpu_ns;
    std::size_t draws{};
    std::size_t submitted{};
    std::size_t evictions{};
    std::size_t dropped{};
    std::size_t uploads{};
};

struct ChurnWorkspaces {
    std::vector<veyra::SoftwarePixel> pixels;
    std::vector<veyra::DisplayCommand> commands;
    std::vector<veyra::GlyphAtlasSlot> slots;
    std::vector<std::uint8_t> coverage;
    std::vector<stbrp_node> nodes;
    std::vector<veyra::ShapedRunCacheSlot> cache_slots;
    std::vector<char> cache_text;
    std::vector<veyra::ShapedGlyph> cache_glyphs;
    std::vector<veyra::ShapedGlyph> line_glyphs;
    std::vector<std::size_t> line_counts;
    std::array<veyra::ShapedGlyph, kMaxLineGlyphs> shaped{};
    std::array<char, kMaxLineBytes> line{};
    std::array<std::uint8_t, 192> mask_pixels{};
};

veyra::GlyphMaskKey glyph_key(std::uint32_t id) noexcept {
    return veyra::GlyphMaskKey{
        .face_id = kAtlasFace,
        .glyph_id = id,
        .pixel_size = kAtlasPixelSize,
    };
}

void ensure_glyph(
    veyra::GlyphAtlas& atlas,
    std::uint32_t id,
    std::span<std::uint8_t> scratch,
    std::size_t& evictions
) {
    const auto key = glyph_key(id);

    if (atlas.find(key).has_value()) {
        return;
    }

    const auto width = static_cast<std::size_t>(8 + (id % 5));
    const auto height = std::size_t{12};

    for (auto row = std::size_t{}; row < height; ++row) {
        for (auto column = std::size_t{}; column < width; ++column) {
            scratch[row * width + column] = churn_byte(id, row, column);
        }
    }

    const auto mask = veyra::GlyphMask{
        .mask =
            {
                .width = width,
                .height = height,
                .stride = width,
                .coverage = scratch.first(height * width),
                .generation = 0,
            },
        .bearing_x = 0,
        .bearing_y = 10,
    };

    const auto inserted = atlas.insert(key, mask);

    require(
        inserted == veyra::GlyphAtlasResult::inserted ||
            inserted == veyra::GlyphAtlasResult::inserted_after_eviction,
        "churn glyphs pack"
    );

    if (inserted == veyra::GlyphAtlasResult::inserted_after_eviction) {
        ++evictions;
    }
}

std::size_t build_frame(
    std::uint64_t frame,
    const Options& options,
    SyntheticFont& font,
    veyra::TextShaper& shaper,
    veyra::ShapedRunCache& run_cache,
    veyra::GlyphAtlas& atlas,
    const veyra::GlyphMaskKey& base_key,
    ChurnWorkspaces& work,
    std::size_t& evictions,
    std::size_t& dropped
) {
    auto count = std::size_t{1};

    work.commands[0] = veyra::FillRect{
        .bounds =
            {.width = static_cast<float>(kBenchWidth), .height = static_cast<float>(kBenchHeight)},
        .color = {.red = 240, .green = 240, .blue = 244, .alpha = 255},
    };

    const auto script = veyra::shaped_run_script_tag("Latn");
    const auto language = veyra::shaped_run_language_id("en");

    for (auto line = std::size_t{}; line < options.lines; ++line) {
        const auto stream_line = frame * options.fresh_per_frame + line;
        const auto bytes = stream_line_text(stream_line, work.line);
        const auto text = std::string_view{work.line.data(), bytes};
        const veyra::ShapedRunCacheKey cache_key{
            .font_id = kAtlasFace,
            .font_scale_x = 1000,
            .font_scale_y = 1000,
            .direction = veyra::TextDirection::left_to_right,
            .script_tag = script,
            .language_id = language,
        };

        auto glyph_count = std::size_t{};
        const auto hit =
            run_cache
                .find(cache_key, text, std::span<veyra::ShapedGlyph>(work.shaped), glyph_count);

        require(
            hit == veyra::ShapedRunCacheResult::hit || hit == veyra::ShapedRunCacheResult::miss,
            "run cache answers"
        );

        if (hit == veyra::ShapedRunCacheResult::miss) {
            require(
                shaper.shape_utf8(
                    font.get(),
                    text,
                    {.direction = veyra::TextDirection::left_to_right, .script = "Latn"},
                    std::span<veyra::ShapedGlyph>(work.shaped),
                    glyph_count
                ) == veyra::ShapeResult::complete,
                "fresh lines shape"
            );

            for (auto index = std::size_t{}; index < glyph_count; ++index) {
                const auto id = work.shaped[index].id;
                work.shaped[index].x_advance = 500 + static_cast<std::int32_t>(id % 5) * 100;
            }

            require(
                run_cache.insert(
                    cache_key,
                    text,
                    std::span<const veyra::ShapedGlyph>(work.shaped).first(glyph_count)
                ) == veyra::ShapedRunCacheResult::stored,
                "fresh lines store"
            );
        }

        require(glyph_count <= kMaxLineGlyphs, "shaped lines fit the scratch");

        auto* stored = work.line_glyphs.data() + line * kMaxLineGlyphs;
        for (auto index = std::size_t{}; index < glyph_count; ++index) {
            stored[index] = work.shaped[index];
        }
        work.line_counts[line] = glyph_count;
    }

    for (auto line = std::size_t{}; line < options.lines; ++line) {
        const auto* stored = work.line_glyphs.data() + line * kMaxLineGlyphs;
        for (auto index = std::size_t{}; index < work.line_counts[line]; ++index) {
            ensure_glyph(
                atlas,
                stored[index].id,
                std::span<std::uint8_t>(work.mask_pixels),
                evictions
            );
        }
    }

    for (auto line = std::size_t{}; line < options.lines; ++line) {
        const auto* stored = work.line_glyphs.data() + line * kMaxLineGlyphs;
        auto resident = true;

        for (auto index = std::size_t{}; index < work.line_counts[line]; ++index) {
            if (!atlas.find(glyph_key(stored[index].id)).has_value()) {
                resident = false;
                break;
            }
        }

        if (!resident) {
            dropped += work.line_counts[line];
            continue;
        }

        auto line_commands = std::size_t{};
        require(
            veyra::build_glyph_atlas_draws(
                std::span<const veyra::ShapedGlyph>(
                    work.line_glyphs.data() + line * kMaxLineGlyphs,
                    kMaxLineGlyphs
                )
                    .first(work.line_counts[line]),
                atlas,
                base_key,
                {.x = 8.0F, .y = static_cast<float>(line * kLineStep + 12)},
                62.5,
                62.5,
                {.red = 20, .green = 20, .blue = 24, .alpha = 255},
                std::span<veyra::DisplayCommand>(work.commands).subspan(count),
                line_commands
            ) == veyra::GlyphRunResult::complete,
            "resident lines lower to atlas draws"
        );

        count += line_commands;
    }

    return count;
}

void run_variant(
    veyra::VulkanContext& context,
    veyra::VulkanRectPipeline& pipeline,
    const Options& options,
    VariantResult& result
) {
    auto work = ChurnWorkspaces{
        .pixels = std::vector<veyra::SoftwarePixel>(kBenchWidth * kBenchHeight),
        .commands = std::vector<veyra::DisplayCommand>(2048),
        .slots = std::vector<veyra::GlyphAtlasSlot>(kAtlasSlots),
        .coverage = std::vector<std::uint8_t>(kAtlasWidth * kAtlasHeight),
        .nodes = std::vector<stbrp_node>(kAtlasWidth),
        .cache_slots = std::vector<veyra::ShapedRunCacheSlot>(kCacheSlots),
        .cache_text = std::vector<char>(kCacheSlots * kCacheTextBytes),
        .cache_glyphs = std::vector<veyra::ShapedGlyph>(kCacheSlots * kCacheGlyphs),
        .line_glyphs = std::vector<veyra::ShapedGlyph>(options.lines * kMaxLineGlyphs),
        .line_counts = std::vector<std::size_t>(options.lines),
    };

    auto font = SyntheticFont{};
    auto shaper = veyra::TextShaper(256);
    auto run_cache = veyra::ShapedRunCache(
        std::span<veyra::ShapedRunCacheSlot>(work.cache_slots),
        std::span<char>(work.cache_text),
        std::span<veyra::ShapedGlyph>(work.cache_glyphs),
        kCacheTextBytes,
        kCacheGlyphs
    );

    require(run_cache.valid(), "run cache validates");

    auto atlas = veyra::GlyphAtlas(
        std::span<veyra::GlyphAtlasSlot>(work.slots),
        std::span<std::uint8_t>(work.coverage),
        std::span<stbrp_node>(work.nodes),
        kAtlasWidth,
        kAtlasHeight
    );

    require(atlas.valid(), "churn atlas validates");

    const veyra::GlyphMaskKey base_key{.face_id = kAtlasFace, .pixel_size = kAtlasPixelSize};

    result.cpu_ns.reserve(options.frames);
    result.gpu_ns.reserve(options.frames);

    for (std::size_t frame = 0; frame < options.warmup + options.frames; ++frame) {
        const auto measuring = frame >= options.warmup;
        auto stats = veyra::VulkanRectStats{};
        auto evictions = std::size_t{};
        auto dropped = std::size_t{};
        const auto render_begin = Clock::now();
        const auto count = build_frame(
            frame,
            options,
            font,
            shaper,
            run_cache,
            atlas,
            base_key,
            work,
            evictions,
            dropped
        );
        const auto rendered = veyra::render_vulkan_rects(
            context,
            pipeline,
            std::span<const veyra::DisplayCommand>(work.commands).first(count),
            std::span<veyra::SoftwarePixel>(work.pixels),
            stats
        );
        const auto render_end = Clock::now();

        require(rendered == veyra::VulkanRenderResult::complete, "churn frame completes");

        if (measuring) {
            result.cpu_ns.push_back(
                std::chrono::duration<double, std::nano>(render_end - render_begin).count()
            );
            result.gpu_ns.push_back(static_cast<double>(stats.gpu_ns));
            result.draws = stats.draws;
            result.submitted = stats.submitted;
            result.uploads += stats.uploads;
            result.evictions += evictions;
            result.dropped += dropped;
        }
    }

    std::sort(result.cpu_ns.begin(), result.cpu_ns.end());
    std::sort(result.gpu_ns.begin(), result.gpu_ns.end());
}

int run(const Options& options) {
    auto context = veyra::VulkanContext{};
    require(
        veyra::create_vulkan_context(context, kBenchWidth, kBenchHeight) ==
            veyra::VulkanContextResult::ready,
        "vulkan context comes up for churn"
    );

    auto specialized = veyra::VulkanRectPipeline{};
    require(
        veyra::create_vulkan_rect_pipeline(context, specialized) ==
            veyra::VulkanRenderResult::complete,
        "specialized pipeline comes up for churn"
    );

    auto unified = veyra::VulkanRectPipeline{};
    require(
        veyra::create_vulkan_unified_pipeline(context, unified) ==
            veyra::VulkanRenderResult::complete,
        "unified pipeline comes up for churn"
    );

    auto specialized_result = VariantResult{.name = "specialized"};
    auto unified_result = VariantResult{.name = "unified"};

    run_variant(context, specialized, options, specialized_result);
    run_variant(context, unified, options, unified_result);

    veyra::destroy_vulkan_rect_pipeline(context, unified);
    veyra::destroy_vulkan_rect_pipeline(context, specialized);
    veyra::destroy_vulkan_context(context);

    std::cout << "scene,platform,compiler,build_config,variant,lines,fresh_per_frame,"
                 "warmup_frames,measured_frames,cpu_p50_ns,cpu_p95_ns,cpu_p99_ns,gpu_p50_ns,"
                 "gpu_p95_ns,gpu_p99_ns,draws,submitted,uploads,evictions,dropped\n";

    for (const auto* result : {&specialized_result, &unified_result}) {
        std::cout << "text-churn," << VEYRA_BUILD_PLATFORM << ',' << VEYRA_BUILD_COMPILER << ','
                  << VEYRA_BUILD_CONFIG << ',' << result->name << ',' << options.lines << ','
                  << options.fresh_per_frame << ',' << options.warmup << ',' << options.frames
                  << ',' << percentile(result->cpu_ns, 50) << ',' << percentile(result->cpu_ns, 95)
                  << ',' << percentile(result->cpu_ns, 99) << ',' << percentile(result->gpu_ns, 50)
                  << ',' << percentile(result->gpu_ns, 95) << ',' << percentile(result->gpu_ns, 99)
                  << ',' << result->draws << ',' << result->submitted << ',' << result->uploads
                  << ',' << result->evictions << ',' << result->dropped << '\n';
    }

    return 0;
}

}

int main(int argc, char** argv) {
    try {
        return run(parse_options(argc, argv));
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
