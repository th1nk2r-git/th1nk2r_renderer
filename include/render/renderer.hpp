#ifndef RENDERER_HPP
#define RENDERER_HPP

#include <array>

#include "gfx/device/device_context.hpp"
#include "gfx/frame/frame_context.hpp"
#include "gfx/pipeline/graphics_pipeline.hpp"
#include "gfx/resources/buffer.hpp"
#include "gfx/swapchain/swapchain_context.hpp"
#include "gfx/resources/data_uploader.hpp"
#include "platform/window.hpp"

struct Vertex {
    float position[2];
    float color[3];
};

class Renderer {
public:
    Renderer() = default;

    // create the renderer
    auto create(const Window& window) -> void;

    // render a frame
    auto render() -> void;

    // wait gpu
    auto wait_idle() const -> void {
        device_context_.device().logical_device().waitIdle();
    }

    // recreate the swapchain
    auto recreate_swapchain(const Window& window) -> void;

private:
    DeviceContext device_context_;
    DataUploader data_uploader_;
    SwapchainContext swapchain_context_;
    FrameContext frame_context_;

    PipelineLayout main_pipeline_layout_;
    GraphicsPipeline main_pipeline_;

    std::vector<Vertex> vertices_{
        Vertex{{0.0F, 0.0F}, {1.0F, 1.0F, 1.0F}},
        Vertex{{0.0F, 0.7F}, {1.0F, 0.0F, 0.0F}},
        Vertex{{-0.1571F, 0.2162F}, {1.0F, 0.5F, 0.0F}},
        Vertex{{-0.6658F, 0.2163F}, {1.0F, 1.0F, 0.0F}},
        Vertex{{-0.2542F, -0.0826F}, {0.5F, 1.0F, 0.0F}},
        Vertex{{-0.4115F, -0.5663F}, {0.0F, 1.0F, 1.0F}},
        Vertex{{0.0F, -0.2673F}, {0.0F, 0.0F, 1.0F}},
        Vertex{{0.4115F, -0.5663F}, {1.0F, 0.0F, 1.0F}},
        Vertex{{0.2542F, -0.0826F}, {1.0F, 0.5F, 0.5F}},
        Vertex{{0.6658F, 0.2163F}, {0.8F, 0.0F, 0.5F}},
        Vertex{{0.1571F, 0.2162F}, {0.0F, 0.5F, 0.5F}}
    };

    std::vector<uint16_t> indices_{
        0, 2, 1,
        0, 3, 2,
        0, 4, 3,
        0, 5, 4,
        0, 6, 5,
        0, 7, 6,
        0, 8, 7,
        0, 9, 8,
        0, 10, 9,
        0, 1, 10
    };

    Buffer vertex_buffer_;
    Buffer index_buffer_;

    // create the default graphics pipeline
    auto create_main_pipeline() -> void;

    // create the default buffers
    auto create_buffers() -> void;

    // recourd the command
    auto record(uint32_t image_id) -> void;

    // submit the command
    auto submit(uint32_t image_id) -> void;

    // present the image
    auto present(uint32_t image_id) -> vk::Result;
};

#endif
