#include "resource/manager/resource_manager.hpp"

#include <array>
#include <utility>

namespace {
    constexpr uint32_t max_material_count = 1024;

    auto create_material_descriptor_pool(
        const Device& device
    ) -> DescriptorPool {
        vk::DescriptorPoolSize sampled_image_pool_size{};
        sampled_image_pool_size
            .setType(vk::DescriptorType::eSampledImage)
            .setDescriptorCount(max_material_count);

        vk::DescriptorPoolSize sampler_pool_size{};
        sampler_pool_size
            .setType(vk::DescriptorType::eSampler)
            .setDescriptorCount(max_material_count);

        const std::array pool_sizes{
            sampled_image_pool_size,
            sampler_pool_size
        };
        return DescriptorPool{
            device,
            max_material_count,
            pool_sizes
        };
    }
}

ResourceManager::ResourceManager(ImportContext context)
    : material_descriptor_pool_(
          create_material_descriptor_pool(context.device)
      ),
      importer_(context) {}

auto ResourceManager::import_model(
    const std::filesystem::path& path,
    std::string name
) -> ResourceId<Model> {
    return importer_.import(
        path,
        std::move(name),
        registry_,
        material_descriptor_pool_
    );
}

auto ResourceManager::query(ResourceId<Material> id) const -> const Material& {
    return registry_.query(id);
}

auto ResourceManager::query(ResourceId<Model> id) const -> const Model& {
    return registry_.query(id);
}

auto ResourceManager::query_model(std::string_view name) const -> const Model& {
    return registry_.query_model(name);
}

auto ResourceManager::contains_model(std::string_view name) const -> bool {
    return registry_.contains_model(name);
}
