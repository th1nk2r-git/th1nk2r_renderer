#ifndef SPIRV_LOADER_HPP
#define SPIRV_LOADER_HPP

#include <cstdint>
#include <filesystem>
#include <vector>

auto load_spirv(const std::filesystem::path& path) -> std::vector<uint32_t>;

#endif
