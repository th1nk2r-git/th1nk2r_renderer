#include "gfx/resource/model.hpp"

#include <stdexcept>

Model::Model(const ModelData& data, const GpuAllocator& allocator, DataUploader& uploader) {
    if (data.meshes_.empty()) {
        throw std::invalid_argument(
            "model requires at least one mesh"
        );
    }

    meshes_.reserve(data.meshes_.size());
    for (const auto& mesh_data : data.meshes_) {
        meshes_.emplace_back(
            mesh_data,
            allocator,
            uploader
        );
    }
}
