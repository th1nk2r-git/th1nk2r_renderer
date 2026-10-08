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
    if (depth >= 1.0)
    {
        view_z_texture[pixel] = params.depth.z + 1.0;
        nrd_motion_texture[pixel] = float4(100.0, 100.0, 0.0, 0.0);
        nrd_normal_roughness_texture[pixel] = NRD_FrontEnd_PackNormalAndRoughness(float3(0.0, 0.0, 1.0), 1.0, 0.0);
        return;
    }

    float view_z = params.depth.y / (depth + params.depth.x);
    view_z_texture[pixel] = isfinite(view_z) && view_z > 0.0 ? view_z : params.depth.z + 1.0;

    float4 motion = motion_texture.Load(int3(pixel, 0));
    nrd_motion_texture[pixel] = motion.w > 0.5 && all(isfinite(motion.xyz)) ? float4(motion.xyz, 0.0) : float4(100.0, 100.0, 0.0, 0.0);

    float4 normal_roughness = normal_roughness_texture.Load(int3(pixel, 0));
    float3 normal = normal_roughness.xyz;
    normal = all(isfinite(normal)) && dot(normal, normal) > 1e-8 ? normalize(normal) : float3(0.0, 0.0, 1.0);
    float roughness = isfinite(normal_roughness.w) ? clamp(normal_roughness.w, 0.045, 1.0) : 1.0;
    float metallic = saturate(emissive_metallic_texture.Load(int3(pixel, 0)).a);
    float material_id = metallic >= 0.99 ? 0.0 : 1.0;
    nrd_normal_roughness_texture[pixel] = NRD_FrontEnd_PackNormalAndRoughness(normal, roughness, material_id);
}
