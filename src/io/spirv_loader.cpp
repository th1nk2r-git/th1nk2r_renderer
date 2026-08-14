#include "io/spirv_loader.hpp"

#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>

namespace {
    constexpr uint32_t spirv_magic = 0x07230203;
    constexpr size_t spirv_header_size = 5 * sizeof(uint32_t);

    auto shader_error(
        const std::filesystem::path& path,
        const std::string& reason
    ) -> std::runtime_error {
        return std::runtime_error(
            "failed to load SPIR-V file '" + path.string() +
            "': " + reason
        );
    }
}

auto load_spirv(const std::filesystem::path& path)
    -> std::vector<uint32_t> {
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

    const auto word_count =
        static_cast<size_t>(file_size) / sizeof(uint32_t);
    std::vector<uint32_t> buffer(word_count);
    if (!file.read(
            reinterpret_cast<char*>(buffer.data()),
            static_cast<std::streamsize>(file_size))) {
        throw shader_error(path, "file could not be read completely!");
    }

    if (buffer.front() != spirv_magic) {
        throw shader_error(path, "invalid SPIR-V magic number!");
    }

    return buffer;
}
