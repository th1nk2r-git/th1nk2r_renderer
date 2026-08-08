#include "gfx/pipeline/shader_module.hpp"
#include "io/spirv_loader.hpp"

#include <stdexcept>

namespace {
    auto create_shader_module(
        const Device& device,
        const std::filesystem::path& path) -> vk::raii::ShaderModule {
        const auto code = load_spirv(path);
        vk::ShaderModuleCreateInfo create_info{
            .codeSize = code.size() * sizeof(uint32_t),
            .pCode = code.data()};

        try {
            return device.logical_device().createShaderModule(create_info);
        } 
        catch (const vk::SystemError& error) {
            throw std::runtime_error(
                "failed to create shader module from '" + path.string() +
                "': " + error.what()
            );
        }
    }
}

ShaderModule::ShaderModule(
    const Device& device,
    const std::filesystem::path& path
) : handle_(create_shader_module(device, path)) {}
