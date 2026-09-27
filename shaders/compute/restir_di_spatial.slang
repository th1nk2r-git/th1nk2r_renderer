#include "../common/restir_di.slangh"

struct RestirFrame
{
    column_major matrix<float, 4, 4> inverse_view_projection;
    column_major matrix<float, 4, 4> view_projection;
    float4 camera_position;
    uint4 light_count_viewport;
    // Z: number of neighbors, W: radius in pixels.
    uint4 sampling;
    // X: maximum effective sample count taken from one reused reservoir.
    uint4 reuse_limits;
};

[[vk::binding(0, 0)]] ConstantBuffer<RestirFrame> frame;
[[vk::binding(1, 0)]] StructuredBuffer<RestirPointLight> point_lights;
[[vk::binding(2, 0)]] Texture2D<float4> base_color_ao_texture;
[[vk::binding(3, 0)]] Texture2D<float4> normal_rm_texture;
[[vk::binding(4, 0)]] Texture2D<float> depth_texture;
[[vk::binding(5, 0)]] RWStructuredBuffer<RestirReservoir> current_reservoirs;
[[vk::binding(6, 0)]] RWStructuredBuffer<RestirReservoir> final_reservoirs;

[[numthreads(8, 8, 1)]]
void main(uint3 dispatch_thread_id : SV_DispatchThreadID)
{
    uint2 pixel = dispatch_thread_id.xy;
    uint2 viewport = frame.light_count_viewport.yz;
    if (any(pixel >= viewport))
    {
        return;
    }

    uint index = pixel.y * viewport.x + pixel.x;
    RestirReservoir own = current_reservoirs[index];
    RestirReservoir result = restir_empty(own.depth, normal_rm_texture.Load(int3(pixel, 0)));
    result.view_depth = own.view_depth;
    if (own.depth >= 1.0 || frame.light_count_viewport.x == 0u)
    {
        final_reservoirs[index] = result;
        return;
    }

    RestirSurface surface;
    surface.position = restir_reconstruct_position(
        float2(pixel) + 0.5, own.depth, viewport, frame.inverse_view_projection
    );
    surface.normal_rm = normal_rm_texture.Load(int3(pixel, 0));
    surface.normal = restir_decode_octahedral(surface.normal_rm.xy);
    surface.base_color = max(base_color_ao_texture.Load(int3(pixel, 0)).rgb, 0.0);
    surface.roughness = clamp(surface.normal_rm.z, 0.045, 1.0);
    surface.metallic = saturate(surface.normal_rm.w);
    surface.depth = own.depth;

    uint random_state = restir_seed(pixel, frame.sampling.x, 0x7654321u);
    float weight_sum = 0.0;
    if (own.light_index < frame.light_count_viewport.x && own.weight > 0.0)
    {
        uint own_count = min(own.sample_count, frame.reuse_limits.x);
        restir_add(
            result, weight_sum, own.light_index, own.packed_uv, own.target,
            own.target * own.weight * float(own_count), own_count, random_state
        );
    }

    int radius = int(frame.sampling.w);
    for (uint tap = 0u; tap < frame.sampling.z; ++tap)
    {
        int2 offset = int2(
            int(restir_random_uint(random_state) % uint(radius * 2 + 1)) - radius,
            int(restir_random_uint(random_state) % uint(radius * 2 + 1)) - radius
        );
        if (all(offset == int2(0, 0)))
        {
            continue;
        }
        int2 neighbor_pixel = int2(pixel) + offset;
        if (any(neighbor_pixel < int2(0, 0)) ||
            any(neighbor_pixel >= int2(viewport)))
        {
            continue;
        }
        uint neighbor_index = uint(neighbor_pixel.y) * viewport.x + uint(neighbor_pixel.x);
        RestirReservoir neighbor = current_reservoirs[neighbor_index];
        if (neighbor.depth >= 1.0 || neighbor.sample_count == 0u ||
            neighbor.light_index >= frame.light_count_viewport.x || neighbor.weight <= 0.0)
        {
            continue;
        }
        float4 neighbor_normal_rm = normal_rm_texture.Load(int3(neighbor_pixel, 0));
        float3 neighbor_normal = restir_decode_octahedral(neighbor_normal_rm.xy);
        float3 neighbor_position = restir_reconstruct_position(
            float2(neighbor_pixel) + 0.5, neighbor.depth, viewport,
            frame.inverse_view_projection
        );
        float position_tolerance = max(
            0.05, 0.03 * length(surface.position - frame.camera_position.xyz)
        );
        if (distance(surface.position, neighbor_position) >= position_tolerance ||
            dot(surface.normal, neighbor_normal) <= 0.9 ||
            abs(surface.roughness - neighbor_normal_rm.z) >= 0.15 ||
            abs(surface.metallic - neighbor_normal_rm.w) >= 0.15)
        {
            continue;
        }
        float target = restir_target(restir_contribution(
            surface, frame.camera_position.xyz,
            point_lights[neighbor.light_index], neighbor.packed_uv
        ));
        uint neighbor_count = min(
            neighbor.sample_count, frame.reuse_limits.x
        );
        restir_add(
            result, weight_sum, neighbor.light_index, neighbor.packed_uv,
            target, target * neighbor.weight * float(neighbor_count),
            neighbor_count, random_state
        );
    }

    restir_finalize(result, weight_sum);
    final_reservoirs[index] = result;
}
