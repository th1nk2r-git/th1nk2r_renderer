#include "gfx/pipeline/shader_module.hpp"

#include <cstdint>
#include <fstream>

ShaderModule::ShaderModule(const Device& device, const std::filesystem::path& path) {
    auto code = load_spirv(path);
    vk::ShaderModuleCreateInfo create_info{
        .codeSize = code.size(),
        .pCode = reinterpret_cast<const uint32_t*>(code.data())};

    handle_ = device.logical_device().createShaderModule(create_info);
}


auto ShaderModule::load_spirv(const std::filesystem::path& path) -> std::vector<char> {
    std::fstream file(path, std::ios::ate | std::ios::binary);
    size_t file_size = file.tellg();
    file.seekg(0);
    std::vector<char> buffer(file_size);
    file.read(buffer.data(), file_size);
    return buffer;
}
