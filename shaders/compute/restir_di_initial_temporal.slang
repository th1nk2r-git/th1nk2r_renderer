#include "../common/restir_di.slangh"

struct RestirFrame
{
    column_major matrix<float, 4, 4> inverse_view_projection;
    column_major matrix<float, 4, 4> view_projection;
    float4 camera_position;
    // X: light count, YZ: viewport, W: valid history.
    uint4 light_count_viewport;
    // X: monotonically increasing sampling sequence, Y: candidates.
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
[[vk::binding(7, 0)]] Texture2D<float4> motion_texture;

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
    float depth = depth_texture.Load(int3(pixel, 0));
    float4 normal_rm = normal_rm_texture.Load(int3(pixel, 0));
    RestirReservoir result = restir_empty(depth, normal_rm);
    if (depth >= 1.0 || frame.light_count_viewport.x == 0u)
    {
        current_reservoirs[index] = result;
        return;
    }

    RestirSurface surface;
    surface.position = restir_reconstruct_position(
        float2(pixel) + 0.5, depth, viewport, frame.inverse_view_projection
    );
    surface.normal = restir_decode_octahedral(normal_rm.xy);
    surface.base_color = max(base_color_ao_texture.Load(int3(pixel, 0)).rgb, 0.0);
    surface.roughness = clamp(normal_rm.z, 0.045, 1.0);
    surface.metallic = saturate(normal_rm.w);
    surface.depth = depth;
    surface.normal_rm = normal_rm;
    result.view_depth = mul(
        frame.view_projection, float4(surface.position, 1.0)
    ).w;

    uint random_state = restir_seed(pixel, frame.sampling.x, 0x1234567u);
    float weight_sum = 0.0;
    uint light_count = frame.light_count_viewport.x;
    for (uint candidate = 0u; candidate < frame.sampling.y; ++candidate)
    {
        uint light_index = min(uint(restir_random_float(random_state) *
            float(light_count)), light_count - 1u);
        uint packed_uv = restir_pack_uv(float2(
            restir_random_float(random_state),
            restir_random_float(random_state)
        ));
        float target = restir_target(restir_contribution(
            surface, frame.camera_position.xyz, point_lights[light_index], packed_uv
        ));
        // The proposal selects a light uniformly, then a uniform point on its
        // sphere. UV is already uniform in that parameterization.
        restir_add(
            result, weight_sum, light_index, packed_uv, target,
            target * float(light_count), 1u, random_state
        );
    }

    float4 motion = motion_texture.Load(int3(pixel, 0));
    if (frame.light_count_viewport.w != 0u && motion.w > 0.5 &&
        motion.z > 0.0)
    {
        float2 previous_uv = (float2(pixel) + 0.5) / float2(viewport) +
            motion.xy;
        if (all(previous_uv >= 0.0) && all(previous_uv < 1.0))
        {
            uint2 previous_pixel = uint2(previous_uv * float2(viewport));
            uint previous_index = previous_pixel.y * viewport.x + previous_pixel.x;
            RestirReservoir history = final_reservoirs[previous_index];
            if (history.depth < 1.0 && history.sample_count > 0u &&
                history.light_index < light_count && history.weight > 0.0)
            {
                float4 previous_normal_rm =
                    restir_unpack_normal_rm(history.packed_normal_rm);
                float3 previous_normal =
                    restir_decode_octahedral(previous_normal_rm.xy);
                float depth_tolerance = max(0.05, 0.02 * motion.z);
                if (abs(history.view_depth - motion.z) < depth_tolerance &&
                    dot(surface.normal, previous_normal) > 0.9 &&
                    abs(surface.roughness - previous_normal_rm.z) < 0.15 &&
                    abs(surface.metallic - previous_normal_rm.w) < 0.15)
                {
                    float target = restir_target(restir_contribution(
                        surface, frame.camera_position.xyz,
                        point_lights[history.light_index], history.packed_uv
                    ));
                    uint history_count = min(
                        history.sample_count, frame.reuse_limits.x
                    );
                    restir_add(
                        result, weight_sum, history.light_index,
                        history.packed_uv, target,
                        target * history.weight * float(history_count),
                        history_count, random_state
                    );
                }
            }
        }
    }

    restir_finalize(result, weight_sum);
    current_reservoirs[index] = result;
}
