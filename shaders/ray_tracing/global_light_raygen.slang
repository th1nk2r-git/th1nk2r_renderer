#include "global_light.slangh"

void light_material_factors(
    float3 normal, float3 view_direction, float3 base_color, float metallic, float roughness,
    out float3 diffuse_factor, out float3 specular_factor)
{
    float3 albedo = base_color * (1.0 - metallic);
    float3 f0 = lerp(float3(0.04), base_color, metallic);
    float n_dot_v = abs(dot(normal, view_direction));
    float m = saturate(roughness * roughness);
    float4 x = float4(1.0, n_dot_v, n_dot_v * n_dot_v, n_dot_v * n_dot_v * n_dot_v);
    float4 y = float4(1.0, m, m * m, m * m * m);
    const float2x2 bias_num = float2x2(0.99044, -1.28514, 1.29678, -0.755907);
    const float3x3 bias_den = float3x3(1.0, 2.92338, 59.4188, 20.3225, -27.0302, 222.592, 121.563, 626.13, 316.627);
    const float2x2 scale_num = float2x2(0.0365463, 3.32707, 9.0632, -9.04756);
    const float3x3 scale_den = float3x3(1.0, 3.59685, -1.36772, 9.04401, -16.3174, 9.22949, 5.56589, 19.7886, -20.2123);
    float bias = dot(mul(bias_num, x.xy), y.xy) / max(dot(mul(bias_den, x.xyw), y.xyw), 1e-6);
    float scale = dot(mul(scale_num, x.xy), y.xy) / max(dot(mul(scale_den, x.xzw), y.xyw), 1e-6);
    float3 environment_fresnel = saturate(f0 * scale + bias);
    diffuse_factor = lerp(float3(0.02), float3(1.0), (1.0 - environment_fresnel) * albedo);
    specular_factor = lerp(float3(0.02), float3(1.0), environment_fresnel * lerp(0.1, 1.0, roughness));
}

uint random_bits(inout uint state) {
    state = state * 747796405u + 2891336453u;
    uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}

float random_float(inout uint state) {
    return (random_bits(state) >> 8u) * (1.0 / 16777216.0);
}

float3 uniform_sphere(inout uint state) {
    float z = 1.0 - 2.0 * random_float(state);
    float angle = 2.0 * PI * random_float(state);
    float radius = sqrt(max(0.0, 1.0 - z * z));
    return float3(radius * cos(angle), radius * sin(angle), z);
}

float3 uniform_hemisphere(float3 normal, inout uint state) {
    float z = random_float(state);
    float angle = 2.0 * PI * random_float(state);
    float radius = sqrt(max(0.0, 1.0 - z * z));
    float3 tangent = normalize(cross(abs(normal.z) < 0.999 ? float3(0.0, 0.0, 1.0) : float3(1.0, 0.0, 0.0), normal));
    float3 bitangent = cross(normal, tangent);
    return tangent * (radius * cos(angle)) + bitangent * (radius * sin(angle)) + normal * z;
}

uint ray_flags() {
    return frame.frame_light_roulette.w != 0u ? RAY_FLAG_CULL_BACK_FACING_TRIANGLES : 0u;
}

float max_component(float3 value) {
    return max(value.r, max(value.g, value.b));
}

[shader("raygeneration")]
void main() {
    uint2 pixel = DispatchRaysIndex().xy;
    float depth = depth_texture.Load(int3(pixel, 0));
    if (depth >= 1.0) {
        diffuse_output[pixel] = 0.0;
        specular_output[pixel] = 0.0;
        return;
    }

    float2 ndc = (float2(pixel) + 0.5) / float2(frame.viewport_samples_bounces.xy) * 2.0 - 1.0;
    float4 world = mul(frame.inverse_view_projection, float4(ndc, depth, 1.0));
    Surface primary;
    primary.position = world.xyz / world.w;
    primary.normal = normalize(normal_roughness_texture.Load(int3(pixel, 0)).xyz);
    primary.base_color = max(base_color_ao_texture.Load(int3(pixel, 0)).rgb, 0.0);
    float4 emissive_metallic = emissive_metallic_texture.Load(int3(pixel, 0));
    primary.emissive = 0.0;
    primary.metallic = saturate(emissive_metallic.a);
    primary.roughness = clamp(normal_roughness_texture.Load(int3(pixel, 0)).a, 0.045, 1.0);

    float3 diffuse_factor;
    float3 specular_factor;
    light_material_factors(primary.normal, normalize(frame.camera_position.xyz - primary.position), primary.base_color,
                                  primary.metallic, primary.roughness, diffuse_factor, specular_factor);

    float3 diffuse_sum = 0.0;
    float3 specular_sum = 0.0;
    float hit_distance_sum = 0.0;
    uint state = pixel.x * 1973u ^ pixel.y * 9277u ^ frame.frame_light_roulette.x * 26699u;
    state = random_bits(state);

    for (uint sample = 0; sample < frame.viewport_samples_bounces.z; ++sample) {
        Surface surface = primary;
        float3 view_direction = normalize(frame.camera_position.xyz - primary.position);
        float3 diffuse_throughput = 1.0;
        float3 specular_throughput = 1.0;
        float first_hit_distance = 0.0;
        float direct_distance = 0.0;

        for (uint bounce = 0; bounce < frame.viewport_samples_bounces.w; ++bounce) {
            if (frame.frame_light_roulette.y != 0u) {
                uint light_index = min(uint(random_float(state) * frame.frame_light_roulette.y), frame.frame_light_roulette.y - 1u);
                Light light = lights[light_index];
                float3 sphere_normal = uniform_sphere(state);
                float3 light_position = light.position_range.xyz + light.radius.x * sphere_normal;
                float3 light_vector = light_position - surface.position;
                float distance_squared = dot(light_vector, light_vector);
                float distance = sqrt(distance_squared);
                float3 light_direction = light_vector / max(distance, 1e-7);
                float emitter_cosine = max(dot(sphere_normal, -light_direction), 0.0);
                float surface_cosine = max(dot(surface.normal, light_direction), 0.0);
                if (distance > 2.0 * frame.roulette_bias_distance.z && distance <= light.position_range.w &&
                    emitter_cosine > 0.0 && surface_cosine > 0.0) {
                    RadiancePayload shadow;
                    shadow.hit = 0u;
                    RayDesc shadow_ray;
                    shadow_ray.Origin = surface.position + surface.normal * frame.roulette_bias_distance.z;
                    shadow_ray.Direction = light_direction;
                    shadow_ray.TMin = frame.roulette_bias_distance.z;
                    shadow_ray.TMax = distance - frame.roulette_bias_distance.z;
                    TraceRay(scene_tlas, ray_flags() | RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_CLOSEST_HIT_SHADER,
                             0xff, 1, 0, 1, shadow_ray, shadow);
                    if (shadow.hit != 0u) {
                        if (bounce == 0u) direct_distance = distance;
                        float3 diffuse_brdf;
                        float3 specular_brdf;
                        evaluate_brdf(surface, view_direction, light_direction, diffuse_brdf, specular_brdf);
                        float area = 4.0 * PI * light.radius.x * light.radius.x;
                        float3 emission = light.color_intensity.rgb * light.color_intensity.w;
                        float3 incoming = emission * (frame.frame_light_roulette.y * area * emitter_cosine * surface_cosine / distance_squared);
                        if (bounce == 0u) {
                            diffuse_sum += diffuse_brdf * incoming;
                            specular_sum += specular_brdf * incoming;
                        } else {
                            float3 brdf = diffuse_brdf + specular_brdf;
                            diffuse_sum += diffuse_throughput * brdf * incoming;
                            specular_sum += specular_throughput * brdf * incoming;
                        }
                    }
                }
            }

            if (bounce + 1u >= frame.viewport_samples_bounces.w) break;
            float3 direction = uniform_hemisphere(surface.normal, state);
            float cosine = max(dot(surface.normal, direction), 0.0);
            float3 diffuse_brdf;
            float3 specular_brdf;
            evaluate_brdf(surface, view_direction, direction, diffuse_brdf, specular_brdf);
            if (bounce == 0u) {
                diffuse_throughput *= diffuse_brdf * (2.0 * PI * cosine);
                specular_throughput *= specular_brdf * (2.0 * PI * cosine);
            } else {
                float3 multiplier = (diffuse_brdf + specular_brdf) * (2.0 * PI * cosine);
                diffuse_throughput *= multiplier;
                specular_throughput *= multiplier;
            }

            if (bounce + 1u >= frame.frame_light_roulette.z) {
                float survival = clamp(max(max_component(diffuse_throughput), max_component(specular_throughput)),
                    frame.roulette_bias_distance.x, frame.roulette_bias_distance.y);
                if (random_float(state) >= survival) break;
                diffuse_throughput /= survival;
                specular_throughput /= survival;
            }

            RadiancePayload hit;
            hit.hit = 0u;
            RayDesc ray;
            ray.Origin = surface.position + surface.normal * frame.roulette_bias_distance.z;
            ray.Direction = direction;
            ray.TMin = frame.roulette_bias_distance.z;
            ray.TMax = frame.roulette_bias_distance.w;
            TraceRay(scene_tlas, ray_flags(), 0xff, 0, 0, 0, ray, hit);
            if (hit.hit == 0u) break;
            if (bounce == 0u) first_hit_distance = hit.distance;
            surface = hit_surface(hit, ray.Origin + direction * hit.distance);
            view_direction = -direction;
            diffuse_sum += diffuse_throughput * surface.emissive;
            specular_sum += specular_throughput * surface.emissive;
        }
        hit_distance_sum += first_hit_distance > 0.0 ? first_hit_distance : direct_distance;
    }

    float inv_samples = 1.0 / frame.viewport_samples_bounces.z;
    float hit_distance = hit_distance_sum * inv_samples;
    diffuse_output[pixel] = float4(diffuse_sum * inv_samples / diffuse_factor, hit_distance);
    specular_output[pixel] = float4(specular_sum * inv_samples / specular_factor, hit_distance);
}
