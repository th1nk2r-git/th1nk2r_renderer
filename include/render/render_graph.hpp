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

class Device;
class FramesInFlight;
class MemoryAllocator;

enum class ResourceMultiplicity {
    Single,
    PerFrame
};

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
    VertexStorageRead,
    FragmentUniform,
    FragmentStorageRead,
    FragmentStorageWrite,
    ComputeStorageRead,
    ComputeStorageWrite,
    Indirect,
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
        const Device& device,
        const MemoryAllocator& allocator,
        const FramesInFlight& frames_in_flight,
        ThreadPool& thread_pool
    );

    RenderGraph(const RenderGraph&) = delete;
    auto operator=(const RenderGraph&) -> RenderGraph& = delete;
    RenderGraph(RenderGraph&&) = delete;
    auto operator=(RenderGraph&&) -> RenderGraph& = delete;

    auto create_image(
        std::string name,
        const ImageDesc& desc,
        ResourceMultiplicity multiplicity = ResourceMultiplicity::Single
    ) -> void;
    auto bind_external_image(std::string name, Image& image) -> void;
    auto image(std::string_view name) -> Image&;
    auto image(std::string_view name) const -> const Image&;
    auto image(std::string_view name, uint32_t instance) -> Image&;
    auto image(std::string_view name, uint32_t instance) const -> const Image&;
    auto image_instance_count(std::string_view name) const -> uint32_t;

    auto create_buffer(
        std::string name,
        const BufferDesc& desc,
        ResourceMultiplicity multiplicity = ResourceMultiplicity::Single
    ) -> void;
    auto bind_external_buffer(std::string name, Buffer& buffer) -> void;
    auto buffer(std::string_view name) -> Buffer&;
    auto buffer(std::string_view name) const -> const Buffer&;
    auto buffer(std::string_view name, uint32_t instance) -> Buffer&;
    auto buffer(std::string_view name, uint32_t instance) const -> const Buffer&;
    auto buffer_instance_count(std::string_view name) const -> uint32_t;

    auto current_frame_index() const noexcept -> uint32_t;
    auto frames_in_flight_count() const noexcept -> uint32_t;

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
    auto reset() -> void;

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

    struct DeclaredImage {
        ImageDesc desc;
        ResourceMultiplicity multiplicity = ResourceMultiplicity::Single;
    };

    struct DeclaredBuffer {
        BufferDesc desc;
        ResourceMultiplicity multiplicity = ResourceMultiplicity::Single;
    };

    const Device& device_;
    const MemoryAllocator& allocator_;
    const FramesInFlight& frames_in_flight_;
    ThreadPool& thread_pool_;
    ImageRegistry images_;
    BufferRegistry buffers_;

    std::unordered_map<std::string, DeclaredImage> declared_images_;
    std::unordered_map<std::string, DeclaredBuffer> declared_buffers_;

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
    std::unordered_set<const Buffer*> initialized_buffers_;

    bool compiled_ = false;
    bool resources_created_ = false;

    auto create_declared_resources(
        const std::unordered_map<std::string, vk::ImageUsageFlags>& image_flags,
        const std::unordered_map<std::string, vk::BufferUsageFlags>& buffer_flags
    ) -> void;

    auto image_instance(std::string_view name) const -> uint32_t;
    auto buffer_instance(std::string_view name) const -> uint32_t;
};

#endif
