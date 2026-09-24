#ifndef MATERIAL_HPP
#define MATERIAL_HPP

#include <array>
#include <cstddef>
#include <cstdint>

#include "resource/cpu/material.hpp"
#include "resource/gpu/resource_id.hpp"

class Texture;

struct MaterialTextures {
    ResourceId<Texture> base_color;
    ResourceId<Texture> metallic_roughness;
    ResourceId<Texture> normal;
    ResourceId<Texture> occlusion;
    ResourceId<Texture> emissive;
};

// Packed element stored in AssetsDB's global material storage buffer.
// Keep this layout synchronized with GeometryMaterial in the shader.
struct alignas(16) GpuMaterial {
    std::array<float, 4> base_color_factor{};
    std::array<float, 4> emissive_normal_scale{};
    std::array<float, 4> metallic_roughness_occlusion_alpha_cutoff{};
    std::array<uint32_t, 4> flags{};
    // Each index is ResourceId<Texture>::value(). Descriptor arrays that
    // consume GpuMaterial must place that texture in the same array slot.
    uint32_t base_color_texture_index = 0;
    uint32_t metallic_roughness_texture_index = 0;
    uint32_t normal_texture_index = 0;
    uint32_t occlusion_texture_index = 0;
    uint32_t emissive_texture_index = 0;
    uint32_t padding_0 = 0;
    uint32_t padding_1 = 0;
    uint32_t padding_2 = 0;
};

static_assert(sizeof(GpuMaterial) == 96);
static_assert(alignof(GpuMaterial) == 16);
static_assert(offsetof(GpuMaterial, base_color_factor) == 0);
static_assert(offsetof(GpuMaterial, emissive_normal_scale) == 16);
static_assert(
    offsetof(GpuMaterial, metallic_roughness_occlusion_alpha_cutoff) == 32
);
static_assert(offsetof(GpuMaterial, flags) == 48);
static_assert(offsetof(GpuMaterial, base_color_texture_index) == 64);
static_assert(offsetof(GpuMaterial, metallic_roughness_texture_index) == 68);
static_assert(offsetof(GpuMaterial, normal_texture_index) == 72);
static_assert(offsetof(GpuMaterial, occlusion_texture_index) == 76);
static_assert(offsetof(GpuMaterial, emissive_texture_index) == 80);

// Like Mesh, Material only describes data stored in global GPU resources.
struct Material {
    uint32_t buffer_index = 0;
};

auto make_gpu_material(
    const MaterialData& data,
    MaterialTextures textures
) -> GpuMaterial;

#endif
