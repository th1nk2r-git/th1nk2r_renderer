#include "../common/restir_di.slangh"

struct Input
{
    float4 position : SV_Position;
};

struct Output
{
    // Linear direct lighting; the sRGB backbuffer performs encoding.
    [[vk::location(0)]] float4 color;
};

[[vk::binding(0, 0)]] ConstantBuffer<Frame> frame;
[[vk::binding(1, 0)]] StructuredBuffer<PointLight> point_lights;
[[vk::binding(2, 0)]] Texture2D<float4> base_color_ao_texture;
[[vk::binding(3, 0)]] Texture2D<float4> normal_roughness_texture;
[[vk::binding(4, 0)]] Texture2D<float> depth_texture;
[[vk::binding(5, 0)]] RaytracingAccelerationStructure scene_tlas;
[[vk::binding(6, 0)]] StructuredBuffer<Reservoir> final_reservoirs;
[[vk::binding(7, 0)]] Texture2D<float4> emissive_metallic_texture;

Output main(Input input)
{
    Output output;
    output.color = float4(0.0, 0.0, 0.0, 1.0);
    uint2 pixel = uint2(input.position.xy);
    uint2 viewport = frame.light_count_viewport.yz;
    float depth = depth_texture.Load(int3(pixel, 0));
    if (depth >= 1.0)
    {
        return output;
    }
    Reservoir reservoir = final_reservoirs[pixel.y * viewport.x + pixel.x];
    if (reservoir.light_index >= frame.light_count_viewport.x || reservoir.sample_count == 0u || reservoir.weight <= 0.0)
    {
        return output;
    }

    float4 normal_roughness = normal_roughness_texture.Load(int3(pixel, 0));
    Surface surface;
    surface.position = reconstruct_position(input.position.xy, depth, frame);
    surface.normal = normalize(normal_roughness.xyz);
    surface.base_color = max(base_color_ao_texture.Load(int3(pixel, 0)).rgb, 0.0);
    surface.roughness = clamp(normal_roughness.w, 0.045, 1.0);
    surface.metallic = saturate(emissive_metallic_texture.Load(int3(pixel, 0)).a);

    PointLight light = point_lights[reservoir.light_index];
    if (visible_to_sample(scene_tlas, surface, light, reservoir.packed_uv, frame.visibility.x))
    {
        output.color.rgb = contribution(surface, frame.camera_position.xyz, light, reservoir.packed_uv) * reservoir.weight;
    }
    return output;
}
