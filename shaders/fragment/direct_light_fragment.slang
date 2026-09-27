#include "../common/restir_di.slangh"

struct DirectLightFragmentInput
{
    float4 position : SV_Position;
};

struct DirectLightFragmentOutput
{
    // Linear direct lighting; the sRGB backbuffer performs encoding.
    [[vk::location(0)]] float4 color;
};

struct DirectLightFrame
{
    column_major matrix<float, 4, 4> inverse_view_projection;
    float4 camera_position;
    uint4 light_count_viewport;
};

[[vk::binding(0, 0)]] ConstantBuffer<DirectLightFrame> frame;
[[vk::binding(1, 0)]] StructuredBuffer<RestirPointLight> point_lights;
[[vk::binding(2, 0)]] Texture2D<float4> base_color_ao_texture;
[[vk::binding(3, 0)]] Texture2D<float4> normal_rm_texture;
[[vk::binding(4, 0)]] Texture2D<float> depth_texture;
[[vk::binding(5, 0)]] RaytracingAccelerationStructure scene_tlas;
[[vk::binding(6, 0)]] StructuredBuffer<RestirReservoir> final_reservoirs;

bool visible_to_light_sample(
    float3 world_position,
    float3 normal,
    float3 light_sample_position)
{
    static const float RAY_BIAS = 0.01;
    float3 ray_origin = world_position + normal * RAY_BIAS;
    float3 ray_to_light_sample = light_sample_position - ray_origin;
    float distance_to_light_sample = length(ray_to_light_sample);
    if (distance_to_light_sample <= RAY_BIAS * 2.0)
    {
        return true;
    }

    RayDesc ray;
    ray.Origin = ray_origin;
    ray.Direction = ray_to_light_sample / distance_to_light_sample;
    ray.TMin = RAY_BIAS;
    ray.TMax = distance_to_light_sample - RAY_BIAS;

    RayQuery<
        RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH |
        RAY_FLAG_FORCE_OPAQUE
    > query;
    query.TraceRayInline(scene_tlas, RAY_FLAG_NONE, 0xFF, ray);
    while (query.Proceed())
    {
    }
    return query.CommittedStatus() == COMMITTED_NOTHING;
}

DirectLightFragmentOutput main(DirectLightFragmentInput input)
{
    DirectLightFragmentOutput output;
    output.color = float4(0.0, 0.0, 0.0, 1.0);
    uint2 pixel = uint2(input.position.xy);
    uint2 viewport = frame.light_count_viewport.yz;
    float depth = depth_texture.Load(int3(pixel, 0));
    if (depth >= 1.0)
    {
        return output;
    }

    RestirReservoir reservoir = final_reservoirs[pixel.y * viewport.x + pixel.x];
    if (reservoir.light_index >= frame.light_count_viewport.x ||
        reservoir.weight <= 0.0)
    {
        return output;
    }

    float4 normal_rm = normal_rm_texture.Load(int3(pixel, 0));
    RestirSurface surface;
    surface.position = restir_reconstruct_position(
        input.position.xy, depth, viewport, frame.inverse_view_projection
    );
    surface.normal = restir_decode_octahedral(normal_rm.xy);
    surface.base_color = max(base_color_ao_texture.Load(int3(pixel, 0)).rgb, 0.0);
    surface.roughness = clamp(normal_rm.z, 0.045, 1.0);
    surface.metallic = saturate(normal_rm.w);
    surface.depth = depth;
    surface.normal_rm = normal_rm;

    RestirPointLight light = point_lights[reservoir.light_index];
    float3 sample_position = restir_sample_position(light, reservoir.packed_uv);
    if (visible_to_light_sample(surface.position, surface.normal, sample_position))
    {
        float3 unshadowed = restir_contribution(
            surface, frame.camera_position.xyz, light, reservoir.packed_uv
        );
        output.color.rgb = unshadowed * reservoir.weight;
    }
    return output;
}
