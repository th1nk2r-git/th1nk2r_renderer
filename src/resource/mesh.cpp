#include "resource/gpu/mesh.hpp"

#include <array>
#include <limits>
#include <stdexcept>
#include <string>

namespace {
    template <typename T>
    auto buffer_size(const std::vector<T>& values, const char* name) -> vk::DeviceSize {
        if (values.empty()) {
            throw std::invalid_argument(
                std::string{"mesh requires at least one "} + name);
        }

        if (values.size() >
            std::numeric_limits<vk::DeviceSize>::max() / sizeof(T)) {
            throw std::length_error(
                std::string{"mesh "} + name + " data is too large");
        }

        return static_cast<vk::DeviceSize>(values.size()) * sizeof(T);
    }

    auto checked_index_count(const MeshData& data) -> uint32_t {
        if (data.indices_.empty()) {
            throw std::invalid_argument(
                "mesh requires at least one index");
        }

        if (data.indices_.size() > std::numeric_limits<uint32_t>::max()) {
            throw std::length_error(
                "mesh index count exceeds uint32_t");
        }

        return static_cast<uint32_t>(data.indices_.size());
    }
}

Mesh::Mesh(
    const MeshData& data,
    ResourceId<Material> material,
    const MemoryAllocator& allocator,
    BufferUploader& uploader
)
    : index_count_(checked_index_count(data)),
      material_(material),
      vertex_buffer_(
          allocator,
          BufferDesc{
              .size = buffer_size(data.vertices_, "vertex"),
              .usage = vk::BufferUsageFlagBits::eVertexBuffer |
                       vk::BufferUsageFlagBits::eTransferDst,
              .memory = BufferMemoryUsage::GpuOnly
            }
        ),
      index_buffer_(
          allocator,
          BufferDesc{
              .size = buffer_size(data.indices_, "index"),
              .usage = vk::BufferUsageFlagBits::eIndexBuffer |
                       vk::BufferUsageFlagBits::eTransferDst,
              .memory = BufferMemoryUsage::GpuOnly
            }
        ) {
    uploader.enqueue(
        data.vertices_.data(),
        vertex_buffer_.size(),
        vertex_buffer_,
        BufferUploadDesc{
            .destination_stage =
                vk::PipelineStageFlagBits::eVertexInput,
            .destination_access =
                vk::AccessFlagBits::eVertexAttributeRead
        }
    );
    uploader.enqueue(
        data.indices_.data(),
        index_buffer_.size(),
        index_buffer_,
        BufferUploadDesc{
            .destination_stage =
                vk::PipelineStageFlagBits::eVertexInput,
            .destination_access = vk::AccessFlagBits::eIndexRead
        }
    );
    uploader.submit_and_wait();
}

auto Mesh::bind(vk::raii::CommandBuffer& command_buffer) const -> void {
    const std::array vertex_buffers{
        vertex_buffer_.get()
    };
    constexpr std::array<vk::DeviceSize, 1> offsets{0};

    command_buffer.bindVertexBuffers(0, vertex_buffers, offsets);
    command_buffer.bindIndexBuffer(
        index_buffer_.get(),
        0,
        vk::IndexType::eUint32
    );
}
