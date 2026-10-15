#include "veyra/behaviors/editable_text.hpp"
#include "veyra/engine.hpp"
#include "veyra/text/glyph_mask_cache.hpp"
#include "veyra/text/text_editor_buffer.hpp"
#ifdef VEYRA_AUDIT_TEXT
#include "veyra/text/shaped_run_cache.hpp"
#endif

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <new>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

std::atomic<std::size_t> g_allocation_count{0};
std::atomic<bool> g_counting{false};

// NOTE: every allocation form counts while armed; deletes never count.
void note_allocation() noexcept {
    if (g_counting.load(std::memory_order_relaxed)) {
        g_allocation_count.fetch_add(1, std::memory_order_relaxed);
    }
}

void* allocate_unaligned(std::size_t size) {
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

// NOTE: manual align-up stashes the raw pointer one word below the result.
void* allocate_aligned(std::size_t size, std::size_t alignment) {
    note_allocation();
    if (alignment < alignof(void*)) {
        alignment = alignof(void*);
    }
    if (size == 0) {
        size = 1;
    }
    const auto header = sizeof(void*);
    if (size > std::numeric_limits<std::size_t>::max() - alignment - header) {
        throw std::bad_alloc{};
    }
    void* raw = std::malloc(size + alignment + header);
    if (raw == nullptr) {
        throw std::bad_alloc{};
    }
    const auto base = reinterpret_cast<std::uintptr_t>(raw) + header;
    const auto aligned = (base + alignment - 1U) & ~(alignment - 1U);
    auto* slot = reinterpret_cast<void**>(aligned - header);
    *slot = raw;
    return reinterpret_cast<void*>(aligned);
}

void release_unaligned(void* ptr) noexcept {
    std::free(ptr);
}

void release_aligned(void* ptr) noexcept {
    if (ptr == nullptr) {
        return;
    }
    const auto slot = reinterpret_cast<void**>(static_cast<char*>(ptr) - sizeof(void*));
    std::free(*slot);
}

// NOTE: arm around the steady repeat only; setup and teardown stay outside.
class Measure {
  public:
    Measure() noexcept {
        g_allocation_count.store(0, std::memory_order_relaxed);
        g_counting.store(true, std::memory_order_relaxed);
    }

    ~Measure() noexcept {
        g_counting.store(false, std::memory_order_relaxed);
    }

    Measure(const Measure&) = delete;
    Measure& operator=(const Measure&) = delete;

    std::size_t count() const noexcept {
        return g_allocation_count.load(std::memory_order_relaxed);
    }
};

void require(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

std::uint64_t mix_hash(std::uint64_t hash, std::uint64_t value) noexcept {
    hash ^= value;
    hash *= 1099511628211ULL;
    return hash;
}

std::uint64_t hash_bytes(std::uint64_t hash, std::string_view bytes) noexcept {
    for (const auto byte : bytes) {
        hash ^= static_cast<std::uint64_t>(static_cast<unsigned char>(byte));
        hash *= 1099511628211ULL;
    }
    return hash;
}

constexpr std::size_t kAuditLines = 400;
constexpr std::size_t kAuditLineLength = 61;
constexpr std::size_t kAuditWrap = 32;
constexpr float kAuditAdvance = 8.0F;

std::string seed_audit_document() {
    constexpr std::string_view kFiller =
        "abcdefghijklmnopqrstuvwxyz0123456789 .:-_+~=,;!?/#%@*()[]{}";
    auto document = std::string{};
    document.reserve(kAuditLines * (kAuditLineLength + 1));
    for (std::size_t line = 0; line < kAuditLines; ++line) {
        auto head = std::string{"L"};
        const auto digits = std::to_string(line);
        while (head.size() + digits.size() < 5) {
            head.push_back('0');
        }
        head += digits;
        head.push_back(' ');
        document += head;
        for (std::size_t column = head.size(); column < kAuditLineLength; ++column) {
            document.push_back(kFiller[(line + column) % kFiller.size()]);
        }
        if (line + 1 < kAuditLines) {
            document.push_back('\n');
        }
    }
    return document;
}

struct AuditVisualMap {
    std::vector<std::vector<veyra::TextCaretStop>> stops;
    std::vector<veyra::TextVisualLine> lines;
};

AuditVisualMap build_audit_visual_map(const std::string& text) {
    auto map = AuditVisualMap{};
    auto hard_start = std::size_t{0};
    while (true) {
        auto hard_end = text.find('\n', hard_start);
        if (hard_end == std::string::npos) {
            hard_end = text.size();
        }
        auto offset = hard_start;
        while (true) {
            auto shard_length = hard_end - offset;
            if (shard_length > kAuditWrap) {
                shard_length = kAuditWrap;
            }
            auto shard = std::vector<veyra::TextCaretStop>{};
            for (std::size_t column = 0; column <= shard_length; ++column) {
                shard.push_back(
                    veyra::TextCaretStop{
                        .byte_offset = offset + column,
                        .x = static_cast<float>(column) * kAuditAdvance,
                    }
                );
            }
            map.stops.push_back(shard);
            offset += shard_length;
            if (offset == hard_end) {
                break;
            }
        }
        if (hard_end == text.size()) {
            break;
        }
        hard_start = hard_end + 1;
    }
    for (const auto& shard : map.stops) {
        map.lines.push_back(veyra::TextVisualLine{.caret_stops = shard});
    }
    return map;
}

veyra::GlyphMaskKey audit_glyph_key(std::uint32_t glyph_id) {
    return veyra::GlyphMaskKey{
        .face_id = 12,
        .variation_id = 34,
        .glyph_id = glyph_id,
        .pixel_size = 16,
        .scale_x_16_16 = 65'536,
        .scale_y_16_16 = 65'536,
        .mode = veyra::GlyphRasterMode::grayscale,
    };
}

std::uint64_t run_editor_steady_cycle(
    veyra::TextEditorBuffer& buffer,
    veyra::EditableTextController& field,
    const AuditVisualMap& map,
    veyra::GlyphMaskCache& glyphs,
    std::span<char> scratch
) {
    constexpr std::size_t kEntryLine = 30;
    constexpr std::size_t kColumn = 10;
    const auto entry_offset = map.stops[kEntryLine][kColumn].byte_offset;
    require(
        field.set_cursor(entry_offset).edit == veyra::TextEditResult::complete,
        "steady caret lands"
    );

    auto position = veyra::TextVisualPosition{.line_index = kEntryLine, .stop_index = kColumn};
    for (std::size_t step = 0; step < 2; ++step) {
        auto next = veyra::TextVisualPosition{};
        require(
            buffer.move_cursor_vertically(
                map.lines,
                position,
                veyra::TextVerticalDirection::down,
                static_cast<float>(kColumn) * kAuditAdvance,
                next
            ) == veyra::TextEditResult::complete,
            "steady caret steps down"
        );
        position = next;
    }
    for (std::size_t step = 0; step < 2; ++step) {
        auto next = veyra::TextVisualPosition{};
        require(
            buffer.move_cursor_vertically(
                map.lines,
                position,
                veyra::TextVerticalDirection::up,
                static_cast<float>(kColumn) * kAuditAdvance,
                next
            ) == veyra::TextEditResult::complete,
            "steady caret steps up"
        );
        position = next;
    }
    require(buffer.cursor() == entry_offset, "steady vertical roam returns");

    require(
        field.set_cursor(5000).edit == veyra::TextEditResult::complete,
        "steady selection anchor lands"
    );
    require(
        buffer.move_cursor(veyra::TextCursorDirection::forward) == veyra::TextEditResult::complete,
        "steady caret steps forward"
    );
    require(
        buffer.move_cursor(veyra::TextCursorDirection::backward, true) ==
            veyra::TextEditResult::complete,
        "steady caret extends backward"
    );
    const auto selected = buffer.selection();
    require(selected.start == 5000 && selected.length == 1, "steady selection covers one byte");

    auto selected_bytes = std::array<char, 32>{};
    auto selected_length = std::size_t{0};
    require(
        buffer.copy_selection(selected_bytes, selected_length) == veyra::TextEditResult::complete,
        "steady span copies out"
    );
    require(selected_length == 1, "steady span reports one byte");

    auto text_length = std::size_t{0};
    require(
        buffer.copy_text(scratch, text_length) == veyra::TextEditResult::complete,
        "steady text copies into scratch"
    );
    auto hash = hash_bytes(
        std::uint64_t{14695981039346656037ULL},
        std::string_view{scratch.data(), text_length}
    );
    hash = mix_hash(hash, buffer.cursor());
    hash = mix_hash(hash, selected.start);

    for (std::uint32_t glyph = 1; glyph <= 4; ++glyph) {
        const auto cached = glyphs.find(audit_glyph_key(glyph));
        require(cached.has_value(), "steady glyph lookup hits");
        hash = mix_hash(hash, cached->mask.width);
        hash = mix_hash(hash, cached->mask.height);
        hash = mix_hash(hash, cached->mask.coverage[0]);
    }
    return hash;
}

void check_editor_steady_repeat_allocates_nothing() {
    const auto document = seed_audit_document();
    auto storage = std::vector<char>(49152);
    auto history = std::vector<char>(32768);
    auto scratch = std::vector<char>(49152);
    auto buffer =
        veyra::TextEditorBuffer(std::span<char>{storage}, document, std::span<char>{history});
    require(
        buffer.initialization_result() == veyra::TextEditResult::complete,
        "audit document fits its storage"
    );
    auto field = veyra::EditableTextController(buffer);

    const auto map = build_audit_visual_map(document);

    auto glyph_slots = std::array<veyra::GlyphMaskCacheSlot, 8>{};
    auto glyph_storage = std::array<std::uint8_t, 128>{};
    auto glyphs = veyra::GlyphMaskCache(glyph_slots, glyph_storage, 4, 4);
    require(glyphs.valid(), "audit glyph cache starts valid");
    const auto source = std::array<std::uint8_t, 4>{10, 11, 20, 21};
    for (std::uint32_t glyph = 1; glyph <= 4; ++glyph) {
        const auto inserted = glyphs.insert(
            audit_glyph_key(glyph),
            veyra::GlyphMask{
                .mask = {
                    .width = 2,
                    .height = 2,
                    .stride = 2,
                    .coverage = source,
                },
            }
        );
        require(inserted == veyra::GlyphMaskCacheResult::inserted, "audit glyph is cached");
    }

    const auto baseline = run_editor_steady_cycle(buffer, field, map, glyphs, scratch);
    require(!buffer.can_undo(), "steady editor reads record no undo steps");

    auto allocations = std::size_t{0};
    {
        auto measure = Measure{};
        for (auto repeat = 0; repeat < 40; ++repeat) {
            const auto checksum = run_editor_steady_cycle(buffer, field, map, glyphs, scratch);
            require(checksum == baseline, "steady editor repeat keeps its text");
        }
        allocations = measure.count();
    }
    require(allocations == 0, "steady editor repeats allocate nothing");
}

#ifdef VEYRA_AUDIT_TEXT
void check_shaped_run_hits_allocate_nothing() {
    auto slots = std::array<veyra::ShapedRunCacheSlot, 16>{};
    auto text_storage = std::array<char, 512>{};
    auto glyph_storage = std::array<veyra::ShapedGlyph, 128>{};
    auto cache = veyra::ShapedRunCache(slots, text_storage, glyph_storage, 32, 8);
    require(cache.valid(), "audit shaped-run cache starts valid");

    constexpr std::string_view kTexts[] = {"hello", "world", "clip", "run"};
    constexpr std::size_t kCounts[] = {4, 5, 4, 3};
    auto key = veyra::ShapedRunCacheKey{
        .font_id = 3,
        .style_id = 5,
        .font_scale_x = 16,
        .font_scale_y = 16,
    };
    for (std::size_t run = 0; run < 4; ++run) {
        auto glyphs = std::array<veyra::ShapedGlyph, 8>{};
        for (std::size_t index = 0; index < kCounts[run]; ++index) {
            glyphs[index] = veyra::ShapedGlyph{
                .id = static_cast<std::uint32_t>(run * 16 + index + 1),
                .cluster = static_cast<std::uint32_t>(index),
                .x_advance = 8,
            };
        }
        require(
            cache.insert(
                key,
                kTexts[run],
                std::span<const veyra::ShapedGlyph>{glyphs.data(), kCounts[run]}
            ) == veyra::ShapedRunCacheResult::stored,
            "audit run is cached"
        );
    }

    auto baseline = std::uint64_t{14695981039346656037ULL};
    auto output = std::array<veyra::ShapedGlyph, 8>{};
    for (std::size_t run = 0; run < 4; ++run) {
        auto glyph_count = std::size_t{0};
        require(
            cache.find(key, kTexts[run], output, glyph_count) == veyra::ShapedRunCacheResult::hit,
            "audit run hits"
        );
        require(glyph_count == kCounts[run], "audit run keeps its glyph count");
        for (std::size_t index = 0; index < glyph_count; ++index) {
            baseline = mix_hash(baseline, output[index].id);
        }
    }

    auto allocations = std::size_t{0};
    {
        auto measure = Measure{};
        for (auto repeat = 0; repeat < 100; ++repeat) {
            auto hash = std::uint64_t{14695981039346656037ULL};
            for (std::size_t run = 0; run < 4; ++run) {
                auto glyph_count = std::size_t{0};
                const auto found = cache.find(key, kTexts[run], output, glyph_count);
                require(found == veyra::ShapedRunCacheResult::hit, "steady run hits");
                require(glyph_count == kCounts[run], "steady run keeps its glyph count");
                for (std::size_t index = 0; index < glyph_count; ++index) {
                    hash = mix_hash(hash, output[index].id);
                }
            }
            require(hash == baseline, "steady shaped-run repeat keeps its glyphs");
        }
        allocations = measure.count();
    }
    require(allocations == 0, "steady shaped-run repeats allocate nothing");
}
#endif

void check_engine_frames_allocate_nothing() {
    const auto engines = veyra::available_engines();
    require(!engines.empty(), "at least one engine is registered");

    for (const auto name : engines) {
        auto engine = veyra::create_engine(name);
        engine->reset(64);
        const auto changes = std::array<veyra::Update, 3>{
            veyra::Update{8, 5},
            veyra::Update{20, 7},
            veyra::Update{63, 9},
        };
        engine->apply(changes);

        auto out = std::array<veyra::Rect, 24>{};
        const auto warm = engine->frame(8, 24, out);
        require(warm.visited == 24 && warm.emitted == 24, "warm frame covers its window");
        const auto steady = engine->frame(8, 24, out);
        require(
            steady.visited == 24 && steady.invalidated == 0 && steady.emitted == 24,
            "second frame settles its paint"
        );
        auto baseline = std::uint64_t{14695981039346656037ULL};
        baseline = mix_hash(baseline, steady.visited);
        baseline = mix_hash(baseline, steady.invalidated);
        baseline = mix_hash(baseline, steady.emitted);
        for (const auto& rect : out) {
            baseline = mix_hash(baseline, rect.color);
        }
        baseline = mix_hash(baseline, engine->checksum());

        auto allocations = std::size_t{0};
        {
            auto measure = Measure{};
            for (auto repeat = 0; repeat < 200; ++repeat) {
                const auto stats = engine->frame(8, 24, out);
                require(
                    stats.visited == steady.visited && stats.invalidated == steady.invalidated &&
                        stats.emitted == steady.emitted,
                    "steady frame repeats its stats"
                );
                auto hash = std::uint64_t{14695981039346656037ULL};
                hash = mix_hash(hash, stats.visited);
                hash = mix_hash(hash, stats.invalidated);
                hash = mix_hash(hash, stats.emitted);
                for (const auto& rect : out) {
                    hash = mix_hash(hash, rect.color);
                }
                hash = mix_hash(hash, engine->checksum());
                require(hash == baseline, "steady frame repeats its paint");
            }
            allocations = measure.count();
        }
        require(allocations == 0, "steady engine frames allocate nothing");
    }
}

void check_harness_counts_every_form() {
    auto counted = std::size_t{0};
    {
        auto measure = Measure{};
        auto* scalar = new int(7);
        auto* items = new int[5];
        auto* aligned = new (std::align_val_t{32}) unsigned char[64];
        auto* tolerated = new (std::nothrow) int(9);
        require(scalar != nullptr && *scalar == 7, "scalar new works while armed");
        require(items != nullptr, "array new works while armed");
        require(
            aligned != nullptr && reinterpret_cast<std::uintptr_t>(aligned) % 32U == 0U,
            "aligned new works while armed"
        );
        require(tolerated != nullptr && *tolerated == 9, "nothrow new works while armed");
        counted = measure.count();
        delete scalar;
        delete[] items;
        ::operator delete[](aligned, std::align_val_t{32});
        ::operator delete(tolerated, std::nothrow);
    }
    require(counted == 4, "every new form counts once while armed");
}

}

void* operator new(std::size_t size) {
    return allocate_unaligned(size);
}

void* operator new[](std::size_t size) {
    return allocate_unaligned(size);
}

void operator delete(void* ptr) noexcept {
    release_unaligned(ptr);
}

void operator delete[](void* ptr) noexcept {
    release_unaligned(ptr);
}

void operator delete(void* ptr, std::size_t) noexcept {
    release_unaligned(ptr);
}

void operator delete[](void* ptr, std::size_t) noexcept {
    release_unaligned(ptr);
}

void* operator new(std::size_t size, std::align_val_t alignment) {
    return allocate_aligned(size, static_cast<std::size_t>(alignment));
}

void* operator new[](std::size_t size, std::align_val_t alignment) {
    return allocate_aligned(size, static_cast<std::size_t>(alignment));
}

void operator delete(void* ptr, std::align_val_t) noexcept {
    release_aligned(ptr);
}

void operator delete[](void* ptr, std::align_val_t) noexcept {
    release_aligned(ptr);
}

void operator delete(void* ptr, std::size_t, std::align_val_t) noexcept {
    release_aligned(ptr);
}

void operator delete[](void* ptr, std::size_t, std::align_val_t) noexcept {
    release_aligned(ptr);
}

void* operator new(std::size_t size, const std::nothrow_t&) noexcept {
    note_allocation();
    if (size == 0) {
        size = 1;
    }
    return std::malloc(size);
}

void* operator new[](std::size_t size, const std::nothrow_t&) noexcept {
    note_allocation();
    if (size == 0) {
        size = 1;
    }
    return std::malloc(size);
}

void operator delete(void* ptr, const std::nothrow_t&) noexcept {
    release_unaligned(ptr);
}

void operator delete[](void* ptr, const std::nothrow_t&) noexcept {
    release_unaligned(ptr);
}

int main() {
    try {
        check_editor_steady_repeat_allocates_nothing();
#ifdef VEYRA_AUDIT_TEXT
        check_shaped_run_hits_allocate_nothing();
#endif
        check_engine_frames_allocate_nothing();
        check_harness_counts_every_form();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
