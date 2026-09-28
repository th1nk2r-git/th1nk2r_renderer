#include "../common/restir_di.slangh"

[[vk::binding(0, 0)]] ConstantBuffer<Frame> frame;
[[vk::binding(1, 0)]] StructuredBuffer<PointLight> point_lights;
[[vk::binding(2, 0)]] Texture2D<float4> base_color_ao_texture;
[[vk::binding(3, 0)]] Texture2D<float4> normal_roughness_texture;
[[vk::binding(4, 0)]] Texture2D<float4> emissive_metallic_texture;
[[vk::binding(5, 0)]] Texture2D<float> depth_texture;
[[vk::binding(6, 0)]] StructuredBuffer<Reservoir> current_reservoirs;
[[vk::binding(7, 0)]] RWStructuredBuffer<Reservoir> final_reservoirs;

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
    Reservoir reservoir = current_reservoirs[index];
    if (reservoir.depth >= 1.0 || frame.light_count_viewport.x == 0u || frame.sampling.z == 0u || frame.sampling.w == 0u)
    {
        final_reservoirs[index] = reservoir;
        return;
    }

    float4 normal_roughness = normal_roughness_texture.Load(int3(pixel, 0));
    Surface surface;
    surface.position = reconstruct_position(float2(pixel) + 0.5, reservoir.depth, frame);
    surface.normal = normalize(normal_roughness.xyz);
    surface.base_color = max(base_color_ao_texture.Load(int3(pixel, 0)).rgb, 0.0);
    surface.roughness = clamp(normal_roughness.w, 0.045, 1.0);
    surface.metallic = saturate(emissive_metallic_texture.Load(int3(pixel, 0)).a);

    float weight_sum = reservoir.weight * reservoir.target * float(reservoir.sample_count);
    uint rng = index ^ (frame.sampling.x * 277803737u + 1013904223u);
    // A randomly rotated low-discrepancy sequence distributes disk neighbors.
    float2 offset = float2(random_float(rng), random_float(rng));
    for (uint neighbor_index = 0u; neighbor_index < frame.sampling.z; ++neighbor_index)
    {
        float2 uv = frac(offset + float(neighbor_index) * float2(0.754877666, 0.569840296));
        float radius = float(frame.sampling.w) * sqrt(uv.x);
        float angle = 2.0 * PI * uv.y;
        int2 neighbor_pixel = int2(pixel) + int2(round(radius * float2(cos(angle), sin(angle))));
        if (any(neighbor_pixel < 0) || any(neighbor_pixel >= int2(viewport)) || all(neighbor_pixel == int2(pixel)))
        {
            continue;
        }
        Reservoir neighbor = current_reservoirs[uint(neighbor_pixel.y) * viewport.x + uint(neighbor_pixel.x)];
        if (!similar_surface(reservoir.packed_normal_rm, reservoir.view_depth, neighbor, frame.rejection))
        {
            continue;
        }
        float target = 0.0;
        if (neighbor.light_index < frame.light_count_viewport.x && neighbor.weight > 0.0)
        {
            target = target_pdf(surface, frame, point_lights[neighbor.light_index], neighbor.packed_uv);
        }
        float weight = target * neighbor.weight * float(neighbor.sample_count);
        update_reservoir(reservoir, weight_sum, neighbor.light_index, neighbor.packed_uv, target, weight, neighbor.sample_count, random_float(rng));
    }
    normalize_reservoir(reservoir, weight_sum);
    final_reservoirs[index] = reservoir;
}
