#ifndef INSTANCE_HPP
#define INSTANCE_HPP

#define VULKAN_HPP_NO_STRUCT_CONSTRUCTORS

#include <vulkan/vulkan_raii.hpp>
#include <optional>

class Instance {
public:
    Instance() = default;

    // create the vulkan instance
    auto create() -> void;

    // return the reference of the vulkan instance
    auto get() const -> const vk::raii::Instance& {
        return handle_;
    }


private:

    vk::raii::Context dispatcher;

    vk::raii::Instance handle_ = nullptr;

#ifdef NDEBUG
    bool validation_layers_enabled = false;
#else 
    bool validation_layers_enabled = true;
#endif

    const std::vector<const char*> validation_layers = {
        "VK_LAYER_KHRONOS_validation"
    };

    // check the validation layer support
    auto check_validation_layer_support() -> bool;
};

#endif