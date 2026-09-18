#ifndef BUFFER_REGISTRY_HPP
#define BUFFER_REGISTRY_HPP

#include <string>
#include <string_view>
#include <unordered_map>

#include "gfx/resource/buffer.hpp"

class BufferRegistry {
public:
    auto add(std::string name, Buffer buffer) -> void;

    auto buffer(std::string_view name) -> Buffer&;
    auto buffer(std::string_view name) const -> const Buffer&;

private:
    std::unordered_map<std::string, Buffer> buffers_;
};

#endif
