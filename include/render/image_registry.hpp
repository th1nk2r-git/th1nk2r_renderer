#ifndef IMAGE_REGISTRY_HPP
#define IMAGE_REGISTRY_HPP

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "gfx/resource/image.hpp"

class ImageRegistry {
public:
    auto add(std::string name, std::vector<Image> images) -> void;
    auto bind_external(std::string name, Image& image) -> void;

    auto query(std::string_view name) -> Image&;
    auto query(std::string_view name) const -> const Image&;
    auto query(std::string_view name, uint32_t instance) -> Image&;
    auto query(std::string_view name, uint32_t instance) const -> const Image&;

    auto contains(std::string_view name) const -> bool;
    auto instance_count(std::string_view name) const -> uint32_t;
    auto clear() -> void;

private:
    std::unordered_map<std::string, std::vector<Image>> images_;
    std::unordered_map<std::string, Image*> external_images_;
};

#endif
