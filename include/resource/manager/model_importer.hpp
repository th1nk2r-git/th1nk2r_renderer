#ifndef MODEL_IMPORTER_HPP
#define MODEL_IMPORTER_HPP

#include <filesystem>
#include <string>

#include "resource/manager/resource_id.hpp"

class DataUploader;
class DescriptorPool;
class DescriptorSetLayout;
class Device;
class GpuAllocator;
class Model;
class ResourceRegistry;

struct ImportContext {
    const Device& device;
    const GpuAllocator& allocator;
    DataUploader& uploader;
    const DescriptorSetLayout& material_descriptor_set_layout;
};

class ModelImporter {
public:
    ModelImporter() = delete;
    explicit ModelImporter(ImportContext context) noexcept;

    ModelImporter(const ModelImporter&) = delete;
    auto operator=(const ModelImporter&) -> ModelImporter& = delete;
    ModelImporter(ModelImporter&&) = delete;
    auto operator=(ModelImporter&&) -> ModelImporter& = delete;

    auto import(
        const std::filesystem::path& path,
        std::string name,
        ResourceRegistry& registry,
        DescriptorPool& material_descriptor_pool
    ) -> ResourceId<Model>;

private:
    ImportContext context_;
};

#endif
