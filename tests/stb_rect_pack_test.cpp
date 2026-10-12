#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

void veyra_stbrp_sort(
    void* base,
    std::size_t count,
    std::size_t size,
    int (*compare)(const void*, const void*)
);
void veyra_stbrp_check(int condition);

}

#define STBRP_SORT veyra_stbrp_sort
#define STBRP_ASSERT veyra_stbrp_check

#define STB_RECT_PACK_IMPLEMENTATION
#include "stb_rect_pack.h"

namespace {

int g_sort_calls = 0;

void veyra_stbrp_sort(
    void* base,
    std::size_t count,
    std::size_t size,
    int (*compare)(const void*, const void*)
) {
    ++g_sort_calls;

    std::qsort(base, count, size, compare);
}

void veyra_stbrp_check(int condition) {
    if (!condition) {
        throw std::runtime_error("stbrp assert fired");
    }
}

void require(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

const int kAtlasWidth = 256;
const int kAtlasHeight = 256;

std::vector<stbrp_rect> make_glyph_like_rects() {
    std::vector<stbrp_rect> rects;

    for (int i = 0; i < 48; ++i) {
        stbrp_rect rect{};

        rect.id = i;
        rect.w = static_cast<stbrp_coord>(6 + (i * 7) % 31);
        rect.h = static_cast<stbrp_coord>(10 + (i * 13) % 35);

        rects.push_back(rect);
    }

    return rects;
}

int pack_rects(std::vector<stbrp_rect>& rects) {
    std::vector<stbrp_node> nodes(static_cast<std::size_t>(kAtlasWidth));

    stbrp_context context{};

    stbrp_init_target(&context, kAtlasWidth, kAtlasHeight, nodes.data(), kAtlasWidth);

    return stbrp_pack_rects(&context, rects.data(), static_cast<int>(rects.size()));
}

bool overlaps(const stbrp_rect& a, const stbrp_rect& b) {
    if (a.x + a.w <= b.x || b.x + b.w <= a.x) {
        return false;
    }

    return a.y + a.h > b.y && b.y + b.h > a.y;
}

void check_packs_glyph_distribution() {
    std::vector<stbrp_rect> rects = make_glyph_like_rects();

    const int before = g_sort_calls;
    const int packed = pack_rects(rects);

    require(packed == 1, "a 48-glyph distribution must fit a 256x256 atlas");
    require(g_sort_calls > before, "the STBRP_SORT override must take, not the default qsort");

    for (const stbrp_rect& rect : rects) {
        require(rect.was_packed != 0, "every glyph rect must report packed");
        require(rect.x >= 0 && rect.y >= 0, "packed origins must stay non-negative");
        require(rect.x + rect.w <= kAtlasWidth, "packed rects must stay inside the atlas width");
        require(rect.y + rect.h <= kAtlasHeight, "packed rects must stay inside the atlas height");
    }

    for (std::size_t i = 0; i < rects.size(); ++i) {
        for (std::size_t j = i + 1; j < rects.size(); ++j) {
            require(!overlaps(rects[i], rects[j]), "packed glyph rects must never overlap");
        }
    }
}

void check_layout_is_deterministic() {
    std::vector<stbrp_rect> first = make_glyph_like_rects();
    std::vector<stbrp_rect> second = make_glyph_like_rects();

    require(pack_rects(first) == 1, "the first deterministic pack must succeed");
    require(pack_rects(second) == 1, "the second deterministic pack must succeed");
    require(first.size() == second.size(), "both packs must cover the same rect count");

    for (std::size_t i = 0; i < first.size(); ++i) {
        require(first[i].x == second[i].x, "same input must pack to the same x");
        require(first[i].y == second[i].y, "same input must pack to the same y");
        require(
            first[i].was_packed == second[i].was_packed,
            "same input must agree on packed flags"
        );
    }
}

void check_wide_short_rect_is_never_rotated() {
    std::vector<stbrp_node> nodes(static_cast<std::size_t>(kAtlasWidth));

    stbrp_context context{};

    stbrp_init_target(&context, kAtlasWidth, kAtlasHeight, nodes.data(), kAtlasWidth);

    stbrp_rect rect{};

    rect.id = 1;
    rect.w = 240;
    rect.h = 8;

    const int packed = stbrp_pack_rects(&context, &rect, 1);

    require(packed == 1, "a 240x8 strip must fit a 256-wide atlas without rotation");
    require(rect.was_packed != 0, "the wide strip must report packed");
    require(rect.w == 240 && rect.h == 8, "packing must not swap w and h, there is no rotation");
    require(rect.x + rect.w <= kAtlasWidth, "the unrotated strip must stay inside the atlas");
}

void check_oversize_rect_fails_cleanly() {
    std::vector<stbrp_node> nodes(static_cast<std::size_t>(kAtlasWidth));

    stbrp_context context{};

    stbrp_init_target(&context, kAtlasWidth, kAtlasHeight, nodes.data(), kAtlasWidth);

    stbrp_rect rect{};

    rect.id = 1;
    rect.w = kAtlasWidth + 44;
    rect.h = kAtlasHeight + 44;

    const int packed = stbrp_pack_rects(&context, &rect, 1);

    require(packed == 0, "a rect larger than the atlas must fail the pack");
    require(rect.was_packed == 0, "an oversize rect must report not packed");
}

}

int main() {
    try {
        check_packs_glyph_distribution();
        check_layout_is_deterministic();
        check_wide_short_rect_is_never_rotated();
        check_oversize_rect_fails_cleanly();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }

    return 0;
}
