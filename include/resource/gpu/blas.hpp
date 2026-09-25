#ifndef BLAS_HPP
#define BLAS_HPP

#include <utility>

#include <vulkan/vulkan_raii.hpp>

#include "gfx/resource/buffer.hpp"

struct Blas {
    Blas(
        Buffer storage_buffer,
        vk::raii::AccelerationStructureKHR acceleration_structure,
        vk::DeviceAddress device_address
    ) noexcept
        : storage(std::move(storage_buffer)),
          handle(std::move(acceleration_structure)),
          address(device_address) {}

    Blas(const Blas&) = delete;
    auto operator=(const Blas&) -> Blas& = delete;
    Blas(Blas&&) noexcept = default;
    auto operator=(Blas&&) noexcept -> Blas& = default;

    // Declared before the handle so the handle is destroyed first.
    Buffer storage;
    vk::raii::AccelerationStructureKHR handle = nullptr;
    vk::DeviceAddress address = 0;
};

#endif
