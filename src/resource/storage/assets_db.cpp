#include "resource/storage/assets_db.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

#include "gfx/device/buffer_uploader.hpp"
#include "gfx/device/device.hpp"

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

    auto buffer_address(
        const Device& device,
        const Buffer& buffer
    ) -> vk::DeviceAddress {
        return device.logical_device().getBufferAddress(
            vk::BufferDeviceAddressInfo{
                .buffer = buffer.get()
            }
        );
    }

    auto acceleration_structure_properties(const Device& device)
        -> VkPhysicalDeviceAccelerationStructurePropertiesKHR {
        VkPhysicalDeviceAccelerationStructurePropertiesKHR properties{};
        properties.sType =
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_PROPERTIES_KHR;
        VkPhysicalDeviceProperties2 device_properties{};
        device_properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
        device_properties.pNext = &properties;
        vkGetPhysicalDeviceProperties2(
            *device.physical_device(),
            &device_properties
        );
        return properties;
    }

    auto aligned_address(
        vk::DeviceAddress address,
        vk::DeviceSize alignment
    ) -> vk::DeviceAddress {
        if (alignment == 0 || (alignment & (alignment - 1)) != 0) {
            throw std::runtime_error(
                "acceleration-structure scratch alignment is invalid"
            );
        }
        if (address >
            std::numeric_limits<vk::DeviceAddress>::max() - alignment + 1) {
            throw std::overflow_error(
                "acceleration-structure scratch address overflows"
            );
        }
        return (address + alignment - 1) & ~(alignment - 1);
    }
}

auto AssetsDB::add(std::unique_ptr<Texture> texture)
    -> ResourceId<Texture> {
    return textures_.add(std::move(texture));
}

auto AssetsDB::add(
    const MaterialData& data,
    MaterialTextures textures
) -> ResourceId<Material> {
    if (upload_state_ != UploadState::Collecting) {
        throw std::logic_error(
            "cannot add materials after resource upload starts"
        );
    }

    // Validate that every resolved texture id belongs to this database.
    static_cast<void>(textures_.query(textures.base_color));
    static_cast<void>(textures_.query(textures.metallic_roughness));
    static_cast<void>(textures_.query(textures.normal));
    static_cast<void>(textures_.query(textures.occlusion));
    static_cast<void>(textures_.query(textures.emissive));

    if (pending_material_data_.size() >= ResourceId<Material>::invalid_value) {
        throw std::length_error("material resource ids are exhausted");
    }
    const auto buffer_index =
        static_cast<uint32_t>(pending_material_data_.size());
    pending_material_data_.push_back(PendingMaterialData{
        .data = data,
        .textures = textures
    });

    ResourceId<Material> id;
    try {
        id = materials_.add(std::make_unique<Material>(Material{
            .buffer_index = buffer_index
        }));
    }
    catch (...) {
        pending_material_data_.pop_back();
        throw;
    }

    if (id.value() != buffer_index) {
        throw std::logic_error(
            "material resource id does not match its GPU buffer index"
        );
    }
    return id;
}

auto AssetsDB::add(MeshData data) -> ResourceId<Mesh> {
    if (upload_state_ != UploadState::Collecting) {
        throw std::logic_error("cannot add meshes after resource upload starts");
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

auto AssetsDB::upload(
    const MemoryAllocator& allocator,
    BufferUploader& uploader
) -> void {
    if (upload_state_ != UploadState::Collecting) {
        throw std::logic_error("resource uploads can only be enqueued once");
    }
    if (pending_mesh_data_.empty() && pending_material_data_.empty()) {
        upload_state_ = UploadState::Enqueued;
        return;
    }

    std::vector<GpuMaterial> material_data;
    material_data.reserve(pending_material_data_.size());
    for (const auto& pending : pending_material_data_) {
        material_data.push_back(make_gpu_material(
            pending.data,
            pending.textures
        ));
    }

    if (!pending_mesh_data_.empty()) {
        global_vertex_buffer_ = Buffer{
            allocator,
            BufferDesc{
                .size = buffer_size<Vertex>(vertex_count_),
                .usage = vk::BufferUsageFlagBits::eVertexBuffer |
                         vk::BufferUsageFlagBits::eShaderDeviceAddress |
                         vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR |
                         vk::BufferUsageFlagBits::eTransferDst,
                .memory = BufferMemoryUsage::GpuOnly
            }
        };
        global_index_buffer_ = Buffer{
            allocator,
            BufferDesc{
                .size = buffer_size<uint32_t>(index_count_),
                .usage = vk::BufferUsageFlagBits::eIndexBuffer |
                         vk::BufferUsageFlagBits::eShaderDeviceAddress |
                         vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR |
                         vk::BufferUsageFlagBits::eTransferDst,
                .memory = BufferMemoryUsage::GpuOnly
            }
        };
    }
    if (!pending_material_data_.empty()) {
        global_material_buffer_ = Buffer{
            allocator,
            BufferDesc{
                .size = buffer_size<GpuMaterial>(
                    material_data.size()
                ),
                .usage = vk::BufferUsageFlagBits::eStorageBuffer |
                         vk::BufferUsageFlagBits::eTransferDst,
                .memory = BufferMemoryUsage::GpuOnly
            }
        };
    }
    // Own the destination handles before enqueueing; retain them on failure.
    upload_state_ = UploadState::Enqueueing;

    for (size_t i = 0; i < pending_mesh_data_.size(); ++i) {
        auto& data = pending_mesh_data_[i];
        const auto& mesh = meshes_.query(ResourceId<Mesh>{static_cast<uint32_t>(i)});
        uploader.enqueue(
            data.vertices_.data(),
            buffer_size<Vertex>(mesh.vertex_count),
            global_vertex_buffer_,
            BufferUploadDesc{
                .destination_offset = buffer_size<Vertex>(mesh.vertex_offset),
                .destination_stage = vk::PipelineStageFlagBits::eVertexInput,
                .destination_access = vk::AccessFlagBits::eVertexAttributeRead
            }
        );
        uploader.enqueue(
            data.indices_.data(),
            buffer_size<uint32_t>(mesh.index_count),
            global_index_buffer_,
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

    if (!material_data.empty()) {
        uploader.enqueue(
            material_data.data(),
            buffer_size<GpuMaterial>(material_data.size()),
            global_material_buffer_,
            BufferUploadDesc{
                .destination_stage =
                    vk::PipelineStageFlagBits::eFragmentShader,
                .destination_access = vk::AccessFlagBits::eShaderRead
            }
        );
    }
    std::vector<PendingMaterialData>{}.swap(pending_material_data_);
    upload_state_ = UploadState::Enqueued;
}

auto AssetsDB::build_blas(
    const Device& device,
    const MemoryAllocator& allocator
) -> void {
    if (upload_state_ != UploadState::Enqueued) {
        throw std::logic_error(
            "resource uploads must be enqueued before building BLAS"
        );
    }
    if (blases_.size() != 0) {
        throw std::logic_error("BLAS resources can only be built once");
    }
    if (meshes_.size() == 0) {
        return;
    }
    if (!global_vertex_buffer_.get() || !global_index_buffer_.get()) {
        throw std::logic_error(
            "geometry buffers must exist before building BLAS"
        );
    }

    const auto format_properties = device.physical_device()
        .getFormatProperties(vk::Format::eR32G32B32Sfloat);
    if (!(format_properties.bufferFeatures &
          vk::FormatFeatureFlagBits::eAccelerationStructureVertexBufferKHR)) {
        throw std::runtime_error(
            "R32G32B32Sfloat cannot be used as acceleration-structure vertices"
        );
    }

    const auto properties = acceleration_structure_properties(device);
    const auto vertex_base = buffer_address(device, global_vertex_buffer_);
    const auto index_base = buffer_address(device, global_index_buffer_);
    if (vertex_base == 0 || index_base == 0) {
        throw std::runtime_error(
            "failed to acquire geometry buffer device addresses"
        );
    }

    const auto mesh_count = meshes_.size();
    std::vector<vk::AccelerationStructureGeometryKHR> geometries;
    std::vector<vk::AccelerationStructureBuildGeometryInfoKHR> build_infos;
    std::vector<vk::AccelerationStructureBuildRangeInfoKHR> ranges;
    std::vector<std::unique_ptr<Blas>> pending_blases;
    geometries.reserve(mesh_count);
    build_infos.reserve(mesh_count);
    ranges.reserve(mesh_count);
    pending_blases.reserve(mesh_count);

    vk::DeviceSize maximum_scratch_size = 0;
    for (std::size_t index = 0; index < mesh_count; ++index) {
        const auto mesh_id = ResourceId<Mesh>{static_cast<uint32_t>(index)};
        const auto& mesh = meshes_.query(mesh_id);
        if (mesh.vertex_count == 0 || mesh.index_count == 0 ||
            mesh.index_count % 3 != 0) {
            throw std::runtime_error(
                "BLAS mesh must contain indexed triangles"
            );
        }

        const uint32_t primitive_count = mesh.index_count / 3;
        if (primitive_count > properties.maxPrimitiveCount) {
            throw std::length_error(
                "BLAS primitive count exceeds the device limit"
            );
        }

        vk::DeviceOrHostAddressConstKHR vertex_data{};
        vertex_data.deviceAddress = vertex_base +
            buffer_size<Vertex>(mesh.vertex_offset);
        vk::DeviceOrHostAddressConstKHR index_data{};
        index_data.deviceAddress = index_base +
            buffer_size<uint32_t>(mesh.first_index);

        vk::AccelerationStructureGeometryTrianglesDataKHR triangles{};
        triangles
            .setVertexFormat(vk::Format::eR32G32B32Sfloat)
            .setVertexData(vertex_data)
            .setVertexStride(sizeof(Vertex))
            .setMaxVertex(mesh.vertex_count - 1)
            .setIndexType(vk::IndexType::eUint32)
            .setIndexData(index_data);

        vk::AccelerationStructureGeometryKHR geometry{};
        geometry
            .setGeometryType(vk::GeometryTypeKHR::eTriangles)
            .setFlags({});
        geometry.geometry.triangles = triangles;
        geometries.push_back(geometry);

        vk::AccelerationStructureBuildGeometryInfoKHR build_info{};
        build_info
            .setType(vk::AccelerationStructureTypeKHR::eBottomLevel)
            .setFlags(
                vk::BuildAccelerationStructureFlagBitsKHR::ePreferFastTrace
            )
            .setMode(vk::BuildAccelerationStructureModeKHR::eBuild)
            .setGeometryCount(1)
            .setPGeometries(&geometries.back());

        const std::array maximum_primitive_counts{primitive_count};
        const auto sizes =
            device.logical_device().getAccelerationStructureBuildSizesKHR(
                vk::AccelerationStructureBuildTypeKHR::eDevice,
                build_info,
                maximum_primitive_counts
            );
        maximum_scratch_size = std::max(
            maximum_scratch_size,
            sizes.buildScratchSize
        );

        Buffer storage{
            allocator,
            BufferDesc{
                .size = sizes.accelerationStructureSize,
                .usage =
                    vk::BufferUsageFlagBits::eAccelerationStructureStorageKHR |
                    vk::BufferUsageFlagBits::eShaderDeviceAddress,
                .memory = BufferMemoryUsage::GpuOnly
            }
        };
        auto handle = device.logical_device().createAccelerationStructureKHR(
            vk::AccelerationStructureCreateInfoKHR{
                .buffer = storage.get(),
                .offset = 0,
                .size = sizes.accelerationStructureSize,
                .type = vk::AccelerationStructureTypeKHR::eBottomLevel
            }
        );
        const auto address =
            device.logical_device().getAccelerationStructureAddressKHR(
                vk::AccelerationStructureDeviceAddressInfoKHR{
                    .accelerationStructure = *handle
                }
            );
        if (address == 0) {
            throw std::runtime_error(
                "failed to acquire a BLAS device address"
            );
        }

        build_info.setDstAccelerationStructure(*handle);
        build_infos.push_back(build_info);
        ranges.push_back(vk::AccelerationStructureBuildRangeInfoKHR{
            .primitiveCount = primitive_count,
            .primitiveOffset = 0,
            .firstVertex = 0,
            .transformOffset = 0
        });
        pending_blases.push_back(std::make_unique<Blas>(
            std::move(storage),
            std::move(handle),
            address
        ));
    }

    const auto scratch_alignment = static_cast<vk::DeviceSize>(
        properties.minAccelerationStructureScratchOffsetAlignment
    );
    if (maximum_scratch_size >
        std::numeric_limits<vk::DeviceSize>::max() - scratch_alignment + 1) {
        throw std::overflow_error("BLAS scratch buffer size overflows");
    }
    Buffer scratch{
        allocator,
        BufferDesc{
            .size = maximum_scratch_size + scratch_alignment - 1,
            .usage = vk::BufferUsageFlagBits::eStorageBuffer |
                vk::BufferUsageFlagBits::eShaderDeviceAddress,
            .memory = BufferMemoryUsage::GpuOnly
        }
    };
    const auto scratch_address = aligned_address(
        buffer_address(device, scratch),
        scratch_alignment
    );

    auto command_pool = device.logical_device().createCommandPool(
        vk::CommandPoolCreateInfo{
            .flags = vk::CommandPoolCreateFlagBits::eTransient,
            .queueFamilyIndex = device.graphics_family()
        }
    );
    auto command_buffers = device.logical_device().allocateCommandBuffers(
        vk::CommandBufferAllocateInfo{
            .commandPool = *command_pool,
            .level = vk::CommandBufferLevel::ePrimary,
            .commandBufferCount = 1
        }
    );
    auto& command_buffer = command_buffers.front();
    command_buffer.begin(vk::CommandBufferBeginInfo{
        .flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit
    });

    const std::array geometry_barriers{
        vk::BufferMemoryBarrier{}
            .setSrcAccessMask(vk::AccessFlagBits::eTransferWrite)
            .setDstAccessMask(
                vk::AccessFlagBits::eAccelerationStructureReadKHR
            )
            .setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
            .setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
            .setBuffer(global_vertex_buffer_.get())
            .setOffset(0)
            .setSize(global_vertex_buffer_.size()),
        vk::BufferMemoryBarrier{}
            .setSrcAccessMask(vk::AccessFlagBits::eTransferWrite)
            .setDstAccessMask(
                vk::AccessFlagBits::eAccelerationStructureReadKHR
            )
            .setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
            .setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
            .setBuffer(global_index_buffer_.get())
            .setOffset(0)
            .setSize(global_index_buffer_.size())
    };
    command_buffer.pipelineBarrier(
        vk::PipelineStageFlagBits::eTransfer,
        vk::PipelineStageFlagBits::eAccelerationStructureBuildKHR,
        {},
        {},
        geometry_barriers,
        {}
    );

    for (std::size_t index = 0; index < mesh_count; ++index) {
        build_infos[index].scratchData.deviceAddress = scratch_address;
        const std::array infos{build_infos[index]};
        const std::array range_pointers{&ranges[index]};
        command_buffer.buildAccelerationStructuresKHR(
            infos,
            range_pointers
        );

        if (index + 1 < mesh_count) {
            const std::array barriers{
                vk::MemoryBarrier{
                    .srcAccessMask =
                        vk::AccessFlagBits::eAccelerationStructureReadKHR |
                        vk::AccessFlagBits::eAccelerationStructureWriteKHR,
                    .dstAccessMask =
                        vk::AccessFlagBits::eAccelerationStructureReadKHR |
                        vk::AccessFlagBits::eAccelerationStructureWriteKHR
                }
            };
            command_buffer.pipelineBarrier(
                vk::PipelineStageFlagBits::eAccelerationStructureBuildKHR,
                vk::PipelineStageFlagBits::eAccelerationStructureBuildKHR,
                {},
                barriers,
                {},
                {}
            );
        }
    }

    command_buffer.end();
    const vk::CommandBuffer command_buffer_handle = *command_buffer;
    vk::SubmitInfo submit_info{};
    submit_info.setCommandBuffers(command_buffer_handle);
    auto fence = device.logical_device().createFence(vk::FenceCreateInfo{});
    device.graphics_queue().submit(submit_info, fence);
    static_cast<void>(device.logical_device().waitForFences(
        *fence,
        true,
        std::numeric_limits<uint64_t>::max()
    ));

    for (std::size_t index = 0; index < pending_blases.size(); ++index) {
        const auto id = blases_.add(std::move(pending_blases[index]));
        if (id.value() != index) {
            throw std::logic_error(
                "BLAS resource id does not match its mesh resource id"
            );
        }
    }
}

auto AssetsDB::vertex_buffer() const -> const Buffer& {
    if (upload_state_ != UploadState::Enqueued) {
        throw std::logic_error("resource uploads must be enqueued before drawing");
    }
    return global_vertex_buffer_;
}

auto AssetsDB::index_buffer() const -> const Buffer& {
    if (upload_state_ != UploadState::Enqueued) {
        throw std::logic_error("resource uploads must be enqueued before drawing");
    }
    return global_index_buffer_;
}

auto AssetsDB::material_buffer() const -> const Buffer& {
    if (upload_state_ != UploadState::Enqueued) {
        throw std::logic_error(
            "resource uploads must be enqueued before accessing materials"
        );
    }
    if (!global_material_buffer_.get()) {
        throw std::logic_error("the asset database contains no materials");
    }
    return global_material_buffer_;
}

auto AssetsDB::add(std::unique_ptr<Model> model) -> ResourceId<Model> {
    return models_.add(std::move(model));
}

auto AssetsDB::set_model_name(ResourceId<Model> id, std::string name) -> void {
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

auto AssetsDB::query(ResourceId<Texture> id) const -> const Texture& {
    return textures_.query(id);
}

auto AssetsDB::query(ResourceId<Material> id) const -> const Material& {
    return materials_.query(id);
}

auto AssetsDB::query(ResourceId<Mesh> id) const -> const Mesh& {
    return meshes_.query(id);
}

auto AssetsDB::query(ResourceId<Blas> id) const -> const Blas& {
    return blases_.query(id);
}

auto AssetsDB::query(ResourceId<Model> id) const -> const Model& {
    return models_.query(id);
}

auto AssetsDB::query_model_id(std::string_view name) const
    -> ResourceId<Model> {
    const auto name_entry = model_names_.find(std::string{name});
    if (name_entry == model_names_.end()) {
        throw std::out_of_range(
            "model name is not registered: " + std::string{name}
        );
    }
    return name_entry->second;
}

auto AssetsDB::query_model(std::string_view name) const -> const Model& {
    return query(query_model_id(name));
}

auto AssetsDB::contains_model(std::string_view name) const -> bool {
    return model_names_.contains(std::string{name});
}
