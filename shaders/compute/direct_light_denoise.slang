#include "NRD.hlsli"

struct Params
{
    uint4 viewport;
    float4 depth;
};

[[vk::binding(0, 0)]] Texture2D<float> depth_texture;
[[vk::binding(1, 0)]] Texture2D<float4> motion_texture;
[[vk::binding(2, 0)]] Texture2D<float4> normal_roughness_texture;
[[vk::binding(3, 0)]] Texture2D<float4> emissive_metallic_texture;
[[vk::binding(4, 0)]] [vk::image_format("r32f")] RWTexture2D<float> view_z_texture;
[[vk::binding(5, 0)]] [vk::image_format("rgba16f")] RWTexture2D<float4> nrd_motion_texture;
[[vk::binding(6, 0)]] [vk::image_format("rgb10a2")] RWTexture2D<float4> nrd_normal_roughness_texture;
[[vk::binding(7, 0)]] [vk::image_format("r32f")] RWTexture2D<float> diffuse_view_z_texture;
[[vk::binding(8, 0)]] [vk::image_format("rgba16f")] RWTexture2D<float4> diffuse_motion_texture;
[[vk::binding(9, 0)]] [vk::image_format("rgb10a2")] RWTexture2D<float4> diffuse_normal_roughness_texture;
[[vk::push_constant]] ConstantBuffer<Params> params;

[numthreads(8, 8, 1)]
void main(uint3 dispatch_thread_id : SV_DispatchThreadID)
{
    uint2 pixel = dispatch_thread_id.xy;
    if (any(pixel >= params.viewport.xy))
    {
        return;
    }

    float depth = depth_texture.Load(int3(pixel, 0));
    float view_z = params.depth.z + 1.0;
    float4 motion = float4(100.0, 100.0, 0.0, 0.0);
    float4 normal_roughness = NRD_FrontEnd_PackNormalAndRoughness(float3(0.0, 0.0, 1.0), 1.0, 0.0);
    if (depth < 1.0)
    {
        float linear_depth = params.depth.y / (depth + params.depth.x);
        view_z = isfinite(linear_depth) && linear_depth > 0.0 ? linear_depth : view_z;

        float4 surface_motion = motion_texture.Load(int3(pixel, 0));
        motion = surface_motion.w > 0.5 && all(isfinite(surface_motion.xyz))
            ? float4(surface_motion.xyz, 0.0) : motion;

        float4 surface_normal_roughness = normal_roughness_texture.Load(int3(pixel, 0));
        float3 normal = surface_normal_roughness.xyz;
        normal = all(isfinite(normal)) && dot(normal, normal) > 1e-8 ? normalize(normal) : float3(0.0, 0.0, 1.0);
        float roughness = isfinite(surface_normal_roughness.w) ? clamp(surface_normal_roughness.w, 0.045, 1.0) : 1.0;
        float metallic = saturate(emissive_metallic_texture.Load(int3(pixel, 0)).a);
        float material_id = metallic >= 0.99 ? 0.0 : 1.0;
        normal_roughness = NRD_FrontEnd_PackNormalAndRoughness(normal, roughness, material_id);
    }

    view_z_texture[pixel] = view_z;
    nrd_motion_texture[pixel] = motion;
    nrd_normal_roughness_texture[pixel] = normal_roughness;
    if (((pixel.x | pixel.y) & 1u) == 0u)
    {
        uint2 diffuse_pixel = pixel >> 1;
        diffuse_view_z_texture[diffuse_pixel] = view_z;
        diffuse_motion_texture[diffuse_pixel] = motion;
        diffuse_normal_roughness_texture[diffuse_pixel] = normal_roughness;
    }
}
