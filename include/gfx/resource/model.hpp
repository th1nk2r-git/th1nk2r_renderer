#ifndef MODEL_HPP
#define MODEL_HPP

#include <cstddef>
#include <span>
#include <vector>

#include "gfx/resource/mesh.hpp"
#include "resource/model_data.hpp"

class Model {
public:
    Model() = delete;
    Model(const ModelData& data, const GpuAllocator& allocator, DataUploader& uploader);
    ~Model() = default;

    Model(const Model&) = delete;
    auto operator=(const Model&) -> Model& = delete;
    Model(Model&&) noexcept = default;
    auto operator=(Model&&) noexcept -> Model& = default;

    auto meshes() const noexcept -> std::span<const Mesh> {
        return std::span<const Mesh>{meshes_};
    }

private:
    std::vector<Mesh> meshes_;
};

#endif
