#include "resource/registry/resource_registry.hpp"

#include <algorithm>
#include <limits>
#include <optional>
#include <stdexcept>
#include <utility>

#include "gfx/device/buffer_uploader.hpp"

namespace {
    auto make_mesh(
        const MeshData& data,
        uint64_t vertex_offset,
        uint64_t first_index
    ) -> Mesh {
        if (data.vertices_.empty() || data.indices_.empty()) {
            throw std::invalid_argument("mesh requires vertices and indices");
        }
        if (vertex_offset > std::numeric_limits<int32_t>::max() ||
            data.vertices_.size() >
                std::numeric_limits<uint32_t>::max() - vertex_offset) {
            throw std::length_error("mesh vertex range exceeds draw limits");
        }
        if (first_index > std::numeric_limits<uint32_t>::max() ||
            data.indices_.size() >
                std::numeric_limits<uint32_t>::max() - first_index) {
            throw std::length_error("mesh index range exceeds uint32_t");
        }
        if (std::any_of(data.indices_.begin(), data.indices_.end(),
                        [&data](uint32_t index) {
                            return index >= data.vertices_.size();
                        })) {
            throw std::out_of_range("mesh index exceeds its local vertex range");
        }

        const auto& position = data.vertices_.front().position;
        Mesh mesh{
            .vertex_offset = static_cast<int32_t>(vertex_offset),
            .vertex_count = static_cast<uint32_t>(data.vertices_.size()),
            .first_index = static_cast<uint32_t>(first_index),
            .index_count = static_cast<uint32_t>(data.indices_.size()),
            .bounds = {
                .minimum = {position[0], position[1], position[2]},
                .maximum = {position[0], position[1], position[2]}
            }
        };
        for (const auto& vertex : data.vertices_) {
            for (size_t axis = 0; axis < 3; ++axis) {
                mesh.bounds.minimum[axis] = std::min(
                    mesh.bounds.minimum[axis], vertex.position[axis]
                );
                mesh.bounds.maximum[axis] = std::max(
                    mesh.bounds.maximum[axis], vertex.position[axis]
                );
            }
        }
        return mesh;
    }

    template <typename T>
    auto buffer_size(uint64_t count) -> vk::DeviceSize {
        if (count > std::numeric_limits<vk::DeviceSize>::max() / sizeof(T)) {
            throw std::length_error("geometry buffer size exceeds vk::DeviceSize");
        }
        return static_cast<vk::DeviceSize>(count) * sizeof(T);
    }
}

auto ResourceRegistry::add(std::unique_ptr<Texture> texture)
    -> ResourceId<Texture> {
    return textures_.add(std::move(texture));
}

auto ResourceRegistry::add(std::unique_ptr<Material> material) -> ResourceId<Material> {
    return materials_.add(std::move(material));
}

auto ResourceRegistry::add(MeshData data) -> ResourceId<Mesh> {
    if (geometry_state_ != GeometryState::Collecting) {
        throw std::logic_error("cannot add meshes after geometry upload starts");
    }
    const auto mesh = make_mesh(data, vertex_count_, index_count_);
    pending_mesh_data_.push_back(std::move(data));
    ResourceId<Mesh> id;
    try {
        id = meshes_.add(std::make_unique<Mesh>(mesh));
    } catch (...) {
        pending_mesh_data_.pop_back();
        throw;
    }
    vertex_count_ += mesh.vertex_count;
    index_count_ += mesh.index_count;
    return id;
}

auto ResourceRegistry::upload_meshes(
    const MemoryAllocator& allocator,
    BufferUploader& uploader
) -> void {
    if (geometry_state_ != GeometryState::Collecting) {
        throw std::logic_error("mesh uploads can only be enqueued once");
    }
    if (pending_mesh_data_.empty()) {
        geometry_state_ = GeometryState::Enqueued;
        return;
    }

    auto vertex_buffer = Buffer{
        allocator,
        BufferDesc{
            .size = buffer_size<Vertex>(vertex_count_),
            .usage = vk::BufferUsageFlagBits::eVertexBuffer |
                     vk::BufferUsageFlagBits::eTransferDst,
            .memory = BufferMemoryUsage::GpuOnly
        }
    };
    auto index_buffer = Buffer{
        allocator,
        BufferDesc{
            .size = buffer_size<uint32_t>(index_count_),
            .usage = vk::BufferUsageFlagBits::eIndexBuffer |
                     vk::BufferUsageFlagBits::eTransferDst,
            .memory = BufferMemoryUsage::GpuOnly
        }
    };
    vertex_buffer_ = std::move(vertex_buffer);
    index_buffer_ = std::move(index_buffer);
    // Own the destination handles before enqueueing; retain them on failure.
    geometry_state_ = GeometryState::Enqueueing;

    for (size_t i = 0; i < pending_mesh_data_.size(); ++i) {
        auto& data = pending_mesh_data_[i];
        const auto& mesh = meshes_.query(ResourceId<Mesh>{static_cast<uint32_t>(i)});
        uploader.enqueue(
            data.vertices_.data(),
            buffer_size<Vertex>(mesh.vertex_count),
            vertex_buffer_,
            BufferUploadDesc{
                .destination_offset = buffer_size<Vertex>(mesh.vertex_offset),
                .destination_stage = vk::PipelineStageFlagBits::eVertexInput,
                .destination_access = vk::AccessFlagBits::eVertexAttributeRead
            }
        );
        uploader.enqueue(
            data.indices_.data(),
            buffer_size<uint32_t>(mesh.index_count),
            index_buffer_,
            BufferUploadDesc{
                .destination_offset = buffer_size<uint32_t>(mesh.first_index),
                .destination_stage = vk::PipelineStageFlagBits::eVertexInput,
                .destination_access = vk::AccessFlagBits::eIndexRead
            }
        );
        // Staging owns both copies now; release each mesh before staging the next.
        data = MeshData{};
    }
    // enqueue() copies source data into staging buffers immediately.
    std::vector<MeshData>{}.swap(pending_mesh_data_);
    geometry_state_ = GeometryState::Enqueued;
}

auto ResourceRegistry::vertex_buffer() const -> const Buffer& {
    if (geometry_state_ != GeometryState::Enqueued) {
        throw std::logic_error("geometry uploads must be enqueued before drawing");
    }
    return vertex_buffer_;
}

auto ResourceRegistry::index_buffer() const -> const Buffer& {
    if (geometry_state_ != GeometryState::Enqueued) {
        throw std::logic_error("geometry uploads must be enqueued before drawing");
    }
    return index_buffer_;
}

auto ResourceRegistry::add(std::unique_ptr<Model> model) -> ResourceId<Model> {
    return models_.add(std::move(model));
}

auto ResourceRegistry::set_model_name(ResourceId<Model> id, std::string name) -> void {
    if (name.empty()) {
        throw std::invalid_argument("model name cannot be empty");
    }
    static_cast<void>(models_.query(id));

    const auto name_entry = model_names_.find(name);
    if (name_entry != model_names_.end()) {
        if (name_entry->second == id) {
            return;
        }
        throw std::invalid_argument(
            "model name is already registered: " + name_entry->first
        );
    }

    std::optional<std::string> previous_name;
    for (const auto& [registered_name, registered_id] : model_names_) {
        if (registered_id == id) {
            previous_name = registered_name;
            break;
        }
    }

    model_names_.emplace(std::move(name), id);
    if (previous_name) {
        model_names_.erase(*previous_name);
    }
}

auto ResourceRegistry::query(ResourceId<Texture> id) const -> const Texture& {
    return textures_.query(id);
}

auto ResourceRegistry::query(ResourceId<Material> id) const -> const Material& {
    return materials_.query(id);
}

auto ResourceRegistry::query(ResourceId<Mesh> id) const -> const Mesh& {
    return meshes_.query(id);
}

auto ResourceRegistry::query(ResourceId<Model> id) const -> const Model& {
    return models_.query(id);
}

auto ResourceRegistry::query_model_id(std::string_view name) const
    -> ResourceId<Model> {
    const auto name_entry = model_names_.find(std::string{name});
    if (name_entry == model_names_.end()) {
        throw std::out_of_range(
            "model name is not registered: " + std::string{name}
        );
    }
    return name_entry->second;
}

auto ResourceRegistry::query_model(std::string_view name) const -> const Model& {
    return query(query_model_id(name));
}

auto ResourceRegistry::contains_model(std::string_view name) const -> bool {
    return model_names_.contains(std::string{name});
}
