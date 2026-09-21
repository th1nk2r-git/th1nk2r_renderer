#ifndef BUFFER_REGISTRY_HPP
#define BUFFER_REGISTRY_HPP

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "gfx/resource/buffer.hpp"

class BufferRegistry {
public:
    auto add(std::string name, std::vector<Buffer> buffers) -> void;
    auto bind_external(std::string name, Buffer& buffer) -> void;

    auto query(std::string_view name) -> Buffer&;
    auto query(std::string_view name) const -> const Buffer&;
    auto query(std::string_view name, uint32_t instance) -> Buffer&;
    auto query(std::string_view name, uint32_t instance) const -> const Buffer&;

    auto contains(std::string_view name) const -> bool;
    auto instance_count(std::string_view name) const -> uint32_t;
    auto clear() -> void;

private:
    std::unordered_map<std::string, std::vector<Buffer>> buffers_;
    std::unordered_map<std::string, Buffer*> external_buffers_;
};

#endif
