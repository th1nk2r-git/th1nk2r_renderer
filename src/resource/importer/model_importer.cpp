#include "resource/importer/model_importer.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <unordered_set>
#include <utility>

#include "gfx/device/device_context.hpp"
#include "io/model_loader.hpp"
#include "resource/cpu/image.hpp"
#include "resource/gpu/material.hpp"
#include "resource/gpu/mesh.hpp"
#include "resource/gpu/model.hpp"
#include "resource/gpu/primitive.hpp"
#include "resource/gpu/texture.hpp"
#include "resource/storage/assets_db.hpp"

namespace {
    constexpr std::array white_texel{
        std::byte{255}, std::byte{255}, std::byte{255}, std::byte{255}
    };
    constexpr std::array flat_normal_texel{
        std::byte{128}, std::byte{128}, std::byte{255}, std::byte{255}
    };

    auto make_model_name(const std::filesystem::path& model_path) -> std::string {
        return std::filesystem::absolute(model_path)
            .lexically_normal()
            .parent_path()
            .filename()
            .string();
    }
}

ModelImporter::ModelImporter(
    DeviceContext& device_context,
    AssetsDB& assets
) : device_context_(device_context),
    assets_(assets) {
    const auto register_texel = [this](const std::array<std::byte, 4>& texel, TextureEncoding encoding) {
        return register_texture(
            ImageData{
                .width = 1,
                .height = 1,
                .channels = 4,
                .pixels = std::vector<std::byte>(texel.begin(), texel.end())
            },
            encoding
        );
    };

    white_srgb_ = register_texel(white_texel, TextureEncoding::Srgb);
    white_unorm_ = register_texel(white_texel, TextureEncoding::Unorm);
    flat_normal_ = register_texel(flat_normal_texel, TextureEncoding::Unorm);
}

auto ModelImporter::register_texture(const ImageData& data, TextureEncoding encoding) -> ResourceId<Texture> {
    if (data.channels != 4) {
        throw std::invalid_argument(
            "material texture must contain RGBA8 pixels"
        );
    }

    return assets_.add(
        std::make_unique<Texture>(
            device_context_.device(),
            device_context_.allocator(),
            device_context_.image_uploader(),
            data.width,
            data.height,
            data.pixels,
            encoding == TextureEncoding::Srgb ? vk::Format::eR8G8B8A8Srgb : vk::Format::eR8G8B8A8Unorm
        )
    );
}

auto ModelImporter::resolve_texture(
    const ModelData& model,
    std::optional<std::size_t> texture_index,
    TextureEncoding encoding,
    FallbackTexture fallback
) -> ResourceId<Texture> {
    if (!texture_index) {
        return fallback_texture(encoding, fallback);
    }

    const auto& data = model.textures_.at(*texture_index);
    auto& variants = textures_[data.source_];
    auto& texture_id = encoding == TextureEncoding::Srgb ? variants.srgb : variants.unorm;
    if (!texture_id.valid()) {
        texture_id = register_texture(data.image_, encoding);
    }
    return texture_id;
}

auto ModelImporter::fallback_texture(
    TextureEncoding encoding,
    FallbackTexture fallback
) const -> ResourceId<Texture> {
    if (fallback == FallbackTexture::FlatNormal) {
        return flat_normal_;
    }
    return encoding == TextureEncoding::Srgb ? white_srgb_ : white_unorm_;
}

auto ModelImporter::import_model(
    const std::filesystem::path& path,
    std::string name
) -> ModelImportResult {
    if (path.empty()) {
        throw std::invalid_argument("model path cannot be empty");
    }
    if (name.empty()) {
        name = make_model_name(path);
    }
    if (name.empty()) {
        throw std::invalid_argument("model name cannot be empty");
    }
    if (assets_.contains_model(name)) {
        throw std::invalid_argument(
            "model name is already registered: " + name
        );
    }

    auto data = load_model(path);
    if (data.material_.empty()) {
        throw std::runtime_error(
            "loaded model does not contain a local material list"
        );
    }

    ModelImportResult result;
    std::vector<std::optional<ResourceId<Material>>> material_ids(
        data.material_.size()
    );
    std::vector<Primitive> primitives;
    primitives.reserve(data.meshes_.size());

    for (auto& mesh_data : data.meshes_) {
        const auto material_index = mesh_data.material_index_;
        if (material_index >= data.material_.size()) {
            throw std::out_of_range(
                "mesh local material index is out of range"
            );
        }

        auto& material_id = material_ids[material_index];
        if (!material_id) {
            const auto& material_data = data.material_[material_index];
            material_id = assets_.add(
                std::make_unique<Material>(
                    material_data,
                    MaterialTextures{
                        .base_color = resolve_texture(
                            data,
                            material_data.base_color_texture_,
                            TextureEncoding::Srgb,
                            FallbackTexture::White
                        ),
                        .metallic_roughness = resolve_texture(
                            data,
                            material_data.metallic_roughness_texture_,
                            TextureEncoding::Unorm,
                            FallbackTexture::White
                        ),
                        .normal = resolve_texture(
                            data,
                            material_data.normal_texture_,
                            TextureEncoding::Unorm,
                            FallbackTexture::FlatNormal
                        ),
                        .occlusion = resolve_texture(
                            data,
                            material_data.occlusion_texture_,
                            TextureEncoding::Unorm,
                            FallbackTexture::White
                        ),
                        .emissive = resolve_texture(
                            data,
                            material_data.emissive_texture_,
                            TextureEncoding::Srgb,
                            FallbackTexture::White
                        )
                    }
                )
            );
            result.materials.push_back(*material_id);
        }

        const auto mesh_id = assets_.add(std::move(mesh_data));
        primitives.push_back(
            Primitive{
                .mesh = mesh_id,
                .material = *material_id
            }
        );
    }

    result.model = assets_.add(
        std::make_unique<Model>(std::move(primitives))
    );
    assets_.set_model_name(result.model, std::move(name));
    return result;
}

auto ModelImporter::import_models(
    const std::filesystem::path& root
) -> ModelImportBatchResult {
    std::vector<std::filesystem::path> model_paths;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(root)) {
        if (!entry.is_regular_file()) {
            continue;
        }
        auto extension = entry.path().extension().string();
        std::ranges::transform(
            extension,
            extension.begin(),
            [](unsigned char character) {
                return static_cast<char>(std::tolower(character));
            }
        );
        if (extension == ".obj" || extension == ".fbx" ||
            extension == ".gltf" || extension == ".glb") {
            model_paths.push_back(entry.path());
        }
    }

    std::ranges::sort(model_paths);
    ModelImportBatchResult result;
    result.models.reserve(model_paths.size());
    std::unordered_set<uint32_t> material_id_values;
    for (const auto& path : model_paths) {
        const auto imported = import_model(path);
        result.models.push_back(imported.model);
        for (const auto material_id : imported.materials) {
            if (material_id_values.insert(material_id.value()).second) {
                result.materials.push_back(material_id);
            }
        }
    }
    return result;
}
