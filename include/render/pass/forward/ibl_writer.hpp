#ifndef IBL_WRITER_HPP
#define IBL_WRITER_HPP

#include <vulkan/vulkan_raii.hpp>

#include "gfx/device/device.hpp"
#include "gfx/device/image_uploader.hpp"
#include "gfx/device/memory_allocator.hpp"
#include "gfx/resource/image.hpp"
#include "resource/cpu/image.hpp"

class IblWriter {
public:
    IblWriter(
        const Device& device,
        const MemoryAllocator& allocator,
        const vk::raii::DescriptorSetLayout& descriptor_set_layout
    );

    IblWriter(const IblWriter&) = delete;
    auto operator=(const IblWriter&) -> IblWriter& = delete;
    IblWriter(IblWriter&&) = delete;
    auto operator=(IblWriter&&) -> IblWriter& = delete;

    auto write(
        const HdrImageData& panorama,
        ImageUploader& uploader
    ) -> void;

    auto descriptor_set() const -> const vk::raii::DescriptorSet&;

    auto ready() const noexcept -> bool {
        return ready_;
    }

private:
    const Device& device_;
    const MemoryAllocator& allocator_;
    const vk::raii::DescriptorSetLayout& descriptor_set_layout_;

    Image environment_image_;
    Image irradiance_image_;
    Image prefiltered_image_;
    Image brdf_lut_image_;

    vk::raii::ImageView environment_view_ = nullptr;
    vk::raii::ImageView irradiance_view_ = nullptr;
    vk::raii::ImageView prefiltered_view_ = nullptr;
    vk::raii::ImageView brdf_lut_view_ = nullptr;

    vk::raii::Sampler sampler_ = nullptr;
    vk::raii::DescriptorPool descriptor_pool_ = nullptr;
    vk::raii::DescriptorSet descriptor_set_ = nullptr;
    bool ready_ = false;
};

#endif
