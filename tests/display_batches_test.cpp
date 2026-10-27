#include "veyra/render/common/display_batches.hpp"

#include "veyra/render/cpu/software_renderer.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>

namespace {

void require(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

veyra::FillRect blue_rect(float x, float width) {
    return veyra::FillRect{
        .bounds = {.x = x, .width = width, .height = 1.0F},
        .color = {.red = 10, .green = 20, .blue = 200},
    };
}

bool is_state_command(const veyra::DisplayCommand& command) {
    return std::holds_alternative<veyra::PushClip>(command) ||
        std::holds_alternative<veyra::PopClip>(command) ||
        std::holds_alternative<veyra::PushTranslation>(command) ||
        std::holds_alternative<veyra::PopTranslation>(command);
}

veyra::BatchPrimitive expected_primitive(const veyra::DisplayCommand& command) {
    if (std::get_if<veyra::FillRect>(&command) != nullptr) {
        return veyra::BatchPrimitive::fill_rect;
    }

    if (std::get_if<veyra::DrawMask>(&command) != nullptr) {
        return veyra::BatchPrimitive::draw_mask;
    }

    if (std::get_if<veyra::DrawImage>(&command) != nullptr) {
        return veyra::BatchPrimitive::draw_image;
    }

    if (std::get_if<veyra::StrokeLine>(&command) != nullptr) {
        return veyra::BatchPrimitive::stroke_line;
    }

    throw std::runtime_error("test stream holds an unexpected draw kind");
}

const void* expected_resource(const veyra::DisplayCommand& command) {
    if (const auto* mask = std::get_if<veyra::DrawMask>(&command)) {
        return mask->mask;
    }

    if (const auto* image = std::get_if<veyra::DrawImage>(&command)) {
        return image->image;
    }

    return nullptr;
}

std::size_t covering_run(
    std::span<const veyra::BatchRun> runs,
    std::size_t run_count,
    std::size_t command_index
) {
    for (std::size_t run = 0; run < run_count; ++run) {
        if (command_index >= runs[run].start && command_index < runs[run].start + runs[run].count) {
            return run;
        }
    }

    throw std::runtime_error("draw command is covered by no batch run");
}

void require_exact_cover(
    std::span<const veyra::DisplayCommand> commands,
    std::span<const veyra::BatchRun> runs,
    std::size_t run_count
) {
    auto previous_end = std::size_t{0};

    for (std::size_t run = 0; run < run_count; ++run) {
        require(runs[run].count != 0, "every run covers at least one command");
        require(runs[run].sequence == run, "sequences count up within one analyze call");
        require(runs[run].start >= previous_end, "runs stay ordered and disjoint");
        require(
            runs[run].start + runs[run].count <= commands.size(),
            "runs stay inside the command list"
        );
        previous_end = runs[run].start + runs[run].count;
    }

    for (std::size_t index = 0; index < commands.size(); ++index) {
        auto covering = std::size_t{0};
        auto hits = std::size_t{0};

        for (std::size_t run = 0; run < run_count; ++run) {
            if (index >= runs[run].start && index < runs[run].start + runs[run].count) {
                covering = run;
                ++hits;
            }
        }

        if (is_state_command(commands[index])) {
            continue;
        }

        require(hits == 1, "every draw command lands in exactly one run");

        const auto& key = runs[covering].key;

        require(
            key.primitive == expected_primitive(commands[index]),
            "run key names the draw kind"
        );
        require(key.resource == expected_resource(commands[index]), "run key names the resource");
    }
}

void require_replay_matches(
    std::span<const veyra::DisplayCommand> commands,
    std::span<const veyra::BatchRun> runs,
    std::size_t run_count
) {
    auto clip_depth = std::size_t{0};
    auto clip_ids = std::array<std::uint64_t, 64>{};

    for (std::size_t index = 0; index < commands.size(); ++index) {
        const auto& command = commands[index];

        if (const auto* push = std::get_if<veyra::PushClip>(&command)) {
            require(clip_depth < clip_ids.size(), "replayed nesting fits the test stack");
            clip_ids[clip_depth] = push->clip_id;
            ++clip_depth;
            continue;
        }

        if (std::holds_alternative<veyra::PopClip>(command)) {
            if (clip_depth != 0) {
                --clip_depth;
            }

            continue;
        }

        if (is_state_command(command)) {
            continue;
        }

        const auto run = covering_run(runs, run_count, index);
        const auto expected_id = clip_depth == 0 ? std::uint64_t{0} : clip_ids[clip_depth - 1];

        require(runs[run].key.clip_depth == clip_depth, "run depth matches a replayed clip stack");
        require(runs[run].key.clip_id == expected_id, "run id matches the replayed clip scope");

        if (const auto* image = std::get_if<veyra::DrawImage>(&command)) {
            require(
                runs[run].key.sampling == image->sampling,
                "image runs carry the replayed sampling mode"
            );
        }
    }
}

void require_maximal_runs(
    std::span<const veyra::DisplayCommand> commands,
    std::span<const veyra::BatchRun> runs,
    std::size_t run_count
) {
    for (std::size_t run = 0; run + 1 < run_count; ++run) {
        const auto gap_begin = runs[run].start + runs[run].count;
        const auto gap_end = runs[run + 1].start;

        if (veyra::same_batch_key(runs[run].key, runs[run + 1].key)) {
            auto saw_state = false;

            for (std::size_t index = gap_begin; index < gap_end; ++index) {
                saw_state = saw_state || is_state_command(commands[index]);
            }

            require(saw_state, "equal keys split only across a state command");
        }
    }

    for (std::size_t run = 0; run < run_count; ++run) {
        for (std::size_t offset = 0; offset < runs[run].count; ++offset) {
            require(
                !is_state_command(commands[runs[run].start + offset]),
                "no state command hides inside a run"
            );
        }
    }
}

void check_empty_list() {
    const auto commands = std::array<veyra::DisplayCommand, 0>{};
    auto storage = std::array<veyra::BatchRun, 4>{};
    auto produced = std::size_t{0};
    auto scopes = std::array<std::uint64_t, 8>{};

    const auto result = veyra::analyze_display_batches(commands, storage, scopes, produced);

    require(result == veyra::BatchAnalysisResult::complete, "empty list analyzes");
    require(produced == 0, "empty list needs no runs");
}

void check_state_only_list() {
    const auto commands = std::array<veyra::DisplayCommand, 2>{
        veyra::PushClip{.bounds = {.width = 4.0F, .height = 4.0F}, .clip_id = 0},
        veyra::PopClip{},
    };
    auto storage = std::array<veyra::BatchRun, 4>{};
    auto produced = std::size_t{0};
    auto scopes = std::array<std::uint64_t, 8>{};

    const auto result = veyra::analyze_display_batches(commands, storage, scopes, produced);

    require(result == veyra::BatchAnalysisResult::complete, "state-only list analyzes");
    require(produced == 0, "state commands form no runs");
}

void check_all_same_merges() {
    const auto commands = std::array<veyra::DisplayCommand, 5>{
        blue_rect(0.0F, 1.0F),
        blue_rect(1.0F, 1.0F),
        blue_rect(2.0F, 1.0F),
        blue_rect(3.0F, 1.0F),
        blue_rect(4.0F, 1.0F),
    };
    auto storage = std::array<veyra::BatchRun, 5>{};
    auto produced = std::size_t{0};
    auto scopes = std::array<std::uint64_t, 8>{};

    const auto result = veyra::analyze_display_batches(commands, storage, scopes, produced);

    require(result == veyra::BatchAnalysisResult::complete, "uniform list analyzes");
    require(produced == 1, "uniform draws merge into one run");
    require(storage[0].start == 0 && storage[0].count == 5, "one run spans every draw");
}

void check_kind_splits() {
    const auto line = veyra::StrokeLine{
        .end = {.x = 2.0F, .y = 2.0F},
        .thickness = 1.0F,
    };
    const auto commands = std::array<veyra::DisplayCommand, 3>{
        blue_rect(0.0F, 1.0F),
        line,
        blue_rect(2.0F, 1.0F),
    };
    auto storage = std::array<veyra::BatchRun, 3>{};
    auto produced = std::size_t{0};
    auto scopes = std::array<std::uint64_t, 8>{};

    const auto result = veyra::analyze_display_batches(commands, storage, scopes, produced);

    require(result == veyra::BatchAnalysisResult::complete, "mixed kinds analyze");
    require(produced == 3, "each kind change starts a new run");
    require(
        storage[1].key.primitive == veyra::BatchPrimitive::stroke_line,
        "middle run names the line kind"
    );
    require(storage[0].start == 0 && storage[2].start == 2, "runs keep stream order");
}

void check_image_pointer_and_sampling() {
    const auto pixels_a = std::array<veyra::SoftwarePixel, 1>{};
    const auto pixels_b = std::array<veyra::SoftwarePixel, 1>{};
    const auto image_a = veyra::SoftwareImage{
        .width = 1,
        .height = 1,
        .stride = 1,
        .pixels = pixels_a,
        .generation = 0,
    };
    const auto image_b = veyra::SoftwareImage{
        .width = 1,
        .height = 1,
        .stride = 1,
        .pixels = pixels_b,
        .generation = 0,
    };
    const auto nearest = veyra::DrawImage{
        .image = &image_a,
        .sampling = veyra::SoftwareImageSampling::nearest,
    };
    const auto linear = veyra::DrawImage{
        .image = &image_a,
        .sampling = veyra::SoftwareImageSampling::linear,
    };
    const auto other = veyra::DrawImage{
        .image = &image_b,
        .sampling = veyra::SoftwareImageSampling::nearest,
    };

    const auto same_texture = std::array<veyra::DisplayCommand, 2>{nearest, nearest};
    const auto resampled = std::array<veyra::DisplayCommand, 2>{nearest, linear};
    const auto swapped = std::array<veyra::DisplayCommand, 2>{nearest, other};

    auto storage = std::array<veyra::BatchRun, 2>{};
    auto produced = std::size_t{0};
    auto scopes = std::array<std::uint64_t, 8>{};

    require(
        veyra::analyze_display_batches(same_texture, storage, scopes, produced) ==
            veyra::BatchAnalysisResult::complete,
        "repeated image analyzes"
    );
    require(produced == 1, "same image pointer draws merge");

    require(
        veyra::analyze_display_batches(resampled, storage, scopes, produced) ==
            veyra::BatchAnalysisResult::complete,
        "resampled image analyzes"
    );
    require(produced == 2, "sampling change starts a new run");

    require(
        veyra::analyze_display_batches(swapped, storage, scopes, produced) ==
            veyra::BatchAnalysisResult::complete,
        "swapped image analyzes"
    );
    require(produced == 2, "image pointer change starts a new run");
    require(
        !veyra::same_batch_key(storage[0].key, storage[1].key),
        "different images compare unequal"
    );
}

void check_clip_depth_splits() {
    const auto commands = std::array<veyra::DisplayCommand, 5>{
        blue_rect(0.0F, 1.0F),
        veyra::PushClip{.bounds = {.width = 4.0F, .height = 4.0F}, .clip_id = 0},
        blue_rect(1.0F, 1.0F),
        veyra::PopClip{},
        blue_rect(3.0F, 1.0F),
    };
    auto storage = std::array<veyra::BatchRun, 5>{};
    auto produced = std::size_t{0};
    auto scopes = std::array<std::uint64_t, 8>{};

    const auto result = veyra::analyze_display_batches(commands, storage, scopes, produced);

    require(result == veyra::BatchAnalysisResult::complete, "clipped list analyzes");
    require(produced == 3, "clip push and pop each end the run");
    require(storage[0].key.clip_depth == 0, "outer runs sit at depth zero");
    require(storage[0].key.clip_id == 0, "outer runs stay anonymous");
    require(storage[1].key.clip_depth == 1, "clipped run records depth one");
    require(storage[1].key.clip_id == 0, "zero id marks the anonymous scope");
    require(storage[1].start == 2 && storage[1].count == 1, "clipped run covers one draw");
    require(storage[2].key.clip_id == 0, "popped run stays anonymous");
}

void check_sibling_clips_split_at_equal_depth() {
    const auto commands = std::array<veyra::DisplayCommand, 6>{
        veyra::PushClip{.bounds = {.width = 2.0F, .height = 2.0F}, .clip_id = 0},
        blue_rect(0.0F, 1.0F),
        veyra::PopClip{},
        veyra::PushClip{.bounds = {.x = 2.0F, .width = 2.0F, .height = 2.0F}, .clip_id = 0},
        blue_rect(2.0F, 1.0F),
        veyra::PopClip{},
    };
    auto storage = std::array<veyra::BatchRun, 6>{};
    auto produced = std::size_t{0};
    auto scopes = std::array<std::uint64_t, 8>{};

    const auto result = veyra::analyze_display_batches(commands, storage, scopes, produced);

    require(result == veyra::BatchAnalysisResult::complete, "sibling clips analyze");
    require(produced == 2, "sibling clips never merge despite equal depth");
    require(
        storage[0].key.clip_depth == 1 && storage[1].key.clip_depth == 1,
        "both runs record depth one"
    );
    require(storage[0].start == 1 && storage[1].start == 4, "runs cover each sibling draw");
}

void check_translation_splits() {
    const auto commands = std::array<veyra::DisplayCommand, 5>{
        blue_rect(0.0F, 1.0F),
        veyra::PushTranslation{.offset = {.x = 4.0F}},
        blue_rect(0.0F, 1.0F),
        veyra::PopTranslation{},
        blue_rect(8.0F, 1.0F),
    };
    auto storage = std::array<veyra::BatchRun, 5>{};
    auto produced = std::size_t{0};
    auto scopes = std::array<std::uint64_t, 8>{};

    const auto result = veyra::analyze_display_batches(commands, storage, scopes, produced);

    require(result == veyra::BatchAnalysisResult::complete, "translated list analyzes");
    require(produced == 3, "translation push and pop each end the run");
    require(storage[0].start == 0 && storage[1].start == 2, "runs keep stream order");
    require(storage[2].start == 4 && storage[2].count == 1, "popped run covers one draw");
}

void check_balanced_translation_gap_stays_split() {
    const auto commands = std::array<veyra::DisplayCommand, 4>{
        blue_rect(0.0F, 1.0F),
        veyra::PushTranslation{.offset = {.x = 0.0F}},
        veyra::PopTranslation{},
        blue_rect(2.0F, 1.0F),
    };
    auto storage = std::array<veyra::BatchRun, 4>{};
    auto produced = std::size_t{0};
    auto scopes = std::array<std::uint64_t, 8>{};

    const auto result = veyra::analyze_display_batches(commands, storage, scopes, produced);

    require(result == veyra::BatchAnalysisResult::complete, "zero-offset list analyzes");
    require(produced == 2, "even a net-zero translation pair ends the run");
}

void check_insufficient_output_reports_needed() {
    const auto line = veyra::StrokeLine{
        .end = {.x = 1.0F, .y = 1.0F},
        .thickness = 1.0F,
    };
    const auto commands = std::array<veyra::DisplayCommand, 3>{
        blue_rect(0.0F, 1.0F),
        line,
        blue_rect(2.0F, 1.0F),
    };

    auto cramped = std::array<veyra::BatchRun, 1>{};
    auto produced = std::size_t{0};
    auto scopes = std::array<std::uint64_t, 8>{};

    const auto short_result = veyra::analyze_display_batches(commands, cramped, scopes, produced);

    require(
        short_result == veyra::BatchAnalysisResult::insufficient_output,
        "cramped output reports shortage"
    );
    require(produced == 3, "shortage reports the needed run count");

    auto roomy = std::array<veyra::BatchRun, 3>{};
    const auto full_result = veyra::analyze_display_batches(commands, roomy, scopes, produced);

    require(full_result == veyra::BatchAnalysisResult::complete, "exact output fits");
    require(produced == 3, "exact output reports three runs");

    const auto nested = std::array<veyra::DisplayCommand, 4>{
        veyra::PushClip{.bounds = {.width = 8.0F, .height = 8.0F}, .clip_id = 11},
        veyra::PushClip{.bounds = {.width = 4.0F, .height = 4.0F}, .clip_id = 22},
        blue_rect(0.0F, 1.0F),
        veyra::PopClip{},
    };
    auto shallow = std::array<std::uint64_t, 1>{};

    const auto shallow_result = veyra::analyze_display_batches(nested, roomy, shallow, produced);

    require(
        shallow_result == veyra::BatchAnalysisResult::insufficient_output,
        "a shallow scope scratch reports shortage"
    );
    require(produced == 1, "scope shortage still reports the needed run count");
}

void check_mixed_sequence_invariants() {
    const auto coverage_a = std::array<std::uint8_t, 2>{255, 255};
    const auto coverage_b = std::array<std::uint8_t, 2>{128, 128};
    const auto mask_a = veyra::SoftwareMask{
        .width = 2,
        .height = 1,
        .stride = 2,
        .coverage = coverage_a,
        .generation = 0,
    };
    const auto mask_b = veyra::SoftwareMask{
        .width = 2,
        .height = 1,
        .stride = 2,
        .coverage = coverage_b,
        .generation = 0,
    };
    const auto pixels = std::array<veyra::SoftwarePixel, 1>{};
    const auto image = veyra::SoftwareImage{
        .width = 1,
        .height = 1,
        .stride = 1,
        .pixels = pixels,
        .generation = 0,
    };
    const auto line = veyra::StrokeLine{
        .end = {.x = 3.0F, .y = 1.0F},
        .thickness = 2.0F,
    };

    const auto commands = std::array<veyra::DisplayCommand, 16>{
        blue_rect(0.0F, 8.0F),
        blue_rect(0.0F, 8.0F),
        veyra::DrawMask{.x = 0.0F, .mask = &mask_a},
        veyra::DrawMask{.x = 2.0F, .mask = &mask_a},
        veyra::DrawMask{.x = 4.0F, .mask = &mask_b},
        veyra::PushClip{.bounds = {.width = 8.0F, .height = 8.0F}, .clip_id = 6},
        blue_rect(1.0F, 2.0F),
        line,
        veyra::PopClip{},
        veyra::PushTranslation{.offset = {.x = 0.0F, .y = 6.0F}},
        blue_rect(0.0F, 8.0F),
        veyra::DrawImage{.image = &image},
        veyra::PopTranslation{},
        veyra::DrawImage{.image = &image},
        blue_rect(0.0F, 8.0F),
        blue_rect(0.0F, 8.0F),
    };

    auto storage = std::array<veyra::BatchRun, 16>{};
    auto produced = std::size_t{0};
    auto scopes = std::array<std::uint64_t, 8>{};

    const auto result = veyra::analyze_display_batches(commands, storage, scopes, produced);

    require(result == veyra::BatchAnalysisResult::complete, "mixed list analyzes");
    require(produced == 9, "mixed list folds into nine runs");

    const auto runs = std::span<const veyra::BatchRun>(storage.data(), produced);

    require_exact_cover(commands, runs, produced);
    require_replay_matches(commands, runs, produced);
    require_maximal_runs(commands, runs, produced);
}

void check_unbalanced_pops_saturate() {
    const auto commands = std::array<veyra::DisplayCommand, 9>{
        veyra::PopClip{},
        blue_rect(0.0F, 1.0F),
        veyra::PopClip{},
        blue_rect(1.0F, 1.0F),
        veyra::PushClip{.bounds = {.width = 4.0F, .height = 4.0F}, .clip_id = 5},
        blue_rect(2.0F, 1.0F),
        veyra::PopClip{},
        veyra::PopClip{},
        blue_rect(3.0F, 1.0F),
    };
    auto storage = std::array<veyra::BatchRun, 9>{};
    auto produced = std::size_t{0};
    auto scopes = std::array<std::uint64_t, 8>{};

    const auto result = veyra::analyze_display_batches(commands, storage, scopes, produced);

    require(result == veyra::BatchAnalysisResult::complete, "unbalanced pops analyze");
    require(produced == 4, "each draw between state commands forms a run");
    require(storage[0].start == 1 && storage[0].count == 1, "leading pop leaves the first draw");
    require(
        storage[0].key.clip_depth == 0 && storage[0].key.clip_id == 0 &&
            storage[1].key.clip_depth == 0 && storage[1].key.clip_id == 0,
        "pops at depth zero saturate and stay anonymous"
    );
    require(
        storage[2].key.clip_depth == 1 && storage[2].key.clip_id == 5 && storage[2].start == 5 &&
            storage[2].count == 1,
        "the pushed scope resolves depth and id over one draw"
    );
    require(
        storage[3].key.clip_depth == 0 && storage[3].key.clip_id == 0,
        "trailing pops saturate back to depth zero"
    );
}

void check_deep_nesting_resolves_ids() {
    const auto commands = std::array<veyra::DisplayCommand, 12>{
        veyra::PushClip{.bounds = {.width = 8.0F, .height = 8.0F}, .clip_id = 11},
        blue_rect(0.0F, 1.0F),
        veyra::PushClip{.bounds = {.width = 6.0F, .height = 6.0F}, .clip_id = 22},
        blue_rect(1.0F, 1.0F),
        veyra::PushClip{.bounds = {.width = 4.0F, .height = 4.0F}, .clip_id = 33},
        blue_rect(2.0F, 1.0F),
        blue_rect(3.0F, 1.0F),
        veyra::PopClip{},
        blue_rect(4.0F, 1.0F),
        veyra::PopClip{},
        veyra::PopClip{},
        blue_rect(5.0F, 1.0F),
    };
    auto storage = std::array<veyra::BatchRun, 12>{};
    auto produced = std::size_t{0};
    auto scopes = std::array<std::uint64_t, 8>{};

    const auto result = veyra::analyze_display_batches(commands, storage, scopes, produced);

    require(result == veyra::BatchAnalysisResult::complete, "deep nesting analyzes");
    require(produced == 5, "each nesting level forms its own run");
    require(
        storage[0].key.clip_depth == 1 && storage[0].key.clip_id == 11 &&
            storage[1].key.clip_depth == 2 && storage[1].key.clip_id == 22,
        "outer runs resolve outer scopes"
    );
    require(storage[2].start == 5 && storage[2].count == 2, "inner draws merge at depth three");
    require(
        storage[2].key.clip_depth == 3 && storage[2].key.clip_id == 33,
        "the inner run resolves the inner id"
    );
    require(
        storage[3].key.clip_depth == 2 && storage[3].key.clip_id == 22 &&
            storage[4].key.clip_depth == 0 && storage[4].key.clip_id == 0,
        "popping restores enclosing scopes down to anonymous"
    );
}

void check_sibling_clip_ids_split_keys() {
    const auto first = veyra::BatchKey{
        .primitive = veyra::BatchPrimitive::fill_rect,
        .clip_depth = 1,
        .clip_id = 7,
    };
    auto renamed = first;
    renamed.clip_id = 9;

    require(!veyra::same_batch_key(first, renamed), "different clip ids compare unequal");

    const auto commands = std::array<veyra::DisplayCommand, 6>{
        veyra::PushClip{.bounds = {.width = 2.0F, .height = 2.0F}, .clip_id = 7},
        blue_rect(0.0F, 1.0F),
        veyra::PopClip{},
        veyra::PushClip{.bounds = {.x = 2.0F, .width = 2.0F, .height = 2.0F}, .clip_id = 9},
        blue_rect(2.0F, 1.0F),
        veyra::PopClip{},
    };
    auto storage = std::array<veyra::BatchRun, 6>{};
    auto produced = std::size_t{0};
    auto scopes = std::array<std::uint64_t, 8>{};

    const auto result = veyra::analyze_display_batches(commands, storage, scopes, produced);

    require(
        result == veyra::BatchAnalysisResult::complete && produced == 2,
        "named sibling clips analyze into two runs"
    );
    require(
        storage[0].key.clip_id == 7 && storage[1].key.clip_id == 9,
        "siblings keep their own ids"
    );
    require(
        !veyra::same_batch_key(storage[0].key, storage[1].key),
        "sibling run keys compare unequal"
    );
}

}

int main() {
    try {
        check_empty_list();
        check_state_only_list();
        check_all_same_merges();
        check_kind_splits();
        check_image_pointer_and_sampling();
        check_clip_depth_splits();
        check_sibling_clips_split_at_equal_depth();
        check_translation_splits();
        check_balanced_translation_gap_stays_split();
        check_insufficient_output_reports_needed();
        check_mixed_sequence_invariants();
        check_unbalanced_pops_saturate();
        check_deep_nesting_resolves_ids();
        check_sibling_clip_ids_split_keys();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
