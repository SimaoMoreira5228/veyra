#include "veyra/platform/sdl3/sdl3_gpu_present.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_gpu.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>

namespace veyra {
namespace {

constexpr std::size_t kBytesPerPixel = 4;

constexpr std::uint32_t kShaderFormats =
    SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_DXIL | SDL_GPU_SHADERFORMAT_MSL;

static_assert(sizeof(SoftwarePixel) == kBytesPerPixel);
static_assert(offsetof(SoftwarePixel, red) == 0);
static_assert(offsetof(SoftwarePixel, green) == 1);
static_assert(offsetof(SoftwarePixel, blue) == 2);
static_assert(offsetof(SoftwarePixel, alpha) == 3);

SDL_GPUDevice* as_device(void* device) noexcept {
    return static_cast<SDL_GPUDevice*>(device);
}

SDL_Window* as_window(void* window) noexcept {
    return static_cast<SDL_Window*>(window);
}

SDL_GPUTexture* as_texture(void* texture) noexcept {
    return static_cast<SDL_GPUTexture*>(texture);
}

SDL_GPUTransferBuffer* as_transfer(void* staging) noexcept {
    return static_cast<SDL_GPUTransferBuffer*>(staging);
}

}

SdlGpuPresenter::~SdlGpuPresenter() {
    release();
}

WindowResult SdlGpuPresenter::create() noexcept {
    return create(nullptr);
}

WindowResult SdlGpuPresenter::create(const char* driver) noexcept {
    if (device_ != nullptr) {
        return WindowResult::invalid_state;
    }
    if (driver != nullptr && driver[0] == '\0') {
        return WindowResult::invalid_input;
    }

    SDL_GPUDevice* device = SDL_CreateGPUDevice(kShaderFormats, false, driver);
    if (device == nullptr) {
        return WindowResult::unsupported;
    }

    device_ = device;
    return WindowResult::ready;
}

bool SdlGpuPresenter::ready() const noexcept {
    return device_ != nullptr;
}

const char* SdlGpuPresenter::device_driver() const noexcept {
    if (device_ == nullptr) {
        return "";
    }

    const char* driver = SDL_GetGPUDeviceDriver(as_device(device_));
    return driver != nullptr ? driver : "";
}

std::uint64_t SdlGpuPresenter::generation() const noexcept {
    return generation_;
}

bool SdlGpuPresenter::claimed() const noexcept {
    return window_ != nullptr;
}

void* SdlGpuPresenter::gpu_device() const noexcept {
    return device_;
}

WindowResult SdlGpuPresenter::claim(Sdl3Window& window) noexcept {
    if (device_ == nullptr) {
        return WindowResult::invalid_state;
    }
    if (window_ == &window) {
        return WindowResult::ready;
    }
    if (window_ != nullptr) {
        return WindowResult::invalid_state;
    }

    const WindowResult window_ready = window.creation_result();
    if (window_ready != WindowResult::ready) {
        return window_ready;
    }

    const WindowResult claimed = window.claim_for_gpu_device(device_);
    if (claimed != WindowResult::ready) {
        return claimed;
    }

    window_ = &window;
    ++generation_;
    return WindowResult::ready;
}

void SdlGpuPresenter::release() noexcept {
    if (window_ != nullptr) {
        window_->release_from_gpu_device(device_);
        window_ = nullptr;
        ++generation_;
    }

    destroy_resources();
    destroy_device();
}

WindowResult SdlGpuPresenter::acquire_surface(
    SoftwareSurface& surface,
    std::span<SoftwarePixel> backing
) noexcept {
    if (frame_acquired_) {
        return WindowResult::invalid_state;
    }

    surface = {};

    if (device_ == nullptr || window_ == nullptr) {
        return WindowResult::invalid_state;
    }

    void* handle = window_->window_handle();
    if (handle == nullptr) {
        return WindowResult::presentation_failed;
    }

    int width = 0;
    int height = 0;
    SDL_GetWindowSizeInPixels(as_window(handle), &width, &height);
    if (width <= 0 || height <= 0) {
        return WindowResult::suspended;
    }

    const auto pixel_width = static_cast<std::size_t>(width);
    const auto pixel_height = static_cast<std::size_t>(height);
    if (pixel_width > static_cast<std::size_t>(std::numeric_limits<int>::max()) / kBytesPerPixel ||
        pixel_height > std::numeric_limits<std::size_t>::max() / pixel_width) {
        return WindowResult::unsupported_size;
    }
    const auto pixel_count = pixel_width * pixel_height;
    if (backing.size() < pixel_count) {
        return WindowResult::insufficient_capacity;
    }
    if (static_cast<std::uint64_t>(pixel_count) * kBytesPerPixel >
        static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max())) {
        return WindowResult::unsupported_size;
    }

    pending_width_ = pixel_width;
    pending_height_ = pixel_height;
    surface = {
        .width = pixel_width,
        .height = pixel_height,
        .stride = pixel_width,
        .pixels = backing.first(pixel_count),
    };
    acquired_pixels_ = surface.pixels;
    frame_acquired_ = true;
    return WindowResult::ready;
}

WindowResult SdlGpuPresenter::present() noexcept {
    if (!frame_acquired_) {
        return WindowResult::invalid_state;
    }

    frame_acquired_ = false;
    const WindowResult result = present_pixels();
    acquired_pixels_ = {};
    return result;
}

WindowResult SdlGpuPresenter::present_texture(
    const void* texture,
    std::size_t width,
    std::size_t height
) noexcept {
    if (device_ == nullptr || window_ == nullptr) {
        return WindowResult::presentation_failed;
    }

    if (texture == nullptr || width == 0 || height == 0) {
        return WindowResult::invalid_input;
    }

    if (width > static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max()) ||
        height > static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())) {
        return WindowResult::unsupported_size;
    }

    void* handle = window_->window_handle();

    if (handle == nullptr) {
        return WindowResult::presentation_failed;
    }

    SDL_GPUCommandBuffer* commands = SDL_AcquireGPUCommandBuffer(as_device(device_));

    if (commands == nullptr) {
        return WindowResult::presentation_failed;
    }

    SDL_GPUTexture* swapchain = nullptr;
    std::uint32_t swapchain_width = 0;
    std::uint32_t swapchain_height = 0;

    if (!SDL_WaitAndAcquireGPUSwapchainTexture(
            commands,
            as_window(handle),
            &swapchain,
            &swapchain_width,
            &swapchain_height
        )) {
        SDL_CancelGPUCommandBuffer(commands);

        return WindowResult::presentation_failed;
    }

    if (swapchain == nullptr || swapchain_width == 0 || swapchain_height == 0) {
        SDL_SubmitGPUCommandBuffer(commands);

        return WindowResult::suspended;
    }

    const auto blit = SDL_GPUBlitInfo{
        .source =
            {
                .texture = const_cast<SDL_GPUTexture*>(static_cast<const SDL_GPUTexture*>(texture)),
                .mip_level = 0,
                .layer_or_depth_plane = 0,
                .x = 0,
                .y = 0,
                .w = static_cast<std::uint32_t>(width),
                .h = static_cast<std::uint32_t>(height),
            },
        .destination =
            {
                .texture = swapchain,
                .mip_level = 0,
                .layer_or_depth_plane = 0,
                .x = 0,
                .y = 0,
                .w = swapchain_width,
                .h = swapchain_height,
            },
        .load_op = SDL_GPU_LOADOP_DONT_CARE,
        .clear_color = {},
        .flip_mode = SDL_FLIP_NONE,
        .filter = SDL_GPU_FILTER_LINEAR,
        .cycle = false,
    };

    SDL_BlitGPUTexture(commands, &blit);

    if (!SDL_SubmitGPUCommandBuffer(commands)) {
        return WindowResult::presentation_failed;
    }

    return WindowResult::ready;
}

void SdlGpuPresenter::destroy_resources() noexcept {
    if (upload_ == nullptr && staging_ == nullptr) {
        return;
    }

    if (upload_ != nullptr) {
        SDL_ReleaseGPUTexture(as_device(device_), as_texture(upload_));
        upload_ = nullptr;
    }
    if (staging_ != nullptr) {
        SDL_ReleaseGPUTransferBuffer(as_device(device_), as_transfer(staging_));
        staging_ = nullptr;
    }

    staging_bytes_ = 0;
    texture_width_ = 0;
    texture_height_ = 0;
    ++generation_;
}

void SdlGpuPresenter::destroy_device() noexcept {
    if (device_ == nullptr) {
        return;
    }

    SDL_DestroyGPUDevice(as_device(device_));
    device_ = nullptr;
}

WindowResult SdlGpuPresenter::ensure_resources(std::size_t width, std::size_t height) noexcept {
    if (upload_ != nullptr && width == texture_width_ && height == texture_height_) {
        return WindowResult::ready;
    }

    destroy_resources();

    const auto bytes = static_cast<std::uint64_t>(width) * height * kBytesPerPixel;
    const auto transfer_info = SDL_GPUTransferBufferCreateInfo{
        .usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
        .size = static_cast<std::uint32_t>(bytes),
        .props = 0,
    };
    SDL_GPUTransferBuffer* staging =
        SDL_CreateGPUTransferBuffer(as_device(device_), &transfer_info);
    if (staging == nullptr) {
        return WindowResult::presentation_failed;
    }

    const auto texture_info = SDL_GPUTextureCreateInfo{
        .type = SDL_GPU_TEXTURETYPE_2D,
        .format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
        .usage = SDL_GPU_TEXTUREUSAGE_SAMPLER,
        .width = static_cast<std::uint32_t>(width),
        .height = static_cast<std::uint32_t>(height),
        .layer_count_or_depth = 1,
        .num_levels = 1,
        .sample_count = SDL_GPU_SAMPLECOUNT_1,
        .props = 0,
    };
    SDL_GPUTexture* upload = SDL_CreateGPUTexture(as_device(device_), &texture_info);
    if (upload == nullptr) {
        SDL_ReleaseGPUTransferBuffer(as_device(device_), staging);
        return WindowResult::presentation_failed;
    }

    staging_ = staging;
    upload_ = upload;
    staging_bytes_ = static_cast<std::size_t>(bytes);
    texture_width_ = width;
    texture_height_ = height;
    ++generation_;
    return WindowResult::ready;
}

WindowResult SdlGpuPresenter::present_pixels() noexcept {
    if (device_ == nullptr || window_ == nullptr) {
        return WindowResult::presentation_failed;
    }

    void* handle = window_->window_handle();
    if (handle == nullptr) {
        return WindowResult::presentation_failed;
    }

    if (ensure_resources(pending_width_, pending_height_) != WindowResult::ready) {
        return WindowResult::presentation_failed;
    }

    SDL_GPUCommandBuffer* commands = SDL_AcquireGPUCommandBuffer(as_device(device_));
    if (commands == nullptr) {
        return WindowResult::presentation_failed;
    }

    void* mapping = SDL_MapGPUTransferBuffer(as_device(device_), as_transfer(staging_), true);
    if (mapping == nullptr) {
        SDL_CancelGPUCommandBuffer(commands);
        return WindowResult::presentation_failed;
    }
    std::memcpy(mapping, acquired_pixels_.data(), staging_bytes_);
    SDL_UnmapGPUTransferBuffer(as_device(device_), as_transfer(staging_));

    SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(commands);
    if (copy == nullptr) {
        SDL_CancelGPUCommandBuffer(commands);
        return WindowResult::presentation_failed;
    }
    const auto source = SDL_GPUTextureTransferInfo{
        .transfer_buffer = as_transfer(staging_),
        .offset = 0,
        .pixels_per_row = static_cast<std::uint32_t>(pending_width_),
        .rows_per_layer = static_cast<std::uint32_t>(pending_height_),
    };
    const auto destination = SDL_GPUTextureRegion{
        .texture = as_texture(upload_),
        .mip_level = 0,
        .layer = 0,
        .x = 0,
        .y = 0,
        .z = 0,
        .w = static_cast<std::uint32_t>(pending_width_),
        .h = static_cast<std::uint32_t>(pending_height_),
        .d = 1,
    };
    SDL_UploadToGPUTexture(copy, &source, &destination, false);
    SDL_EndGPUCopyPass(copy);

    SDL_GPUTexture* swapchain = nullptr;
    std::uint32_t swapchain_width = 0;
    std::uint32_t swapchain_height = 0;
    if (!SDL_WaitAndAcquireGPUSwapchainTexture(
            commands,
            as_window(handle),
            &swapchain,
            &swapchain_width,
            &swapchain_height
        )) {
        SDL_CancelGPUCommandBuffer(commands);
        return WindowResult::presentation_failed;
    }
    if (swapchain == nullptr || swapchain_width == 0 || swapchain_height == 0) {
        SDL_SubmitGPUCommandBuffer(commands);
        return WindowResult::suspended;
    }

    const auto blit = SDL_GPUBlitInfo{
        .source =
            {
                .texture = as_texture(upload_),
                .mip_level = 0,
                .layer_or_depth_plane = 0,
                .x = 0,
                .y = 0,
                .w = static_cast<std::uint32_t>(pending_width_),
                .h = static_cast<std::uint32_t>(pending_height_),
            },
        .destination =
            {
                .texture = swapchain,
                .mip_level = 0,
                .layer_or_depth_plane = 0,
                .x = 0,
                .y = 0,
                .w = swapchain_width,
                .h = swapchain_height,
            },
        .load_op = SDL_GPU_LOADOP_DONT_CARE,
        .clear_color = {},
        .flip_mode = SDL_FLIP_NONE,
        .filter = SDL_GPU_FILTER_LINEAR,
        .cycle = false,
    };
    SDL_BlitGPUTexture(commands, &blit);

    if (!SDL_SubmitGPUCommandBuffer(commands)) {
        return WindowResult::presentation_failed;
    }

    return WindowResult::ready;
}

WindowResult Sdl3Window::claim_for_gpu_device(void* device) noexcept {
    if (creation_result_ != WindowResult::ready) {
        return creation_result_;
    }
    if (device == nullptr) {
        return WindowResult::invalid_input;
    }

    void* handle = window_handle();
    if (handle == nullptr) {
        return WindowResult::invalid_state;
    }
    if (!SDL_ClaimWindowForGPUDevice(as_device(device), as_window(handle))) {
        return WindowResult::unsupported;
    }

    return WindowResult::ready;
}

void Sdl3Window::release_from_gpu_device(void* device) noexcept {
    if (device == nullptr || creation_result_ != WindowResult::ready) {
        return;
    }

    void* handle = window_handle();
    if (handle == nullptr) {
        return;
    }

    SDL_ReleaseWindowFromGPUDevice(as_device(device), as_window(handle));
}

void* Sdl3Window::window_handle() noexcept {
    if (creation_result_ != WindowResult::ready || window_ == nullptr) {
        return nullptr;
    }

    return window_.get();
}

}
