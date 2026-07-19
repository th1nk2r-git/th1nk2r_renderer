#ifndef SHADER_MODULE_HPP
#define SHADER_MODULE_HPP

#include <filesystem>
#include <vector>

#include "gfx/device/device.hpp"

class ShaderModule {
public:
    ShaderModule() = default;

    ShaderModule(const Device& device, const std::filesystem::path& path);

    // return the reference of the shader module
    auto get() const -> const vk::raii::ShaderModule& {
        return handle_;
    }

private:
    vk::raii::ShaderModule handle_ = nullptr;

    // read the file and return the byte code
    static auto load_spirv(const std::filesystem::path& path) -> std::vector<char>;
};

#endif
