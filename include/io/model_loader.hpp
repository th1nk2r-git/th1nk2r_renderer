#ifndef MODEL_LOADER_HPP
#define MODEL_LOADER_HPP

#include "resource/cpu/model.hpp"
#include <filesystem>

auto load_model(const std::filesystem::path& path) -> ModelData;

#endif
