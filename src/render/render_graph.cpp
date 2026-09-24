#include "render/render_graph.hpp"

#include "gfx/frame/frames_in_flight.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <future>
#include <optional>
#include <stdexcept>
#include <unordered_map>
#include <utility>

namespace {
    auto image_stage(ImageUsage usage) -> vk::PipelineStageFlags {
        switch (usage) {
        case ImageUsage::ColorAttachment:
            return vk::PipelineStageFlagBits::eColorAttachmentOutput;
        case ImageUsage::DepthAttachment:
            return vk::PipelineStageFlagBits::eEarlyFragmentTests |
                vk::PipelineStageFlagBits::eLateFragmentTests;
        case ImageUsage::FragmentSampled:
            return vk::PipelineStageFlagBits::eFragmentShader;
        case ImageUsage::ComputeSampled:
        case ImageUsage::ComputeStorageRead:
        case ImageUsage::ComputeStorageWrite:
            return vk::PipelineStageFlagBits::eComputeShader;
        case ImageUsage::TransferSource:
        case ImageUsage::TransferDestination:
            return vk::PipelineStageFlagBits::eTransfer;
        case ImageUsage::Present:
            return vk::PipelineStageFlagBits::eBottomOfPipe;
        }
        throw std::logic_error("unknown image usage!");
    }

    auto image_access(ImageUsage usage) -> vk::AccessFlags {
        switch (usage) {
        case ImageUsage::ColorAttachment:
            return vk::AccessFlagBits::eColorAttachmentRead |
                vk::AccessFlagBits::eColorAttachmentWrite;
        case ImageUsage::DepthAttachment:
            return vk::AccessFlagBits::eDepthStencilAttachmentRead |
                vk::AccessFlagBits::eDepthStencilAttachmentWrite;
        case ImageUsage::FragmentSampled:
        case ImageUsage::ComputeSampled:
        case ImageUsage::ComputeStorageRead:
            return vk::AccessFlagBits::eShaderRead;
        case ImageUsage::ComputeStorageWrite:
            return vk::AccessFlagBits::eShaderRead |
                vk::AccessFlagBits::eShaderWrite;
        case ImageUsage::TransferSource:
            return vk::AccessFlagBits::eTransferRead;
        case ImageUsage::TransferDestination:
            return vk::AccessFlagBits::eTransferWrite;
        case ImageUsage::Present:
            return {};
        }
        throw std::logic_error("unknown image usage!");
    }

    auto image_layout(ImageUsage usage) -> vk::ImageLayout {
        switch (usage) {
        case ImageUsage::ColorAttachment:
            return vk::ImageLayout::eColorAttachmentOptimal;
        case ImageUsage::DepthAttachment:
            return vk::ImageLayout::eDepthStencilAttachmentOptimal;
        case ImageUsage::FragmentSampled:
        case ImageUsage::ComputeSampled:
            return vk::ImageLayout::eShaderReadOnlyOptimal;
        case ImageUsage::ComputeStorageRead:
        case ImageUsage::ComputeStorageWrite:
            return vk::ImageLayout::eGeneral;
        case ImageUsage::TransferSource:
            return vk::ImageLayout::eTransferSrcOptimal;
        case ImageUsage::TransferDestination:
            return vk::ImageLayout::eTransferDstOptimal;
        case ImageUsage::Present:
            return vk::ImageLayout::ePresentSrcKHR;
        }
        throw std::logic_error("unknown image usage!");
    }

    auto required_image_flags(ImageUsage usage) -> vk::ImageUsageFlags {
        switch (usage) {
        case ImageUsage::ColorAttachment:
            return vk::ImageUsageFlagBits::eColorAttachment;
        case ImageUsage::DepthAttachment:
            return vk::ImageUsageFlagBits::eDepthStencilAttachment;
        case ImageUsage::FragmentSampled:
        case ImageUsage::ComputeSampled:
            return vk::ImageUsageFlagBits::eSampled;
        case ImageUsage::ComputeStorageRead:
        case ImageUsage::ComputeStorageWrite:
            return vk::ImageUsageFlagBits::eStorage;
        case ImageUsage::TransferSource:
            return vk::ImageUsageFlagBits::eTransferSrc;
        case ImageUsage::TransferDestination:
            return vk::ImageUsageFlagBits::eTransferDst;
        case ImageUsage::Present:
            return {};
        }
        throw std::logic_error("unknown image usage!");
    }

    auto image_aspects(vk::Format format) -> vk::ImageAspectFlags {
        switch (format) {
        case vk::Format::eD16Unorm:
        case vk::Format::eX8D24UnormPack32:
        case vk::Format::eD32Sfloat:
            return vk::ImageAspectFlagBits::eDepth;
        case vk::Format::eS8Uint:
            return vk::ImageAspectFlagBits::eStencil;
        case vk::Format::eD16UnormS8Uint:
        case vk::Format::eD24UnormS8Uint:
        case vk::Format::eD32SfloatS8Uint:
            return vk::ImageAspectFlagBits::eDepth |
                vk::ImageAspectFlagBits::eStencil;
        default:
            return vk::ImageAspectFlagBits::eColor;
        }
    }

    auto buffer_stage(BufferUsage usage) -> vk::PipelineStageFlags {
        switch (usage) {
        case BufferUsage::Vertex:
        case BufferUsage::Index:
            return vk::PipelineStageFlagBits::eVertexInput;
        case BufferUsage::VertexUniform:
            return vk::PipelineStageFlagBits::eVertexShader;
        case BufferUsage::VertexStorageRead:
            return vk::PipelineStageFlagBits::eVertexShader;
        case BufferUsage::FragmentUniform:
        case BufferUsage::FragmentStorageRead:
        case BufferUsage::FragmentStorageWrite:
            return vk::PipelineStageFlagBits::eFragmentShader;
        case BufferUsage::ComputeStorageRead:
        case BufferUsage::ComputeStorageWrite:
            return vk::PipelineStageFlagBits::eComputeShader;
        case BufferUsage::Indirect:
            return vk::PipelineStageFlagBits::eDrawIndirect;
        case BufferUsage::TransferSource:
        case BufferUsage::TransferDestination:
            return vk::PipelineStageFlagBits::eTransfer;
        }
        throw std::logic_error("unknown buffer usage!");
    }

    auto buffer_access(BufferUsage usage) -> vk::AccessFlags {
        switch (usage) {
        case BufferUsage::Vertex:
            return vk::AccessFlagBits::eVertexAttributeRead;
        case BufferUsage::Index:
            return vk::AccessFlagBits::eIndexRead;
        case BufferUsage::VertexUniform:
        case BufferUsage::FragmentUniform:
            return vk::AccessFlagBits::eUniformRead;
        case BufferUsage::VertexStorageRead:
        case BufferUsage::FragmentStorageRead:
        case BufferUsage::ComputeStorageRead:
            return vk::AccessFlagBits::eShaderRead;
        case BufferUsage::FragmentStorageWrite:
        case BufferUsage::ComputeStorageWrite:
            return vk::AccessFlagBits::eShaderRead |
                vk::AccessFlagBits::eShaderWrite;
        case BufferUsage::Indirect:
            return vk::AccessFlagBits::eIndirectCommandRead;
        case BufferUsage::TransferSource:
            return vk::AccessFlagBits::eTransferRead;
        case BufferUsage::TransferDestination:
            return vk::AccessFlagBits::eTransferWrite;
        }
        throw std::logic_error("unknown buffer usage!");
    }

    auto required_buffer_flags(BufferUsage usage) -> vk::BufferUsageFlags {
        switch (usage) {
        case BufferUsage::Vertex:
            return vk::BufferUsageFlagBits::eVertexBuffer;
        case BufferUsage::Index:
            return vk::BufferUsageFlagBits::eIndexBuffer;
        case BufferUsage::VertexUniform:
        case BufferUsage::FragmentUniform:
            return vk::BufferUsageFlagBits::eUniformBuffer;
        case BufferUsage::VertexStorageRead:
        case BufferUsage::FragmentStorageRead:
        case BufferUsage::FragmentStorageWrite:
        case BufferUsage::ComputeStorageRead:
        case BufferUsage::ComputeStorageWrite:
            return vk::BufferUsageFlagBits::eStorageBuffer;
        case BufferUsage::Indirect:
            return vk::BufferUsageFlagBits::eIndirectBuffer;
        case BufferUsage::TransferSource:
            return vk::BufferUsageFlagBits::eTransferSrc;
        case BufferUsage::TransferDestination:
            return vk::BufferUsageFlagBits::eTransferDst;
        }
        throw std::logic_error("unknown buffer usage!");
    }

    auto has_write_access(vk::AccessFlags access) -> bool {
        constexpr auto writes =
            vk::AccessFlagBits::eColorAttachmentWrite |
            vk::AccessFlagBits::eDepthStencilAttachmentWrite |
            vk::AccessFlagBits::eShaderWrite |
            vk::AccessFlagBits::eTransferWrite;
        return static_cast<bool>(access & writes);
    }

    auto layout_stage(vk::ImageLayout layout) -> vk::PipelineStageFlags {
        switch (layout) {
        case vk::ImageLayout::eUndefined:
            return vk::PipelineStageFlagBits::eTopOfPipe;
        case vk::ImageLayout::ePresentSrcKHR:
            return vk::PipelineStageFlagBits::eBottomOfPipe;
        case vk::ImageLayout::eTransferSrcOptimal:
        case vk::ImageLayout::eTransferDstOptimal:
            return vk::PipelineStageFlagBits::eTransfer;
        default:
            return vk::PipelineStageFlagBits::eAllCommands;
        }
    }

    auto has_stencil(vk::Format format) -> bool {
        return format == vk::Format::eS8Uint ||
            format == vk::Format::eD16UnormS8Uint ||
            format == vk::Format::eD24UnormS8Uint ||
            format == vk::Format::eD32SfloatS8Uint;
    }
}

RenderGraph::RenderGraph(
    const Device& device,
    const MemoryAllocator& allocator,
    const FramesInFlight& frames_in_flight,
    ThreadPool& thread_pool
) : device_(device),
    allocator_(allocator),
    frames_in_flight_(frames_in_flight),
    thread_pool_(thread_pool) {}

auto RenderGraph::create_image(
    std::string name,
    const ImageDesc& desc,
    ResourceMultiplicity multiplicity
) -> void {
    if (name.empty()) {
        throw std::invalid_argument("image name cannot be empty!");
    }
    if (resources_created_) {
        throw std::logic_error(
            "cannot declare images after render graph resources are created!"
        );
    }
    if (images_.contains(name)) {
        throw std::invalid_argument(
            "an external image named '" + name + "' is already registered!"
        );
    }

    const bool inserted = declared_images_.try_emplace(
        name,
        DeclaredImage{desc, multiplicity}
    ).second;
    if (!inserted) {
        throw std::invalid_argument(
            "an image named '" + name + "' is already declared!"
        );
    }
    compiled_ = false;
}

auto RenderGraph::bind_external_image(
    std::string name,
    Image& image
) -> void {
    if (declared_images_.contains(name)) {
        throw std::invalid_argument(
            "an owned image named '" + name + "' is already declared!"
        );
    }
    images_.bind_external(std::move(name), image);
}

auto RenderGraph::image(std::string_view name) -> Image& {
    return images_.query(name, image_instance(name));
}

auto RenderGraph::image(std::string_view name) const -> const Image& {
    return images_.query(name, image_instance(name));
}

auto RenderGraph::image(
    std::string_view name,
    uint32_t instance
) -> Image& {
    return images_.query(name, instance);
}

auto RenderGraph::image(
    std::string_view name,
    uint32_t instance
) const -> const Image& {
    return images_.query(name, instance);
}

auto RenderGraph::image_instance_count(std::string_view name) const
    -> uint32_t {
    return images_.instance_count(name);
}

auto RenderGraph::create_buffer(
    std::string name,
    const BufferDesc& desc,
    ResourceMultiplicity multiplicity
) -> void {
    if (name.empty()) {
        throw std::invalid_argument("buffer name cannot be empty!");
    }
    if (resources_created_) {
        throw std::logic_error(
            "cannot declare buffers after render graph resources are created!"
        );
    }
    if (buffers_.contains(name)) {
        throw std::invalid_argument(
            "an external buffer named '" + name + "' is already registered!"
        );
    }

    const bool inserted = declared_buffers_.try_emplace(
        name,
        DeclaredBuffer{desc, multiplicity}
    ).second;
    if (!inserted) {
        throw std::invalid_argument(
            "a buffer named '" + name + "' is already declared!"
        );
    }
    compiled_ = false;
}

auto RenderGraph::bind_external_buffer(
    std::string name,
    Buffer& buffer
) -> void {
    if (declared_buffers_.contains(name)) {
        throw std::invalid_argument(
            "an owned buffer named '" + name + "' is already declared!"
        );
    }
    buffers_.bind_external(std::move(name), buffer);
}

auto RenderGraph::buffer(std::string_view name) -> Buffer& {
    return buffers_.query(name, buffer_instance(name));
}

auto RenderGraph::buffer(std::string_view name) const -> const Buffer& {
    return buffers_.query(name, buffer_instance(name));
}

auto RenderGraph::buffer(
    std::string_view name,
    uint32_t instance
) -> Buffer& {
    return buffers_.query(name, instance);
}

auto RenderGraph::buffer(
    std::string_view name,
    uint32_t instance
) const -> const Buffer& {
    return buffers_.query(name, instance);
}

auto RenderGraph::buffer_instance_count(std::string_view name) const
    -> uint32_t {
    return buffers_.instance_count(name);
}

auto RenderGraph::current_frame_index() const noexcept -> uint32_t {
    return frames_in_flight_.current_index();
}

auto RenderGraph::frames_in_flight_count() const noexcept -> uint32_t {
    return frames_in_flight_.count();
}

auto RenderGraph::image_instance(std::string_view name) const -> uint32_t {
    const auto iterator = declared_images_.find(std::string{name});
    if (iterator == declared_images_.end() ||
        iterator->second.multiplicity == ResourceMultiplicity::Single) {
        return 0;
    }
    return frames_in_flight_.current_index();
}

auto RenderGraph::buffer_instance(std::string_view name) const -> uint32_t {
    const auto iterator = declared_buffers_.find(std::string{name});
    if (iterator == declared_buffers_.end() ||
        iterator->second.multiplicity == ResourceMultiplicity::Single) {
        return 0;
    }
    return frames_in_flight_.current_index();
}

auto RenderGraph::create_node(
    std::string name,
    RenderNode::RecordCallback record
) -> void {
    if (name.empty()) {
        throw std::invalid_argument("render node name cannot be empty!");
    }
    if (!record) {
        throw std::invalid_argument("render node callback cannot be empty!");
    }

    const bool inserted = nodes_.try_emplace(
        name,
        RenderNode{name, std::move(record)}
    ).second;
    if (!inserted) {
        throw std::invalid_argument(
            "a render node named '" + name + "' already exists!"
        );
    }

    node_order_.push_back(std::move(name));
    compiled_ = false;
}

auto RenderGraph::add_dependency(
    std::string_view node,
    std::string_view dependency
) -> void {
    const std::string node_name{node};
    const std::string dependency_name{dependency};

    if (!nodes_.contains(node_name)) {
        throw std::invalid_argument(
            "render node '" + node_name + "' does not exist!"
        );
    }
    if (!nodes_.contains(dependency_name)) {
        throw std::invalid_argument(
            "dependency node '" + dependency_name + "' does not exist!"
        );
    }
    if (node_name == dependency_name) {
        throw std::invalid_argument(
            "a render node cannot depend on itself!"
        );
    }

    auto& node_dependencies = dependencies_[node_name];
    if (std::find(
            node_dependencies.begin(),
            node_dependencies.end(),
            dependency_name
        ) == node_dependencies.end()) {
        node_dependencies.push_back(dependency_name);
        compiled_ = false;
    }
}

auto RenderGraph::set_image_usage(
    std::string_view node,
    std::string_view resource,
    ImageUsage usage
) -> void {
    const std::string node_name{node};
    if (!nodes_.contains(node_name)) {
        throw std::invalid_argument(
            "render node '" + node_name + "' does not exist!"
        );
    }

    const std::string resource_name{resource};
    auto& usages = image_usages_[node_name];
    if (!usages.contains(resource_name)) {
        image_usage_order_[node_name].push_back(resource_name);
    }
    usages.insert_or_assign(resource_name, usage);
    compiled_ = false;
}

auto RenderGraph::set_output(std::string_view resource) -> void {
    const std::string resource_name{resource};
    if (std::find(outputs_.begin(), outputs_.end(), resource_name) ==
        outputs_.end()) {
        outputs_.push_back(resource_name);
        compiled_ = false;
    }
}

auto RenderGraph::set_buffer_usage(
    std::string_view node,
    std::string_view resource,
    BufferUsage usage
) -> void {
    const std::string node_name{node};
    if (!nodes_.contains(node_name)) {
        throw std::invalid_argument(
            "render node '" + node_name + "' does not exist!"
        );
    }

    buffer_usages_[node_name].insert_or_assign(
        std::string{resource},
        usage
    );
    compiled_ = false;
}

auto RenderGraph::create_declared_resources(
    const std::unordered_map<std::string, vk::ImageUsageFlags>& image_flags,
    const std::unordered_map<std::string, vk::BufferUsageFlags>& buffer_flags
) -> void {
    if (resources_created_) {
        return;
    }

    for (const auto& [name, declaration] : declared_images_) {
        auto desc = declaration.desc;
        if (const auto iterator = image_flags.find(name);
            iterator != image_flags.end()) {
            desc.usage |= iterator->second;
        }

        const uint32_t count =
            declaration.multiplicity == ResourceMultiplicity::PerFrame
            ? frames_in_flight_.count()
            : 1;
        std::vector<Image> instances;
        instances.reserve(count);
        for (uint32_t index = 0; index < count; ++index) {
            instances.emplace_back(device_, allocator_, desc);
        }
        images_.add(name, std::move(instances));
    }

    for (const auto& [name, declaration] : declared_buffers_) {
        auto desc = declaration.desc;
        if (const auto iterator = buffer_flags.find(name);
            iterator != buffer_flags.end()) {
            desc.usage |= iterator->second;
        }

        const uint32_t count =
            declaration.multiplicity == ResourceMultiplicity::PerFrame
            ? frames_in_flight_.count()
            : 1;
        std::vector<Buffer> instances;
        instances.reserve(count);
        for (uint32_t index = 0; index < count; ++index) {
            instances.emplace_back(allocator_, desc);
        }
        buffers_.add(name, std::move(instances));
    }

    resources_created_ = true;
}

auto RenderGraph::compile() -> void {
    compiled_ = false;
    execution_order_.clear();
    image_barriers_.clear();
    buffer_barriers_.clear();
    final_image_barriers_.clear();

    std::unordered_map<std::string, uint8_t> visit_states;
    const std::function<void(const std::string&)> visit =
        [&](const std::string& node_name) {
            const auto state = visit_states[node_name];
            if (state == 1) {
                throw std::logic_error(
                    "render graph contains a cycle at node '" +
                    node_name + "'!"
                );
            }
            if (state == 2) {
                return;
            }

            visit_states[node_name] = 1;
            if (const auto iterator = dependencies_.find(node_name);
                iterator != dependencies_.end()) {
                for (const auto& dependency : iterator->second) {
                    visit(dependency);
                }
            }
            visit_states[node_name] = 2;
            execution_order_.push_back(node_name);
        };

    for (const auto& node_name : node_order_) {
        visit(node_name);
    }

    std::unordered_map<
        std::string,
        std::vector<std::pair<std::string, ImageUsage>>
    > image_sequences;
    std::unordered_map<
        std::string,
        std::vector<std::pair<std::string, BufferUsage>>
    > buffer_sequences;

    std::unordered_map<std::string, vk::ImageUsageFlags> image_flags;
    std::unordered_map<std::string, vk::BufferUsageFlags> buffer_flags;

    for (const auto& [node_name, usages] : image_usages_) {
        for (const auto& [resource_name, usage] : usages) {
            if (declared_images_.contains(resource_name)) {
                image_flags[resource_name] |= required_image_flags(usage);
                continue;
            }
            if (!images_.contains(resource_name)) {
                throw std::logic_error(
                    "render node '" + node_name +
                    "' uses undeclared image '" + resource_name + "'!"
                );
            }
        }
    }

    for (const auto& [node_name, usages] : buffer_usages_) {
        for (const auto& [resource_name, usage] : usages) {
            if (declared_buffers_.contains(resource_name)) {
                buffer_flags[resource_name] |= required_buffer_flags(usage);
                continue;
            }
            if (!buffers_.contains(resource_name)) {
                throw std::logic_error(
                    "render node '" + node_name +
                    "' uses undeclared buffer '" + resource_name + "'!"
                );
            }
        }
    }

    for (const auto& output : outputs_) {
        if (!declared_images_.contains(output) && !images_.contains(output)) {
            throw std::logic_error(
                "render graph output '" + output + "' is undeclared!"
            );
        }
    }

    create_declared_resources(image_flags, buffer_flags);

    for (const auto& node_name : execution_order_) {
        if (const auto iterator = image_usages_.find(node_name);
            iterator != image_usages_.end()) {
            uint32_t depth_attachment_count = 0;
            for (const auto& resource_name : image_usage_order_.at(node_name)) {
                const auto usage = iterator->second.at(resource_name);
                const auto& image = images_.query(resource_name);
                const auto required = required_image_flags(usage);
                if (required && (image.usage() & required) != required) {
                    throw std::logic_error(
                        "image '" + resource_name +
                        "' does not support its declared usage!"
                    );
                }
                if ((usage == ImageUsage::ColorAttachment ||
                     usage == ImageUsage::DepthAttachment) &&
                    !image.has_view()) {
                    throw std::logic_error(
                        "attachment image '" + resource_name +
                        "' does not have a default image view!"
                    );
                }
                if (usage == ImageUsage::DepthAttachment &&
                    ++depth_attachment_count > 1) {
                    throw std::logic_error(
                        "render node '" + node_name +
                        "' declares more than one depth attachment!"
                    );
                }
                image_sequences[resource_name].emplace_back(
                    node_name,
                    usage
                );
            }
        }

        if (const auto iterator = buffer_usages_.find(node_name);
            iterator != buffer_usages_.end()) {
            for (const auto& [resource_name, usage] : iterator->second) {
                const auto& buffer = buffers_.query(resource_name);
                const auto required = required_buffer_flags(usage);
                if ((buffer.usage() & required) != required) {
                    throw std::logic_error(
                        "buffer '" + resource_name +
                        "' does not support its declared usage!"
                    );
                }
                buffer_sequences[resource_name].emplace_back(
                    node_name,
                    usage
                );
            }
        }
    }

    for (const auto& output : outputs_) {
        const auto& image = images_.query(output);
        if (image.final_layout() == vk::ImageLayout::eUndefined) {
            throw std::logic_error(
                "render graph output '" + output +
                "' requires a defined final layout!"
            );
        }
        image_sequences.try_emplace(output);
    }

    for (const auto& [resource_name, sequence] : image_sequences) {
        const auto& image = images_.query(resource_name);
        const bool output = std::find(
            outputs_.begin(),
            outputs_.end(),
            resource_name
        ) != outputs_.end();

        if (sequence.empty()) {
            if (output) {
                final_image_barriers_.push_back(ImageBarrierPlan{
                    .resource = resource_name,
                    .source_stage = layout_stage(image.final_layout()),
                    .destination_stage = layout_stage(image.final_layout()),
                    .old_layout = image.final_layout(),
                    .new_layout = image.final_layout(),
                    .first_use = true,
                    .require_initial_transition = true
                });
            }
            continue;
        }

        if (output) {
            const auto& [first_node, first_usage] = sequence.front();
            image_barriers_[first_node].push_back(ImageBarrierPlan{
                .resource = resource_name,
                .source_stage = layout_stage(image.final_layout()),
                .destination_stage = image_stage(first_usage),
                .destination_access = image_access(first_usage),
                .old_layout = image.final_layout(),
                .new_layout = image_layout(first_usage),
                .first_use = true
            });

            for (std::size_t index = 1; index < sequence.size(); ++index) {
                const auto previous_usage = sequence[index - 1].second;
                const auto& [node_name, usage] = sequence[index];
                image_barriers_[node_name].push_back(ImageBarrierPlan{
                    .resource = resource_name,
                    .source_stage = image_stage(previous_usage),
                    .destination_stage = image_stage(usage),
                    .source_access = image_access(previous_usage),
                    .destination_access = image_access(usage),
                    .old_layout = image_layout(previous_usage),
                    .new_layout = image_layout(usage)
                });
            }

            const auto last_usage = sequence.back().second;
            final_image_barriers_.push_back(ImageBarrierPlan{
                .resource = resource_name,
                .source_stage = image_stage(last_usage),
                .destination_stage = layout_stage(image.final_layout()),
                .source_access = image_access(last_usage),
                .old_layout = image_layout(last_usage),
                .new_layout = image.final_layout()
            });
            continue;
        }

        auto previous_usage = sequence.back().second;
        for (std::size_t index = 0; index < sequence.size(); ++index) {
            const auto& [node_name, usage] = sequence[index];
            image_barriers_[node_name].push_back(ImageBarrierPlan{
                .resource = resource_name,
                .source_stage = image_stage(previous_usage),
                .destination_stage = image_stage(usage),
                .source_access = image_access(previous_usage),
                .destination_access = image_access(usage),
                .old_layout = image_layout(previous_usage),
                .new_layout = image_layout(usage),
                .first_use = index == 0
            });
            previous_usage = usage;
        }
    }

    for (const auto& [resource_name, sequence] : buffer_sequences) {
        auto previous_usage = sequence.back().second;

        for (std::size_t index = 0; index < sequence.size(); ++index) {
            const auto& [node_name, usage] = sequence[index];
            buffer_barriers_[node_name].push_back(BufferBarrierPlan{
                .resource = resource_name,
                .source_stage = buffer_stage(previous_usage),
                .destination_stage = buffer_stage(usage),
                .source_access = buffer_access(previous_usage),
                .destination_access = buffer_access(usage),
                .first_use = index == 0
            });
            previous_usage = usage;
        }
    }

    initialized_images_.clear();
    initialized_buffers_.clear();
    compiled_ = true;
}

auto RenderGraph::reset() -> void {
    images_.clear();
    buffers_.clear();

    declared_images_.clear();
    declared_buffers_.clear();

    nodes_.clear();
    node_order_.clear();
    dependencies_.clear();
    image_usages_.clear();
    image_usage_order_.clear();
    buffer_usages_.clear();
    outputs_.clear();

    execution_order_.clear();
    image_barriers_.clear();
    buffer_barriers_.clear();
    final_image_barriers_.clear();
    initialized_images_.clear();
    initialized_buffers_.clear();

    compiled_ = false;
    resources_created_ = false;
}

auto RenderGraph::record(vk::raii::CommandBuffer& primary_command_buffer) -> void {
    if (!compiled_) {
        throw std::logic_error(
            "render graph must be compiled before recording!"
        );
    }

    std::vector<std::future<vk::CommandBuffer>> futures;
    futures.reserve(execution_order_.size());
    for (const auto& node_name : execution_order_) {
        const auto* callback = &nodes_.at(node_name).record;
        futures.push_back(thread_pool_.run(
            [callback] {
                return (*callback)();
            }
        ));
    }

    std::vector<vk::CommandBuffer> secondary_command_buffers;
    secondary_command_buffers.reserve(futures.size());
    for (auto& future : futures) {
        auto command_buffer = future.get();
        if (command_buffer == vk::CommandBuffer{}) {
            throw std::runtime_error(
                "a render node returned an invalid command buffer!"
            );
        }
        secondary_command_buffers.push_back(command_buffer);
    }

    const std::vector<vk::ImageMemoryBarrier> no_image_barriers;
    const std::vector<vk::BufferMemoryBarrier> no_buffer_barriers;

    const auto insert_barriers = [this, &primary_command_buffer,
                                  &no_image_barriers,
                                  &no_buffer_barriers](
        const std::vector<ImageBarrierPlan>& image_plans,
        const std::vector<BufferBarrierPlan>& buffer_plans
    ) {
        std::vector<vk::ImageMemoryBarrier> image_barriers;
        std::vector<vk::BufferMemoryBarrier> buffer_barriers;
        vk::PipelineStageFlags source_stages{};
        vk::PipelineStageFlags destination_stages{};

        image_barriers.reserve(image_plans.size());
        for (const auto& plan : image_plans) {
            const auto& image = this->image(plan.resource);
            auto source_stage = plan.source_stage;
            auto source_access = plan.source_access;
            auto old_layout = plan.old_layout;

            if (plan.first_use && !initialized_images_.contains(image.id())) {
                initialized_images_.insert(image.id());
                if (image.initial_layout() == vk::ImageLayout::eUndefined &&
                    !has_write_access(plan.destination_access) &&
                    !plan.require_initial_transition) {
                    continue;
                }
                source_stage = layout_stage(image.initial_layout());
                source_access = {};
                old_layout = image.initial_layout();
            }

            if (old_layout == plan.new_layout &&
                !source_access && !plan.destination_access) {
                continue;
            }

            image_barriers.push_back(
                vk::ImageMemoryBarrier{}
                    .setSrcAccessMask(source_access)
                    .setDstAccessMask(plan.destination_access)
                    .setOldLayout(old_layout)
                    .setNewLayout(plan.new_layout)
                    .setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
                    .setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
                    .setImage(image.get())
                    .setSubresourceRange(vk::ImageSubresourceRange{
                        image_aspects(image.format()),
                        0,
                        image.mip_levels(),
                        0,
                        image.array_layers()
                    })
            );
            source_stages |= source_stage;
            destination_stages |= plan.destination_stage;
        }

        buffer_barriers.reserve(buffer_plans.size());
        for (const auto& plan : buffer_plans) {
            const auto& buffer = this->buffer(plan.resource);
            if (plan.first_use &&
                !initialized_buffers_.contains(&buffer)) {
                initialized_buffers_.insert(&buffer);
                continue;
            }
            buffer_barriers.push_back(
                vk::BufferMemoryBarrier{}
                    .setSrcAccessMask(plan.source_access)
                    .setDstAccessMask(plan.destination_access)
                    .setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
                    .setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
                    .setBuffer(buffer.get())
                    .setOffset(0)
                    .setSize(buffer.size())
            );
            source_stages |= plan.source_stage;
            destination_stages |= plan.destination_stage;
        }

        if (image_barriers.empty() && buffer_barriers.empty()) {
            return;
        }

        primary_command_buffer.pipelineBarrier(
            source_stages,
            destination_stages,
            {},
            {},
            buffer_barriers.empty() ? no_buffer_barriers : buffer_barriers,
            image_barriers.empty() ? no_image_barriers : image_barriers
        );
    };

    for (std::size_t index = 0; index < execution_order_.size(); ++index) {
        const auto& node_name = execution_order_[index];
        const auto image_iterator = image_barriers_.find(node_name);
        const auto buffer_iterator = buffer_barriers_.find(node_name);

        static const std::vector<ImageBarrierPlan> no_image_plans;
        static const std::vector<BufferBarrierPlan> no_buffer_plans;
        insert_barriers(
            image_iterator == image_barriers_.end()
                ? no_image_plans
                : image_iterator->second,
            buffer_iterator == buffer_barriers_.end()
                ? no_buffer_plans
                : buffer_iterator->second
        );

        std::vector<vk::RenderingAttachmentInfo> color_attachments;
        std::optional<vk::RenderingAttachmentInfo> depth_attachment;
        std::optional<vk::Extent2D> render_extent;
        vk::SampleCountFlagBits samples = vk::SampleCountFlagBits::e1;

        if (const auto usage_iterator = image_usages_.find(node_name);
            usage_iterator != image_usages_.end()) {
            for (const auto& resource_name : image_usage_order_.at(node_name)) {
                const auto usage = usage_iterator->second.at(resource_name);
                if (usage != ImageUsage::ColorAttachment &&
                    usage != ImageUsage::DepthAttachment) {
                    continue;
                }

                const auto& image = this->image(resource_name);
                if (!image.has_view()) {
                    throw std::logic_error(
                        "attachment image '" + resource_name +
                        "' does not have a default image view!"
                    );
                }

                const vk::Extent2D extent{
                    image.extent().width,
                    image.extent().height
                };
                if (render_extent.has_value() && *render_extent != extent) {
                    throw std::logic_error(
                        "render node '" + node_name +
                        "' attachment extents do not match!"
                    );
                }
                if (render_extent.has_value() && samples != image.samples()) {
                    throw std::logic_error(
                        "render node '" + node_name +
                        "' attachment sample counts do not match!"
                    );
                }
                render_extent = extent;
                samples = image.samples();

                vk::RenderingAttachmentInfo attachment{};
                attachment
                    .setImageView(*image.view())
                    .setImageLayout(image_layout(usage))
                    .setLoadOp(vk::AttachmentLoadOp::eClear)
                    .setStoreOp(vk::AttachmentStoreOp::eStore);

                vk::ClearValue clear_value{};
                if (usage == ImageUsage::ColorAttachment) {
                    clear_value.color.float32[0] = 0.01F;
                    clear_value.color.float32[1] = 0.015F;
                    clear_value.color.float32[2] = 0.025F;
                    clear_value.color.float32[3] = 1.0F;
                    attachment.setClearValue(clear_value);
                    color_attachments.push_back(attachment);
                }
                else {
                    clear_value.depthStencil.depth = 1.0F;
                    clear_value.depthStencil.stencil = 0;
                    attachment.setClearValue(clear_value);
                    depth_attachment = attachment;
                }
            }
        }

        const std::array commands{secondary_command_buffers[index]};
        if (render_extent.has_value()) {
            vk::RenderingInfo rendering_info{};
            rendering_info
                .setFlags(vk::RenderingFlagBits::eContentsSecondaryCommandBuffers)
                .setRenderArea(vk::Rect2D{
                    .offset = vk::Offset2D{0, 0},
                    .extent = *render_extent
                })
                .setLayerCount(1)
                .setColorAttachments(color_attachments);
            if (depth_attachment.has_value()) {
                rendering_info.setPDepthAttachment(&*depth_attachment);
                const auto& usages = image_usages_.at(node_name);
                for (const auto& resource_name : image_usage_order_.at(node_name)) {
                    if (usages.at(resource_name) == ImageUsage::DepthAttachment &&
                        has_stencil(this->image(resource_name).format())) {
                        rendering_info.setPStencilAttachment(&*depth_attachment);
                        break;
                    }
                }
            }

            primary_command_buffer.beginRendering(rendering_info);
            primary_command_buffer.executeCommands(commands);
            primary_command_buffer.endRendering();
        }
        else {
            primary_command_buffer.executeCommands(commands);
        }
    }

    static const std::vector<BufferBarrierPlan> no_buffer_plans;
    insert_barriers(final_image_barriers_, no_buffer_plans);

}
