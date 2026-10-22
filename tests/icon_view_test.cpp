#include "veyra/behaviors/icon_view.hpp"

#include <array>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

void require(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

constexpr std::array<veyra::SoftwarePixel, 4> kLightPixels{{
    {.red = 200, .green = 210, .blue = 220, .alpha = 255},
    {.red = 200, .green = 210, .blue = 220, .alpha = 255},
    {.red = 200, .green = 210, .blue = 220, .alpha = 255},
    {.red = 200, .green = 210, .blue = 220, .alpha = 255},
}};

constexpr std::array<veyra::SoftwarePixel, 4> kDarkPixels{{
    {.red = 20, .green = 24, .blue = 30, .alpha = 255},
    {.red = 20, .green = 24, .blue = 30, .alpha = 255},
    {.red = 20, .green = 24, .blue = 30, .alpha = 255},
    {.red = 20, .green = 24, .blue = 30, .alpha = 255},
}};

constexpr veyra::SoftwareImage kLightImage{
    .width = 2,
    .height = 2,
    .stride = 2,
    .pixels = kLightPixels,
};

constexpr veyra::SoftwareImage kDarkImage{
    .width = 2,
    .height = 2,
    .stride = 2,
    .pixels = kDarkPixels,
};

void check_starts_empty() {
    veyra::IconRegistry registry;

    require(registry.empty(), "a new registry holds no icons");
    require(registry.size() == 0, "a new registry reports zero size");
    require(registry.generation() == 0, "a new registry starts at generation zero");
    require(!registry.contains(1), "a new registry resolves no id");
    require(!registry.contains(0), "id zero never resolves");
    require(!registry.pending(1), "an unknown id is never pending");
    require(!registry.needs_image(1), "an unknown id needs no fetch");
    require(!registry.clear(), "clearing an empty registry reports nothing done");
    require(registry.generation() == 0, "clearing an empty registry bumps no generation");

    auto record = veyra::IconRecord{};
    require(!registry.record_for(1, record), "an unknown id reads back nothing");

    const veyra::SoftwareImage* image = nullptr;
    require(!registry.image_for(1, false, image), "an unknown id resolves no variant");

    auto semantics = veyra::SemanticNodeData{};
    require(
        !registry.write_icon_semantics(1, "saved", semantics),
        "semantics for an unknown id fail"
    );
    require(
        !registry.write_icon_semantics(1, "", semantics),
        "an empty label resolves no semantics"
    );
}

void check_register_rejects_zero_and_duplicates() {
    veyra::IconRegistry registry;

    require(!registry.register_icon(0, "zero", false), "id zero is rejected");
    require(registry.empty(), "a rejected register grows nothing");
    require(registry.generation() == 0, "a rejected register bumps no generation");

    require(registry.register_icon(7, "save", true), "a live id registers");
    require(!registry.empty() && registry.size() == 1, "the register holds one icon");
    require(registry.generation() == 1, "a register bumps the generation");
    require(registry.contains(7), "the id resolves after registering");
    require(!registry.register_icon(7, "save-again", false), "a duplicate id registers nothing");
    require(registry.size() == 1, "a duplicate register grows nothing");
    require(registry.generation() == 1, "a duplicate register bumps no generation");

    auto record = veyra::IconRecord{};
    require(registry.record_for(7, record), "the icon reads back");
    require(
        record.id == 7 && record.tintable && record.badge_count == 0,
        "the record keeps its id, tintable flag, and zero badge"
    );
    require(
        record.light == nullptr && record.dark == nullptr,
        "a fresh icon holds no pixels on either variant"
    );
    require(record.description_key == "save", "the record borrows the caller description key");
}

void check_pending_tracks_async_loading() {
    veyra::IconRegistry registry;

    require(registry.register_icon(3, "sync", false), "the icon registers");
    require(registry.pending(3), "a registered icon with no pixels is pending");
    require(registry.needs_image(3), "the loader polls the same pending state");

    auto semantics = veyra::SemanticNodeData{};
    require(
        registry.write_icon_semantics(3, "Sync now", semantics),
        "pending icons still describe themselves"
    );
    require(semantics.role == veyra::SemanticRole::image, "icon semantics read as an image role");
    require(semantics.label == "Sync now", "the borrowed label rides through untouched");
    require(
        veyra::has_state(semantics.states, veyra::SemanticState::busy),
        "a pending icon reports busy until its pixels land"
    );

    require(registry.set_image(3, &kLightImage, nullptr), "one variant landing applies");
    require(!registry.pending(3), "a single variant clears the pending state");
    require(!registry.needs_image(3), "a single variant stops the loader poll");
    require(
        !registry.set_image(3, &kLightImage, nullptr),
        "repeating the same variants reports no change"
    );
    require(registry.generation() == 2, "a no-op set bumps no generation");

    require(
        registry.write_icon_semantics(3, "Sync now", semantics),
        "ready icons describe themselves too"
    );
    require(
        !veyra::has_state(semantics.states, veyra::SemanticState::busy),
        "a ready icon drops the busy flag"
    );
}

void check_variant_pick_prefers_mode_then_falls_back() {
    veyra::IconRegistry registry;

    require(registry.register_icon(9, "theme", false), "the icon registers");
    require(registry.set_image(9, &kLightImage, &kDarkImage), "both baked variants land together");

    const veyra::SoftwareImage* image = nullptr;
    require(registry.image_for(9, false, image) && image == &kLightImage, "light ui picks light");
    require(registry.image_for(9, true, image) && image == &kDarkImage, "dark ui picks dark");

    require(registry.set_image(9, &kLightImage, nullptr), "dropping the dark variant applies");
    require(
        registry.image_for(9, true, image) && image == &kLightImage,
        "a missing dark variant falls back to light"
    );

    require(registry.set_image(9, nullptr, nullptr), "clearing both variants applies");
    require(
        registry.image_for(9, false, image) && image == nullptr,
        "no variants resolve null so the widget paints its placeholder"
    );
    require(registry.pending(9), "clearing both variants re-arms the pending state");
    require(!registry.set_image(11, &kLightImage, nullptr), "an unknown id takes no pixels");
}

void check_badge_and_tintable_mutations() {
    veyra::IconRegistry registry;

    require(!registry.set_badge(5, 3), "a badge on an unknown id fails");
    require(!registry.set_tintable(5, true), "a tint flag on an unknown id fails");
    require(registry.register_icon(5, "inbox", false), "the icon registers");

    const auto before = registry.generation();
    require(registry.set_badge(5, 12), "a badge count applies");
    require(registry.generation() == before + 1, "a badge change bumps the generation");
    require(!registry.set_badge(5, 12), "repeating the count reports no change");
    require(registry.generation() == before + 1, "a repeated badge bumps no generation");

    require(registry.set_tintable(5, true), "flipping tintable applies");
    require(!registry.set_tintable(5, true), "repeating the flag reports no change");

    auto record = veyra::IconRecord{};
    require(registry.record_for(5, record), "the mutated icon reads back");
    require(record.badge_count == 12 && record.tintable, "badge and tintable persist");
}

void check_overflow_refuses_and_keeps_everything() {
    veyra::IconRegistry registry;

    for (auto id = std::uint64_t{1}; id <= veyra::IconRegistry::kCapacity; ++id) {
        require(registry.register_icon(id, "slot", false), "the registry fills slot by slot");
    }

    require(registry.size() == veyra::IconRegistry::kCapacity, "the registry fills to capacity");
    require(registry.generation() == veyra::IconRegistry::kCapacity, "every fill bumps");

    const auto before = registry.generation();
    require(!registry.register_icon(1001, "burst", false), "a push past capacity is refused");
    require(
        registry.size() == veyra::IconRegistry::kCapacity,
        "a refused register keeps the bound"
    );
    require(registry.generation() == before, "a refused register bumps no generation");
    require(!registry.contains(1001), "the refused id resolves nowhere");
    require(registry.contains(1), "the head icon survives the refusal");
}

void check_unregister_and_clear() {
    veyra::IconRegistry registry;

    require(!registry.unregister(4), "removing an unknown id fails");
    require(registry.register_icon(4, "cut", false), "the first icon registers");
    require(registry.register_icon(6, "copy", true), "the second icon registers");

    const auto before = registry.generation();
    require(registry.unregister(4), "removing a live id applies");
    require(registry.generation() == before + 1, "a removal bumps the generation once");
    require(registry.size() == 1, "the removal shrinks the registry");
    require(!registry.contains(4), "the removed id resolves nowhere");
    require(!registry.pending(4), "a removed id is never pending");

    auto record = veyra::IconRecord{};
    require(registry.record_for(6, record) && record.id == 6, "the survivor keeps its id");

    require(registry.register_icon(4, "cut", false), "a removed id registers fresh");
    require(registry.contains(4) && registry.pending(4), "the fresh icon pends again");

    require(registry.clear(), "clearing a live registry reports work done");
    require(registry.empty(), "clear empties the registry");
    require(registry.generation() == before + 3, "remove, re-register, clear each bump once");
    require(!registry.clear(), "clearing an empty registry reports nothing done");
}

}

int main() {
    try {
        check_starts_empty();
        check_register_rejects_zero_and_duplicates();
        check_pending_tracks_async_loading();
        check_variant_pick_prefers_mode_then_falls_back();
        check_badge_and_tintable_mutations();
        check_overflow_refuses_and_keeps_everything();
        check_unregister_and_clear();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
