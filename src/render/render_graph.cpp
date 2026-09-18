#include "render/render_graph.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <future>
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
        case BufferUsage::FragmentUniform:
        case BufferUsage::FragmentStorageRead:
        case BufferUsage::FragmentStorageWrite:
            return vk::PipelineStageFlagBits::eFragmentShader;
        case BufferUsage::ComputeStorageRead:
        case BufferUsage::ComputeStorageWrite:
            return vk::PipelineStageFlagBits::eComputeShader;
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
        case BufferUsage::FragmentStorageRead:
        case BufferUsage::ComputeStorageRead:
            return vk::AccessFlagBits::eShaderRead;
        case BufferUsage::FragmentStorageWrite:
        case BufferUsage::ComputeStorageWrite:
            return vk::AccessFlagBits::eShaderRead |
                vk::AccessFlagBits::eShaderWrite;
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
        case BufferUsage::FragmentStorageRead:
        case BufferUsage::FragmentStorageWrite:
        case BufferUsage::ComputeStorageRead:
        case BufferUsage::ComputeStorageWrite:
            return vk::BufferUsageFlagBits::eStorageBuffer;
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
}

RenderGraph::RenderGraph(
    ImageRegistry& images,
    BufferRegistry& buffers,
    ThreadPool& thread_pool
) : images_(images),
    buffers_(buffers),
    thread_pool_(thread_pool) {}

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

    static_cast<void>(images_.image(resource));
    image_usages_[node_name].insert_or_assign(
        std::string{resource},
        usage
    );
    compiled_ = false;
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

    static_cast<void>(buffers_.buffer(resource));
    buffer_usages_[node_name].insert_or_assign(
        std::string{resource},
        usage
    );
    compiled_ = false;
}

auto RenderGraph::compile() -> void {
    compiled_ = false;
    execution_order_.clear();
    image_barriers_.clear();
    buffer_barriers_.clear();
    source_stages_.clear();
    destination_stages_.clear();

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

    for (const auto& node_name : execution_order_) {
        if (const auto iterator = image_usages_.find(node_name);
            iterator != image_usages_.end()) {
            for (const auto& [resource_name, usage] : iterator->second) {
                const auto& image = images_.image(resource_name);
                const auto required = required_image_flags(usage);
                if (required && (image.usage() & required) != required) {
                    throw std::logic_error(
                        "image '" + resource_name +
                        "' does not support its declared usage!"
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
                const auto& buffer = buffers_.buffer(resource_name);
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

    for (const auto& [resource_name, sequence] : image_sequences) {
        const auto& image = images_.image(resource_name);
        auto previous_usage = sequence.back().second;

        for (const auto& [node_name, usage] : sequence) {
            image_barriers_[node_name].push_back(
                vk::ImageMemoryBarrier{}
                    .setSrcAccessMask(image_access(previous_usage))
                    .setDstAccessMask(image_access(usage))
                    .setOldLayout(image_layout(previous_usage))
                    .setNewLayout(image_layout(usage))
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
            source_stages_[node_name] |= image_stage(previous_usage);
            destination_stages_[node_name] |= image_stage(usage);
            previous_usage = usage;
        }
    }

    for (const auto& [resource_name, sequence] : buffer_sequences) {
        const auto& buffer = buffers_.buffer(resource_name);
        auto previous_usage = sequence.back().second;

        for (const auto& [node_name, usage] : sequence) {
            buffer_barriers_[node_name].push_back(
                vk::BufferMemoryBarrier{}
                    .setSrcAccessMask(buffer_access(previous_usage))
                    .setDstAccessMask(buffer_access(usage))
                    .setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
                    .setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
                    .setBuffer(buffer.get())
                    .setOffset(0)
                    .setSize(buffer.size())
            );
            source_stages_[node_name] |= buffer_stage(previous_usage);
            destination_stages_[node_name] |= buffer_stage(usage);
            previous_usage = usage;
        }
    }

    first_record_ = true;
    compiled_ = true;
}

auto RenderGraph::record(
    vk::raii::CommandBuffer& primary_command_buffer
) -> void {
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

    std::vector<vk::Image> seen_images;
    std::vector<vk::Buffer> seen_buffers;
    const std::vector<vk::ImageMemoryBarrier> no_image_barriers;
    const std::vector<vk::BufferMemoryBarrier> no_buffer_barriers;

    for (std::size_t index = 0; index < execution_order_.size(); ++index) {
        const auto& node_name = execution_order_[index];
        std::vector<vk::ImageMemoryBarrier> initial_image_barriers;
        std::vector<vk::BufferMemoryBarrier> initial_buffer_barriers;

        const auto image_iterator = image_barriers_.find(node_name);
        const auto buffer_iterator = buffer_barriers_.find(node_name);

        const std::vector<vk::ImageMemoryBarrier>* image_barriers = nullptr;
        const std::vector<vk::BufferMemoryBarrier>* buffer_barriers = nullptr;

        if (image_iterator != image_barriers_.end()) {
            image_barriers = &image_iterator->second;
        }
        if (buffer_iterator != buffer_barriers_.end()) {
            buffer_barriers = &buffer_iterator->second;
        }

        if (first_record_ && image_barriers != nullptr) {
            initial_image_barriers.reserve(image_barriers->size());
            for (auto barrier : *image_barriers) {
                const bool first_use = std::find(
                    seen_images.begin(),
                    seen_images.end(),
                    barrier.image
                ) == seen_images.end();

                if (first_use) {
                    seen_images.push_back(barrier.image);
                    if (!has_write_access(barrier.dstAccessMask)) {
                        continue;
                    }
                    barrier.srcAccessMask = {};
                    barrier.oldLayout = vk::ImageLayout::eUndefined;
                }
                initial_image_barriers.push_back(barrier);
            }
            image_barriers = &initial_image_barriers;
        }

        if (first_record_ && buffer_barriers != nullptr) {
            initial_buffer_barriers.reserve(buffer_barriers->size());
            for (const auto& barrier : *buffer_barriers) {
                const bool first_use = std::find(
                    seen_buffers.begin(),
                    seen_buffers.end(),
                    barrier.buffer
                ) == seen_buffers.end();

                if (first_use) {
                    seen_buffers.push_back(barrier.buffer);
                    continue;
                }
                initial_buffer_barriers.push_back(barrier);
            }
            buffer_barriers = &initial_buffer_barriers;
        }

        const bool has_image_barriers = image_barriers != nullptr && !image_barriers->empty();
        const bool has_buffer_barriers = buffer_barriers != nullptr && !buffer_barriers->empty();

        if (has_image_barriers || has_buffer_barriers) {
            primary_command_buffer.pipelineBarrier(
                source_stages_.at(node_name),
                destination_stages_.at(node_name),
                {},
                {},
                has_buffer_barriers
                    ? *buffer_barriers
                    : no_buffer_barriers,
                has_image_barriers
                    ? *image_barriers
                    : no_image_barriers
            );
        }

        const std::array commands{secondary_command_buffers[index]};
        primary_command_buffer.executeCommands(commands);
    }

    first_record_ = false;
}
