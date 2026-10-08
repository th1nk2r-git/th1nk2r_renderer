#include "render/pass/tlas_build/tlas_build_pass.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>

#include <glm/mat4x4.hpp>
#include <glm/matrix.hpp>
#include <glm/vec4.hpp>

#include "gfx/device/device.hpp"
#include "gfx/device/memory_allocator.hpp"
#include "gfx/resource/buffer.hpp"
#include "render/render_graph.hpp"
#include "resource/gpu/blas.hpp"
#include "resource/gpu/material.hpp"
#include "resource/gpu/mesh.hpp"
#include "resource/gpu/model.hpp"
#include "resource/gpu/primitive.hpp"
#include "resource/storage/assets_db.hpp"
#include "scene/components/mesh_renderer.hpp"
#include "scene/components/transform.hpp"
#include "scene/scene.hpp"

namespace {
    struct alignas(16) GpuRayInstance {
        glm::vec4 model_column_0{1.0F, 0.0F, 0.0F, 0.0F};
        glm::vec4 model_column_1{0.0F, 1.0F, 0.0F, 0.0F};
        glm::vec4 model_column_2{0.0F, 0.0F, 1.0F, 0.0F};
        glm::vec4 normal_column_0{1.0F, 0.0F, 0.0F, 0.0F};
        glm::vec4 normal_column_1{0.0F, 1.0F, 0.0F, 0.0F};
        glm::vec4 normal_column_2{0.0F, 0.0F, 1.0F, 0.0F};
        uint32_t first_index = 0;
        int32_t vertex_offset = 0;
        uint32_t material_index = 0;
        uint32_t padding = 0;
    };

    static_assert(sizeof(GpuRayInstance) == 112);
    static_assert(offsetof(GpuRayInstance, first_index) == 96);

    constexpr auto build_flags =
        vk::BuildAccelerationStructureFlagBitsKHR::eAllowUpdate |
        vk::BuildAccelerationStructureFlagBitsKHR::ePreferFastTrace;

    enum class TlasOperation {
        None,
        Build,
        Update
    };

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

    auto instance_buffer_size(uint32_t count) -> vk::DeviceSize {
        static_assert(sizeof(VkAccelerationStructureInstanceKHR) == 64);
        if (count > std::numeric_limits<vk::DeviceSize>::max() /
                sizeof(VkAccelerationStructureInstanceKHR)) {
            throw std::overflow_error("TLAS instance buffer size overflows");
        }
        return static_cast<vk::DeviceSize>(count) *
            sizeof(VkAccelerationStructureInstanceKHR);
    }

    auto tlas_geometry(vk::DeviceAddress instance_address)
        -> vk::AccelerationStructureGeometryKHR {
        vk::DeviceOrHostAddressConstKHR instance_data{};
        instance_data.deviceAddress = instance_address;

        vk::AccelerationStructureGeometryInstancesDataKHR instances{};
        instances
            .setArrayOfPointers(false)
            .setData(instance_data);

        vk::AccelerationStructureGeometryKHR geometry{};
        geometry
            .setGeometryType(vk::GeometryTypeKHR::eInstances)
            .setFlags({});
        geometry.geometry.instances = instances;
        return geometry;
    }

    auto vk_transform(const glm::mat4& model) noexcept
        -> VkTransformMatrixKHR {
        VkTransformMatrixKHR transform{};
        for (uint32_t row = 0; row < 3; ++row) {
            for (uint32_t column = 0; column < 4; ++column) {
                transform.matrix[row][column] = model[column][row];
            }
        }
        return transform;
    }
}

struct TlasBuildPass::Impl {
    struct FrameSlot {
        Buffer instance_buffer;
        Buffer storage_buffer;
        Buffer scratch_buffer;
        vk::raii::AccelerationStructureKHR handle = nullptr;
        vk::DeviceAddress instance_address = 0;
        vk::DeviceAddress scratch_address = 0;
        vk::raii::CommandPool command_pool = nullptr;
        vk::raii::CommandBuffer command_buffer = nullptr;
        uint32_t instance_count = 0;
        uint32_t built_instance_count = 0;
        std::vector<VkAccelerationStructureInstanceKHR> built_instances;
        TlasOperation operation = TlasOperation::Build;
        bool built = false;
        bool prepared = false;
    };

    Impl(
        const MemoryAllocator& memory_allocator,
        const AssetsDB& assets_database
    ) : allocator(memory_allocator), assets(assets_database) {}

    const MemoryAllocator& allocator;
    const AssetsDB& assets;
    std::vector<FrameSlot> slots;
    std::vector<VkAccelerationStructureInstanceKHR> cpu_instances;
    std::vector<GpuRayInstance> cpu_ray_instances;
    bool initialized = false;
};

TlasBuildPass::TlasBuildPass(
    const Device& device,
    const MemoryAllocator& allocator,
    RenderGraph& render_graph,
    const AssetsDB& assets
) : RenderPass(std::string{pass_name}, device, render_graph),
    impl_(std::make_unique<Impl>(allocator, assets)) {}

TlasBuildPass::~TlasBuildPass() = default;

auto TlasBuildPass::declare_resources(RenderGraph& render_graph) -> void {
    render_graph.create_buffer(std::string{instance_resource}, BufferDesc{
        .size = sizeof(GpuRayInstance) * max_instance_count,
        .usage = vk::BufferUsageFlagBits::eStorageBuffer,
        .memory = BufferMemoryUsage::Upload,
        .persistent_mapping = true
    }, ResourceMultiplicity::PerFrame);
}

auto TlasBuildPass::init() -> void {
    if (impl_->initialized) {
        throw std::logic_error("TLAS build pass is already initialized");
    }

    const auto properties = acceleration_structure_properties(device_);
    if (max_instance_count > properties.maxInstanceCount) {
        throw std::runtime_error(
            "TLAS instance capacity exceeds the device limit"
        );
    }

    const auto scratch_alignment = static_cast<vk::DeviceSize>(
        properties.minAccelerationStructureScratchOffsetAlignment
    );
    if (scratch_alignment == 0 ||
        (scratch_alignment & (scratch_alignment - 1)) != 0) {
        throw std::runtime_error(
            "acceleration-structure scratch alignment is invalid"
        );
    }

    const auto geometry = tlas_geometry(0);
    vk::AccelerationStructureBuildGeometryInfoKHR build_info{};
    build_info
        .setType(vk::AccelerationStructureTypeKHR::eTopLevel)
        .setFlags(build_flags)
        .setMode(vk::BuildAccelerationStructureModeKHR::eBuild)
        .setGeometryCount(1)
        .setPGeometries(&geometry);
    const std::array maximum_primitive_counts{max_instance_count};
    const auto sizes =
        device_.logical_device().getAccelerationStructureBuildSizesKHR(
            vk::AccelerationStructureBuildTypeKHR::eDevice,
            build_info,
            maximum_primitive_counts
        );
    const auto scratch_size = std::max(
        sizes.buildScratchSize,
        sizes.updateScratchSize
    );
    if (scratch_size == 0 ||
        scratch_size > std::numeric_limits<vk::DeviceSize>::max() -
            scratch_alignment + 1) {
        throw std::overflow_error("TLAS scratch buffer size is invalid");
    }

    const auto frame_count = render_graph_.frames_in_flight_count();
    impl_->slots.reserve(frame_count);
    for (uint32_t index = 0; index < frame_count; ++index) {
        Impl::FrameSlot slot;
        slot.instance_buffer = Buffer{
            impl_->allocator,
            BufferDesc{
                .size = instance_buffer_size(max_instance_count),
                .usage =
                    vk::BufferUsageFlagBits::eShaderDeviceAddress |
                    vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR,
                .memory = BufferMemoryUsage::Upload,
                .persistent_mapping = true
            }
        };
        slot.instance_address = buffer_address(
            device_,
            slot.instance_buffer
        );
        if (slot.instance_address == 0) {
            throw std::runtime_error(
                "failed to acquire TLAS instance buffer address"
            );
        }

        slot.storage_buffer = Buffer{
            impl_->allocator,
            BufferDesc{
                .size = sizes.accelerationStructureSize,
                .usage =
                    vk::BufferUsageFlagBits::eAccelerationStructureStorageKHR |
                    vk::BufferUsageFlagBits::eShaderDeviceAddress,
                .memory = BufferMemoryUsage::GpuOnly
            }
        };
        slot.handle = device_.logical_device().createAccelerationStructureKHR(
            vk::AccelerationStructureCreateInfoKHR{
                .buffer = slot.storage_buffer.get(),
                .offset = 0,
                .size = sizes.accelerationStructureSize,
                .type = vk::AccelerationStructureTypeKHR::eTopLevel
            }
        );

        slot.scratch_buffer = Buffer{
            impl_->allocator,
            BufferDesc{
                .size = scratch_size + scratch_alignment - 1,
                .usage = vk::BufferUsageFlagBits::eStorageBuffer |
                    vk::BufferUsageFlagBits::eShaderDeviceAddress,
                .memory = BufferMemoryUsage::GpuOnly
            }
        };
        slot.scratch_address = aligned_address(
            buffer_address(device_, slot.scratch_buffer),
            scratch_alignment
        );
        if (slot.scratch_address == 0) {
            throw std::runtime_error(
                "failed to acquire TLAS scratch buffer address"
            );
        }

        slot.command_pool = device_.logical_device().createCommandPool(
            vk::CommandPoolCreateInfo{
                .flags = vk::CommandPoolCreateFlagBits::eTransient,
                .queueFamilyIndex = device_.graphics_family()
            }
        );
        auto command_buffers = device_.logical_device().allocateCommandBuffers(
            vk::CommandBufferAllocateInfo{
                .commandPool = *slot.command_pool,
                .level = vk::CommandBufferLevel::eSecondary,
                .commandBufferCount = 1
            }
        );
        slot.command_buffer = std::move(command_buffers.front());
        impl_->slots.push_back(std::move(slot));
    }

    impl_->cpu_instances.reserve(max_instance_count);
    impl_->cpu_ray_instances.reserve(max_instance_count);
    impl_->initialized = true;
}

auto TlasBuildPass::configure(RenderGraph&) -> void {
}

auto TlasBuildPass::prepare(const Scene& scene) -> void {
    if (!impl_->initialized) {
        throw std::logic_error(
            "TLAS build pass must be initialized before preparation"
        );
    }

    const auto frame_index = render_graph_.current_frame_index();
    auto& slot = impl_->slots.at(frame_index);
    impl_->cpu_instances.clear();
    impl_->cpu_ray_instances.clear();
    for (const auto& entity : scene.entities()) {
        const auto* mesh_renderer = entity.get_component<MeshRenderer>();
        if (mesh_renderer == nullptr) {
            continue;
        }
        const auto* transform = entity.get_component<Transform>();
        const auto model = transform == nullptr
            ? glm::mat4{1.0F}
            : transform->model_matrix();
        const auto normal_matrix = glm::transpose(glm::inverse(glm::mat3{model}));
        const auto& render_model = impl_->assets.query(
            mesh_renderer->model_id()
        );

        for (const auto& primitive : render_model.primitives()) {
            if (impl_->cpu_instances.size() >= max_instance_count) {
                throw std::length_error(
                    "TLAS instance count exceeds its capacity"
                );
            }

            const auto blas_id = ResourceId<Blas>{primitive.mesh.value()};
            const auto& blas = impl_->assets.query(blas_id);
            const auto& mesh = impl_->assets.query(primitive.mesh);
            const auto& material = impl_->assets.query(primitive.material);
            const auto instance_index = static_cast<uint32_t>(
                impl_->cpu_instances.size()
            );
            VkAccelerationStructureInstanceKHR instance{};
            instance.transform = vk_transform(model);
            instance.instanceCustomIndex = instance_index;
            instance.mask = 0xFF;
            instance.instanceShaderBindingTableRecordOffset = 0;
            instance.flags = material.alpha_mask ? 0 : VK_GEOMETRY_INSTANCE_FORCE_OPAQUE_BIT_KHR;
            instance.accelerationStructureReference = blas.address;
            impl_->cpu_instances.push_back(instance);
            impl_->cpu_ray_instances.push_back(GpuRayInstance{
                .model_column_0 = model[0],
                .model_column_1 = model[1],
                .model_column_2 = model[2],
                .normal_column_0 = glm::vec4{normal_matrix[0], 0.0F},
                .normal_column_1 = glm::vec4{normal_matrix[1], 0.0F},
                .normal_column_2 = glm::vec4{normal_matrix[2], 0.0F},
                .first_index = mesh.first_index,
                .vertex_offset = mesh.vertex_offset,
                .material_index = material.buffer_index
            });
        }
    }
    const auto instance_count = static_cast<uint32_t>(
        impl_->cpu_instances.size()
    );
    if (instance_count != 0) {
        render_graph_.buffer(instance_resource).write(impl_->cpu_ray_instances.data(), sizeof(GpuRayInstance) * instance_count);
    }
    slot.instance_count = instance_count;
    const bool unchanged = slot.built && slot.built_instances.size() == impl_->cpu_instances.size() &&
        std::equal(slot.built_instances.begin(), slot.built_instances.end(), impl_->cpu_instances.begin(),
            [](const auto& first, const auto& second) { return std::memcmp(&first, &second, sizeof(first)) == 0; });
    slot.operation = unchanged ? TlasOperation::None :
        slot.built && slot.built_instance_count == instance_count ? TlasOperation::Update : TlasOperation::Build;
    if (!unchanged && instance_count != 0) {
        slot.instance_buffer.write(impl_->cpu_instances.data(), instance_buffer_size(instance_count));
    }
    slot.prepared = true;
}

auto TlasBuildPass::record() -> vk::CommandBuffer {
    if (!impl_->initialized) {
        throw std::logic_error(
            "TLAS build pass must be initialized before recording"
        );
    }

    const auto frame_index = render_graph_.current_frame_index();
    auto& slot = impl_->slots.at(frame_index);
    if (!slot.prepared) {
        throw std::logic_error(
            "TLAS build pass must be prepared before recording"
        );
    }

    slot.command_pool.reset();
    vk::CommandBufferInheritanceInfo inheritance{};
    vk::CommandBufferBeginInfo begin_info{};
    begin_info
        .setFlags(vk::CommandBufferUsageFlagBits::eOneTimeSubmit)
        .setPInheritanceInfo(&inheritance);
    slot.command_buffer.begin(begin_info);
#ifndef NDEBUG
    insert_debug_marker(slot.command_buffer, name().data());
#endif

    if (slot.operation == TlasOperation::None) {
        slot.command_buffer.end();
        slot.prepared = false;
        return *slot.command_buffer;
    }

    const auto geometry = tlas_geometry(slot.instance_address);
    vk::AccelerationStructureBuildGeometryInfoKHR build_info{};
    build_info
        .setType(vk::AccelerationStructureTypeKHR::eTopLevel)
        .setFlags(build_flags)
        .setMode(
            slot.operation == TlasOperation::Update
            ? vk::BuildAccelerationStructureModeKHR::eUpdate
            : vk::BuildAccelerationStructureModeKHR::eBuild
        )
        .setSrcAccelerationStructure(
            slot.operation == TlasOperation::Update
            ? *slot.handle
            : vk::AccelerationStructureKHR{}
        )
        .setDstAccelerationStructure(*slot.handle)
        .setGeometryCount(1)
        .setPGeometries(&geometry);
    build_info.scratchData.deviceAddress = slot.scratch_address;

    const vk::AccelerationStructureBuildRangeInfoKHR range{
        .primitiveCount = slot.instance_count,
        .primitiveOffset = 0,
        .firstVertex = 0,
        .transformOffset = 0
    };
    const std::array build_infos{build_info};
    const std::array range_pointers{&range};
    slot.command_buffer.buildAccelerationStructuresKHR(
        build_infos,
        range_pointers
    );

    const std::array tlas_barriers{
        vk::MemoryBarrier{
            .srcAccessMask =
                vk::AccessFlagBits::eAccelerationStructureWriteKHR,
            .dstAccessMask =
                vk::AccessFlagBits::eAccelerationStructureReadKHR
        }
    };
    slot.command_buffer.pipelineBarrier(
        vk::PipelineStageFlagBits::eAccelerationStructureBuildKHR,
        vk::PipelineStageFlagBits::eRayTracingShaderKHR,
        {},
        tlas_barriers,
        {},
        {}
    );
    slot.command_buffer.end();
    slot.built = true;
    slot.built_instance_count = slot.instance_count;
    slot.built_instances = impl_->cpu_instances;
    slot.prepared = false;
    return *slot.command_buffer;
}

auto TlasBuildPass::handle(uint32_t frame_index) const
    -> const vk::raii::AccelerationStructureKHR& {
    if (!impl_->initialized) {
        throw std::logic_error(
            "TLAS build pass must be initialized before querying a handle"
        );
    }
    return impl_->slots.at(frame_index).handle;
}
