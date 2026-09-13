#ifndef TEXTURE_DATA_HPP
#define TEXTURE_DATA_HPP

#include <string>

#include "resource/cpu/image.hpp"

struct TextureData {
    std::string source_;
    ImageData image_;
};

#endif
