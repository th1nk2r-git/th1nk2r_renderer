#ifndef IMAGE_REGISTRY_HPP
#define IMAGE_REGISTRY_HPP

#include <string>
#include <string_view>
#include <unordered_map>

#include "gfx/resource/image.hpp"

class ImageRegistry {
public:
    auto add(std::string name, Image image) -> void;

    auto image(std::string_view name) -> Image&;
    auto image(std::string_view name) const -> const Image&;

private:
    std::unordered_map<std::string, Image> images_;
};

#endif
