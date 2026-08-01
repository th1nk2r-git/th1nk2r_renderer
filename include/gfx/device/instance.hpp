#ifndef INSTANCE_HPP
#define INSTANCE_HPP

#define VULKAN_HPP_NO_STRUCT_CONSTRUCTORS

#include <array>
#include <vulkan/vulkan_raii.hpp>
#include <vector>

class Instance {
public:
    Instance() = default;
    ~Instance() = default;

    Instance(const Instance&) = delete;
    auto operator=(const Instance&) -> Instance& = delete;

    Instance(Instance&&) noexcept = default;
    auto operator=(Instance&& other) noexcept -> Instance&;

    // construct a complete Vulkan instance while keeping the default state empty
    static auto make() -> Instance;

    // return the reference of the vulkan instance
    auto get() const -> const vk::raii::Instance& {
        return handle_;
    }


private:
    struct ConstructTag {};

    explicit Instance(ConstructTag);

    vk::raii::Context dispatcher;

    vk::raii::Instance handle_ = nullptr;
    vk::raii::DebugUtilsMessengerEXT debug_messenger_ = nullptr;

#ifdef NDEBUG
    bool validation_layers_enabled = false;
#else 
    bool validation_layers_enabled = true;
#endif

    static constexpr std::array<const char*, 1> validation_layers = {
        "VK_LAYER_KHRONOS_validation"
    };

    // check the validation layer support
    auto check_validation_layer_support() -> bool;
};

#endif
