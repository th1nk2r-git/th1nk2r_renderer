#include "gfx/pipeline/shader_module.hpp"

#include <cstdint>
#include <cstring>
#include <fstream>
#include <limits>
#include <stdexcept>

namespace {

constexpr uint32_t spirv_magic = 0x07230203;
constexpr size_t spirv_header_size = 5 * sizeof(uint32_t);

auto shader_error(
    const std::filesystem::path& path,
    const std::string& reason
) -> std::runtime_error {
    return std::runtime_error(
        "failed to load SPIR-V file '" + path.string() + "': " + reason
    );
}

}

ShaderModule::ShaderModule(const Device& device, const std::filesystem::path& path) {
    const auto code = load_spirv(path);
    std::vector<uint32_t> aligned_code(code.size() / sizeof(uint32_t));
    std::memcpy(aligned_code.data(), code.data(), code.size());

    vk::ShaderModuleCreateInfo create_info{
        .codeSize = aligned_code.size() * sizeof(uint32_t),
        .pCode = aligned_code.data()
    };

    try {
        handle_ = device.logical_device().createShaderModule(create_info);
    }
    catch (const vk::SystemError& error) {
        throw std::runtime_error(
            "failed to create shader module from '" + path.string() +
            "': " + error.what()
        );
    }
}

auto ShaderModule::load_spirv(const std::filesystem::path& path) -> std::vector<char> {
    std::ifstream file(path, std::ios::ate | std::ios::binary);
    if (!file.is_open()) {
        throw shader_error(path, "file could not be opened");
    }

    const auto end_position = file.tellg();
    if (end_position == std::ifstream::pos_type{-1}) {
        throw shader_error(path, "file size could not be determined!");
    }

    const auto file_size = static_cast<std::streamoff>(end_position);
    if (file_size < static_cast<std::streamoff>(spirv_header_size)) {
        throw shader_error(path, "file is smaller than the SPIR-V header!");
    }
    if (file_size % static_cast<std::streamoff>(sizeof(uint32_t)) != 0) {
        throw shader_error(path, "file size is not a multiple of 4 bytes!");
    }
    if (file_size > std::numeric_limits<std::streamsize>::max()) {
        throw shader_error(path, "file is too large to read!");
    }

    file.seekg(0, std::ios::beg);
    if (!file) {
        throw shader_error(path, "failed to seek to the beginning of the file!");
    }

    std::vector<char> buffer(file_size);
    if (!file.read(buffer.data(), static_cast<std::streamsize>(file_size))) {
        throw shader_error(path, "file could not be read completely!");
    }

    uint32_t magic = 0;
    std::memcpy(&magic, buffer.data(), sizeof(magic));
    if (magic != spirv_magic) {
        throw shader_error(path, "invalid SPIR-V magic number!");
    }

    return buffer;
}
