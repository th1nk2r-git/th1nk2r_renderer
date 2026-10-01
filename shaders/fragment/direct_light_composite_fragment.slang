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

[[vk::binding(0, 0)]] ConstantBuffer<Frame> frame;
[[vk::binding(1, 0)]] Texture2D<float4> diffuse_texture;
[[vk::binding(2, 0)]] Texture2D<float4> specular_texture;
[[vk::binding(3, 0)]] Texture2D<float4> base_color_ao_texture;
[[vk::binding(4, 0)]] Texture2D<float4> normal_roughness_texture;
[[vk::binding(5, 0)]] Texture2D<float> depth_texture;
[[vk::binding(6, 0)]] Texture2D<float4> emissive_metallic_texture;

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

    float3 diffuse = diffuse_texture.Load(int3(pixel, 0)).rgb * diffuse_factor;
    float3 specular = specular_texture.Load(int3(pixel, 0)).rgb * specular_factor;
    output.color.rgb = diffuse + specular;
    return output;
}
