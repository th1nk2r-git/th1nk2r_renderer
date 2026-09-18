#ifndef RENDER_GRAPH_HPP
#define RENDER_GRAPH_HPP

#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <vulkan/vulkan_raii.hpp>

#include "core/thread_pool.hpp"
#include "render/buffer_registry.hpp"
#include "render/image_registry.hpp"

enum class ImageUsage {
    ColorAttachment,
    DepthAttachment,
    FragmentSampled,
    ComputeSampled,
    ComputeStorageRead,
    ComputeStorageWrite,
    TransferSource,
    TransferDestination,
    Present
};

enum class BufferUsage {
    Vertex,
    Index,
    VertexUniform,
    FragmentUniform,
    FragmentStorageRead,
    FragmentStorageWrite,
    ComputeStorageRead,
    ComputeStorageWrite,
    TransferSource,
    TransferDestination
};

struct RenderNode {
    using RecordCallback = std::function<vk::CommandBuffer()>;

    std::string name;
    RecordCallback record;
};

class RenderGraph {
public:
    RenderGraph(
        ImageRegistry& images,
        BufferRegistry& buffers,
        ThreadPool& thread_pool
    );

    RenderGraph(const RenderGraph&) = delete;
    auto operator=(const RenderGraph&) -> RenderGraph& = delete;
    RenderGraph(RenderGraph&&) = delete;
    auto operator=(RenderGraph&&) -> RenderGraph& = delete;

    auto create_node(
        std::string name,
        RenderNode::RecordCallback record
    ) -> void;

    auto add_dependency(
        std::string_view node,
        std::string_view dependency
    ) -> void;

    auto set_image_usage(
        std::string_view node,
        std::string_view resource,
        ImageUsage usage
    ) -> void;

    auto set_buffer_usage(
        std::string_view node,
        std::string_view resource,
        BufferUsage usage
    ) -> void;

    auto set_output(std::string_view resource) -> void;

    auto compile() -> void;

    auto record(vk::raii::CommandBuffer& primary_command_buffer) -> void;

private:
    struct ImageBarrierPlan {
        std::string resource;
        vk::PipelineStageFlags source_stage;
        vk::PipelineStageFlags destination_stage;
        vk::AccessFlags source_access;
        vk::AccessFlags destination_access;
        vk::ImageLayout old_layout;
        vk::ImageLayout new_layout;
        bool first_use = false;
        bool require_initial_transition = false;
    };

    struct BufferBarrierPlan {
        std::string resource;
        vk::PipelineStageFlags source_stage;
        vk::PipelineStageFlags destination_stage;
        vk::AccessFlags source_access;
        vk::AccessFlags destination_access;
        bool first_use = false;
    };

    ImageRegistry& images_;
    BufferRegistry& buffers_;
    ThreadPool& thread_pool_;

    std::unordered_map<std::string, RenderNode> nodes_;
    std::vector<std::string> node_order_;
    std::unordered_map<std::string, std::vector<std::string>> dependencies_;
    std::unordered_map<std::string, std::unordered_map<std::string, ImageUsage>> image_usages_;
    std::unordered_map<std::string, std::vector<std::string>> image_usage_order_;
    std::unordered_map<std::string, std::unordered_map<std::string, BufferUsage>> buffer_usages_;
    std::vector<std::string> outputs_;

    std::vector<std::string> execution_order_;
    std::unordered_map<std::string, std::vector<ImageBarrierPlan>> image_barriers_;
    std::unordered_map<std::string, std::vector<BufferBarrierPlan>> buffer_barriers_;
    std::vector<ImageBarrierPlan> final_image_barriers_;
    std::unordered_set<uint64_t> initialized_images_;

    bool compiled_ = false;
    bool first_record_ = true;
};

#endif
