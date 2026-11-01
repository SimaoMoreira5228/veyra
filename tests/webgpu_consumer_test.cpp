#include "veyra/render/gpu/webgpu/webgpu_consumer.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
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

struct RecordedDraw {
    veyra::WebGpuBatch batch;
    std::size_t command_count{};
    std::size_t first_command{};
};

class RecordingSink : public veyra::WebGpuSink {
  public:
    void begin_frame(std::uint32_t width, std::uint32_t height) noexcept override {
        this->width = width;
        this->height = height;
        ++begin_count;
    }

    void draw(
        const veyra::WebGpuBatch& batch,
        std::span<const veyra::DisplayCommand> commands
    ) noexcept override {
        if (draw_count < draws.size()) {
            draws[draw_count] = {batch, commands.size(), batch.start};
        }
        ++draw_count;
    }

    void end_frame() noexcept override {
        ++end_count;
    }

    std::array<RecordedDraw, 256> draws{};
    std::size_t begin_count{};
    std::size_t end_count{};
    std::size_t draw_count{};
    std::uint32_t width{};
    std::uint32_t height{};
};

std::vector<veyra::DisplayCommand> sample_commands() {
    auto commands = std::vector<veyra::DisplayCommand>{};
    commands.push_back(
        veyra::FillRect{
            .bounds = {.width = 40.0F, .height = 20.0F},
            .color = {.red = 10, .green = 20, .blue = 30},
        }
    );
    commands.push_back(
        veyra::PushClip{
            .bounds = {.width = 20.0F, .height = 10.0F},
            .clip_id = 7,
        }
    );
    commands.push_back(
        veyra::FillRoundedRect{
            .bounds = {.width = 16.0F, .height = 12.0F},
            .radius = 3.0F,
            .color = {.red = 40, .green = 50, .blue = 60},
        }
    );
    commands.push_back(
        veyra::DrawMask{
            .x = 1.0F,
            .y = 2.0F,
            .color = {.red = 255, .green = 255, .blue = 255, .alpha = 128},
        }
    );
    commands.push_back(
        veyra::DrawGlyph{
            .x = 3.0F,
            .y = 4.0F,
            .color = {.red = 200, .green = 200, .blue = 200},
        }
    );
    commands.push_back(veyra::PopClip{});
    commands.push_back(
        veyra::StrokeLine{
            .start = {.x = 0.0F, .y = 0.0F},
            .end = {.x = 8.0F, .y = 8.0F},
            .thickness = 1.0F,
            .color = {.red = 90, .green = 90, .blue = 90},
        }
    );
    commands.push_back(
        veyra::FillPath{
            .color = {.red = 12, .green = 34, .blue = 56},
        }
    );
    commands.push_back(
        veyra::DrawImage{
            .destination = {.width = 10.0F, .height = 10.0F},
            .sampling = veyra::SoftwareImageSampling::linear,
        }
    );
    return commands;
}

void check_plan_matches_batch_analysis() {
    const auto commands = sample_commands();
    auto runs = std::array<veyra::BatchRun, veyra::WebGpuFramePlan::kMaxBatches>{};
    auto clips = std::array<std::uint64_t, 32>{};
    auto run_count = std::size_t{};
    require(
        veyra::analyze_display_batches(commands, runs, clips, run_count) ==
            veyra::BatchAnalysisResult::complete,
        "the reference batch analysis completes"
    );

    auto sink = RecordingSink{};
    auto consumer = veyra::WebGpuConsumer(sink);
    auto plan = veyra::WebGpuFramePlan{};
    require(
        consumer.submit(commands, 320, 200, plan) == veyra::WebGpuPlanResult::complete,
        "the WebGPU plan completes"
    );

    require(plan.command_count == commands.size(), "the plan counts every command");
    require(plan.batch_count == run_count, "the plan keeps every reference batch");
    require(!plan.overflowed, "a fitting plan is not marked as overflowed");
    require(sink.begin_count == 1 && sink.end_count == 1, "one frame bracket is submitted");
    require(sink.width == 320 && sink.height == 200, "the frame size reaches the sink");
    require(sink.draw_count == run_count, "every batch reaches the sink");

    for (std::size_t index = 0; index < run_count; ++index) {
        const auto& run = runs[index];
        const auto& batch = plan.batches[index];
        require(
            batch.pipeline == veyra::webgpu_pipeline(run.key.primitive),
            "the pipeline maps from the primitive"
        );
        require(batch.resource == run.key.resource, "the resource maps");
        require(batch.sampling == run.key.sampling, "the sampling mode maps");
        require(batch.clip_id == run.key.clip_id, "the clip id maps");
        require(batch.clip_depth == run.key.clip_depth, "the clip depth maps");
        require(batch.start == run.start && batch.count == run.count, "the command span maps");
        require(batch.sequence == run.sequence, "the batch sequence maps");
        require(
            sink.draws[index].batch.sequence == batch.sequence &&
                sink.draws[index].first_command == run.start,
            "the sink receives batches in order"
        );
    }
}

void check_pipeline_mapping() {
    constexpr std::array<veyra::BatchPrimitive, 10> primitives{
        veyra::BatchPrimitive::fill_rect,
        veyra::BatchPrimitive::fill_rounded_rect,
        veyra::BatchPrimitive::stroke_rect,
        veyra::BatchPrimitive::stroke_rounded_rect,
        veyra::BatchPrimitive::stroke_line,
        veyra::BatchPrimitive::stroke_polyline,
        veyra::BatchPrimitive::draw_mask,
        veyra::BatchPrimitive::draw_glyph,
        veyra::BatchPrimitive::fill_path,
        veyra::BatchPrimitive::draw_image,
    };

    for (const auto primitive : primitives) {
        const auto pipeline = veyra::webgpu_pipeline(primitive);
        require(!veyra::webgpu_pipeline_name(pipeline).empty(), "every pipeline has a name");
    }
    require(
        veyra::webgpu_pipeline(veyra::BatchPrimitive::draw_image) ==
            veyra::WebGpuPipeline::draw_image,
        "image primitives select the image pipeline"
    );
}

void check_empty_frame() {
    auto sink = RecordingSink{};
    auto consumer = veyra::WebGpuConsumer(sink);
    auto plan = veyra::WebGpuFramePlan{};

    require(
        consumer.submit({}, 16, 16, plan) == veyra::WebGpuPlanResult::complete,
        "an empty frame completes"
    );
    require(plan.batch_count == 0 && plan.command_count == 0, "an empty frame has no work");
    require(
        sink.begin_count == 1 && sink.end_count == 1 && sink.draw_count == 0,
        "an empty frame keeps its bracket and submits no draw"
    );
}

void check_overflow_does_not_submit() {
    auto commands = std::vector<veyra::DisplayCommand>{};
    commands.reserve(900);
    for (std::uint64_t index = 0; index < 300; ++index) {
        commands.push_back(
            veyra::PushClip{
                .bounds = {.width = 1.0F, .height = 1.0F},
                .clip_id = index + 1,
            }
        );
        commands.push_back(
            veyra::FillRect{
                .bounds = {.width = 1.0F, .height = 1.0F},
                .color = {.red = 1, .green = 1, .blue = 1},
            }
        );
        commands.push_back(veyra::PopClip{});
    }

    auto sink = RecordingSink{};
    auto consumer = veyra::WebGpuConsumer(sink);
    auto plan = veyra::WebGpuFramePlan{};
    require(
        consumer.submit(commands, 64, 64, plan) == veyra::WebGpuPlanResult::insufficient_capacity,
        "a plan beyond the batch bound is rejected"
    );
    require(plan.overflowed, "the overflow is reported in the plan");
    require(plan.batch_count == 0, "an overflowing plan holds no partial batches");
    require(
        sink.begin_count == 0 && sink.draw_count == 0 && sink.end_count == 0,
        "an overflowing plan never reaches the sink"
    );
}

}

int main() {
    try {
        check_plan_matches_batch_analysis();
        check_pipeline_mapping();
        check_empty_frame();
        check_overflow_does_not_submit();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';

        return 1;
    }

    std::cout << "WebGPU consumer checks passed\n";

    return 0;
}
