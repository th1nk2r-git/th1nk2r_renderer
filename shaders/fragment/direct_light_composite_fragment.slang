#include "NRD.hlsli"
#include "../common/direct_light_nrd.slangh"
#include "../common/restir_di.slangh"

struct Input
{
    float4 position : SV_Position;
};

struct Output
{
    [[vk::location(0)]] float4 color;
};

struct Params
{
    float4 depth_and_normal;
};

[[vk::binding(0, 0)]] ConstantBuffer<Frame> frame;
[[vk::binding(1, 0)]] Texture2D<float4> diffuse_texture;
[[vk::binding(2, 0)]] Texture2D<float4> specular_texture;
[[vk::binding(3, 0)]] Texture2D<float4> base_color_ao_texture;
[[vk::binding(4, 0)]] Texture2D<float4> normal_roughness_texture;
[[vk::binding(5, 0)]] Texture2D<float> depth_texture;
[[vk::binding(6, 0)]] Texture2D<float4> emissive_metallic_texture;
[[vk::binding(7, 0)]] Texture2D<float> diffuse_view_z_texture;
[[vk::binding(8, 0)]] Texture2D<float4> diffuse_normal_roughness_texture;
[[vk::push_constant]] ConstantBuffer<Params> params;

Output main(Input input)
{
    Output output;
    output.color = float4(0.0, 0.0, 0.0, 1.0);
    uint2 pixel = uint2(input.position.xy);
    float depth = depth_texture.Load(int3(pixel, 0));
    if (depth >= 1.0)
    {
        return output;
    }

    float4 normal_roughness = normal_roughness_texture.Load(int3(pixel, 0));
    float3 normal = normalize(normal_roughness.xyz);
    float3 position = reconstruct_position(input.position.xy, depth, frame);
    float3 view_direction = normalize(frame.camera_position.xyz - position);
    float3 base_color = max(base_color_ao_texture.Load(int3(pixel, 0)).rgb, 0.0);
    float metallic = saturate(emissive_metallic_texture.Load(int3(pixel, 0)).a);
    float roughness = clamp(normal_roughness.w, 0.045, 1.0);
    float3 diffuse_factor;
    float3 specular_factor;
    direct_light_material_factors(normal, view_direction, base_color, metallic, roughness, diffuse_factor, specular_factor);

    float3 diffuse_radiance = 0.0;
    float view_z = params.depth_and_normal.y / (depth + params.depth_and_normal.x);
    if (((pixel.x | pixel.y) & 1u) == 0u)
    {
        diffuse_radiance = diffuse_texture.Load(int3(pixel >> 1, 0)).rgb;
    }
    else if (isfinite(view_z) && view_z > 0.0)
    {
        int2 low_max = int2((frame.light_count_viewport.yz + 1u) / 2u) - 1;
        int2 base = int2((pixel + 1u) / 2u) - 1;
        float nearest_depth_difference = 3.402823466e+38;
        [unroll]
        for (int y = 0; y < 2; ++y)
        {
            [unroll]
            for (int x = 0; x < 2; ++x)
            {
                int2 low_pixel = clamp(base + int2(x, y), int2(0, 0), low_max);
                float sample_view_z = diffuse_view_z_texture.Load(int3(low_pixel, 0));
                float difference = abs(sample_view_z - view_z);
                if (!isfinite(sample_view_z) || sample_view_z <= 0.0 || difference >= nearest_depth_difference)
                {
                    continue;
                }
                if (depth_texture.Load(int3(low_pixel * 2, 0)) >= 1.0)
                {
                    continue;
                }
                float3 sample_normal = NRD_FrontEnd_UnpackNormalAndRoughness(
                    diffuse_normal_roughness_texture.Load(int3(low_pixel, 0))).xyz;
                if (dot(normal, sample_normal) >= params.depth_and_normal.z)
                {
                    nearest_depth_difference = difference;
                    diffuse_radiance = diffuse_texture.Load(int3(low_pixel, 0)).rgb;
                }
            }
        }
    }
    float3 diffuse = diffuse_radiance * diffuse_factor;
    float3 specular = specular_texture.Load(int3(pixel, 0)).rgb * specular_factor;
    output.color.rgb = diffuse + specular;
    return output;
}
