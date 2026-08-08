#ifndef INSTANCE_HPP
#define INSTANCE_HPP

#define VULKAN_HPP_NO_STRUCT_CONSTRUCTORS

#include <array>
#include <vulkan/vulkan_raii.hpp>
#include <vector>

class Instance {
public:
    Instance();
    ~Instance() = default;

    Instance(const Instance&) = delete;
    auto operator=(const Instance&) -> Instance& = delete;
    Instance(Instance&&) = delete;
    auto operator=(Instance&&) -> Instance& = delete;

    // return the reference of the vulkan instance
    auto get() const -> const vk::raii::Instance& {
        return handle_;
    }


private:
    vk::raii::Context dispatcher;
    bool validation_enabled_;
    vk::raii::Instance handle_ = nullptr;
    vk::raii::DebugUtilsMessengerEXT debug_messenger_ = nullptr;

#ifdef NDEBUG
    static constexpr bool validation_layers_enabled = false;
#else 
    static constexpr bool validation_layers_enabled = true;
#endif

    static constexpr std::array<const char*, 1> validation_layers = {
        "VK_LAYER_KHRONOS_validation"
    };

    // check the validation layer support
    static auto check_validation_layer_support() -> bool;

    static auto create_instance(
        vk::raii::Context& dispatcher,
        bool validation_enabled
    ) -> vk::raii::Instance;

    static auto create_debug_messenger(
        const vk::raii::Instance& instance,
        bool validation_enabled
    ) -> vk::raii::DebugUtilsMessengerEXT;
};

#endif
