#include "../common/restir_di.slangh"

[[vk::binding(0, 0)]] ConstantBuffer<Frame> frame;
[[vk::binding(1, 0)]] StructuredBuffer<PointLight> point_lights;
[[vk::binding(2, 0)]] Texture2D<float4> base_color_ao_texture;
[[vk::binding(3, 0)]] Texture2D<float4> normal_roughness_texture;
[[vk::binding(4, 0)]] Texture2D<float4> emissive_metallic_texture;
[[vk::binding(5, 0)]] Texture2D<float> depth_texture;
[[vk::binding(6, 0)]] RWStructuredBuffer<Reservoir> current_reservoirs;
[[vk::binding(7, 0)]] StructuredBuffer<Reservoir> final_reservoirs;
[[vk::binding(8, 0)]] Texture2D<float4> motion_texture;
[[vk::binding(9, 0)]] RaytracingAccelerationStructure scene_tlas;

[numthreads(8, 8, 1)]
void main(uint3 dispatch_thread_id : SV_DispatchThreadID)
{
    uint2 pixel = dispatch_thread_id.xy;
    uint2 viewport = frame.light_count_viewport.yz;
    if (any(pixel >= viewport))
    {
        return;
    }
    uint index = pixel.y * viewport.x + pixel.x;
    float depth = depth_texture.Load(int3(pixel, 0));
    Reservoir reservoir = {};
    reservoir.light_index = INVALID_LIGHT;
    reservoir.depth = depth;
    if (depth >= 1.0 || frame.light_count_viewport.x == 0u)
    {
        current_reservoirs[index] = reservoir;
        return;
    }

    float4 normal_roughness = normal_roughness_texture.Load(int3(pixel, 0));
    Surface surface;
    surface.position = reconstruct_position(float2(pixel) + 0.5, depth, frame);
    surface.normal = normalize(normal_roughness.xyz);
    surface.base_color = max(base_color_ao_texture.Load(int3(pixel, 0)).rgb, 0.0);
    surface.roughness = clamp(normal_roughness.w, 0.045, 1.0);
    surface.metallic = saturate(emissive_metallic_texture.Load(int3(pixel, 0)).a);
    reservoir.packed_normal_rm = pack_normal_material(surface);
    reservoir.view_depth = mul(frame.view_projection, float4(surface.position, 1.0)).w;

    uint rng = index ^ (frame.sampling.x * 2891336453u + 747796405u);
    float weight_sum = 0.0;
    for (uint candidate = 0u; candidate < frame.sampling.y; ++candidate)
    {
        uint light_index = random_light(rng, frame.light_count_viewport.x);
        uint packed_uv = random_uint(rng);
        float target = target_pdf(surface, frame, point_lights[light_index], packed_uv);
        // Uniform light selection and uniform UV: joint source PDF is 1/N.
        float weight = target * float(frame.light_count_viewport.x);
        update_reservoir(reservoir, weight_sum, light_index, packed_uv, target, weight, 1u, random_float(rng));
    }
    normalize_reservoir(reservoir, weight_sum);
    if (reservoir.light_index != INVALID_LIGHT &&
        !visible_to_sample(scene_tlas, surface, point_lights[reservoir.light_index], reservoir.packed_uv, frame.visibility.x))
    {
        discard_sample(reservoir);
        weight_sum = 0.0;
    }

    float4 motion = motion_texture.Load(int3(pixel, 0));
    float2 previous_uv = (float2(pixel) + 0.5) / float2(viewport) + motion.xy;
    if (frame.light_count_viewport.w != 0u && motion.w > 0.5 && all(isfinite(motion)) &&
        all(previous_uv >= 0.0) && all(previous_uv < 1.0))
    {
        uint2 previous_pixel = uint2(previous_uv * float2(viewport));
        Reservoir history = final_reservoirs[previous_pixel.y * viewport.x + previous_pixel.x];
        float previous_depth = reservoir.view_depth + motion.z;
        if (similar_surface(reservoir.packed_normal_rm, previous_depth, history, frame.rejection))
        {
            uint history_count = min(history.sample_count, frame.reuse_limits.x);
            float target = 0.0;
            if (history.light_index < frame.light_count_viewport.x && history.weight > 0.0)
            {
                target = target_pdf(surface, frame, point_lights[history.light_index], history.packed_uv);
            }
            float weight = target * history.weight * float(history_count);
            update_reservoir(reservoir, weight_sum, history.light_index, history.packed_uv, target, weight, history_count, random_float(rng));
        }
    }
    normalize_reservoir(reservoir, weight_sum);
    current_reservoirs[index] = reservoir;
}
