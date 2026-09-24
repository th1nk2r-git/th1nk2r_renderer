#ifndef MODEL_IMPORTER_HPP
#define MODEL_IMPORTER_HPP

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "resource/gpu/resource_id.hpp"

class DeviceContext;
struct Material;
class Model;
class AssetsDB;
class Texture;
struct ImageData;
struct ModelData;

struct ModelImportResult {
    ResourceId<Model> model;
    std::vector<ResourceId<Material>> materials;
};

struct ModelImportBatchResult {
    std::vector<ResourceId<Model>> models;
    std::vector<ResourceId<Material>> materials;
};

class ModelImporter {
public:
    ModelImporter(
        DeviceContext& device_context,
        AssetsDB& assets
    );

    ModelImporter(const ModelImporter&) = delete;
    auto operator=(const ModelImporter&) -> ModelImporter& = delete;
    ModelImporter(ModelImporter&&) = delete;
    auto operator=(ModelImporter&&) -> ModelImporter& = delete;

    // Imports store CPU geometry/material data and enqueue texture uploads.
    // After all imports, call assets.upload(), then submit uploads before
    // rendering.
    // The default name is the model's immediate parent directory name.
    auto import_model(
        const std::filesystem::path& path,
        std::string name = {}
    ) -> ModelImportResult;

    auto import_models(const std::filesystem::path& root) -> ModelImportBatchResult;

private:
    enum class TextureEncoding {
        Srgb,
        Unorm
    };

    enum class FallbackTexture {
        White,
        FlatNormal
    };

    struct TextureVariants {
        ResourceId<Texture> srgb;
        ResourceId<Texture> unorm;
    };

    DeviceContext& device_context_;
    AssetsDB& assets_;
    std::unordered_map<std::string, TextureVariants> textures_;
    ResourceId<Texture> white_srgb_;
    ResourceId<Texture> white_unorm_;
    ResourceId<Texture> flat_normal_;

    auto register_texture(
        const ImageData& data,
        TextureEncoding encoding
    ) -> ResourceId<Texture>;

    auto resolve_texture(
        const ModelData& model,
        std::optional<std::size_t> texture_index,
        TextureEncoding encoding,
        FallbackTexture fallback
    ) -> ResourceId<Texture>;

    auto fallback_texture(
        TextureEncoding encoding,
        FallbackTexture fallback
    ) const -> ResourceId<Texture>;
};

#endif
