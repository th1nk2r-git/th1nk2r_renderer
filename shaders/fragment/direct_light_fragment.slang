#include "../common/restir_di.slangh"
#include "../common/direct_light_nrd.slangh"

struct Input
{
    float4 position : SV_Position;
};

struct Output
{
    // Linear, material-demodulated radiance and first-bounce hit distance for RELAX.
    [[vk::location(0)]] float4 specular;
};

[[vk::binding(0, 0)]] ConstantBuffer<Frame> frame;
[[vk::binding(1, 0)]] StructuredBuffer<PointLight> point_lights;
[[vk::binding(2, 0)]] Texture2D<float4> base_color_ao_texture;
[[vk::binding(3, 0)]] Texture2D<float4> normal_roughness_texture;
[[vk::binding(4, 0)]] Texture2D<float> depth_texture;
[[vk::binding(5, 0)]] RaytracingAccelerationStructure scene_tlas;
[[vk::binding(6, 0)]] StructuredBuffer<Reservoir> final_reservoirs;
[[vk::binding(7, 0)]] Texture2D<float4> emissive_metallic_texture;
[[vk::binding(8, 0)]] [vk::image_format("rgba16f")] RWTexture2D<float4> diffuse_texture;

float3 world_direction(float3 local, float3 normal, float3 tangent, float3 bitangent)
{
    return normalize(local.x * tangent + local.y * bitangent + local.z * normal);
}

float3 sample_diffuse_direction(float3 normal, float3 tangent, float3 bitangent, inout uint state)
{
    float angle = 2.0 * PI * random_float(state);
    float cosine = sqrt(random_float(state));
    float sine = sqrt(saturate(1.0 - cosine * cosine));
    return world_direction(float3(sine * cos(angle), sine * sin(angle), cosine), normal, tangent, bitangent);
}

float3 sample_specular_direction(
    float3 normal, float3 tangent, float3 bitangent, float3 view_direction, float roughness, inout uint state)
{
    float3 view_local = float3(dot(view_direction, tangent), dot(view_direction, bitangent), dot(view_direction, normal));
    float alpha = roughness * roughness;
    float3 warped_view = normalize(float3(alpha * view_local.xy, view_local.z));
    float angle = 2.0 * PI * random_float(state);
    float random = random_float(state);
    float s = 1.0 + length(view_local.xy);
    float alpha_squared = alpha * alpha;
    float k = (1.0 - alpha_squared) * s * s / (s * s + alpha_squared * view_local.z * view_local.z);
    float z = 1.0 - random * (1.0 + k * warped_view.z);
    float radius = sqrt(saturate(1.0 - z * z));
    float3 half_local = float3(radius * cos(angle), radius * sin(angle), z) + warped_view;
    half_local = normalize(float3(alpha * half_local.xy, max(half_local.z, 1e-6)));
    return world_direction(reflect(-view_local, half_local), normal, tangent, bitangent);
}

float trace_hit_distance(float3 origin, float3 direction, float ray_min, float ray_max, out bool visible)
{
    if (ray_max <= ray_min)
    {
        visible = true;
        return ray_max;
    }
    RayDesc ray;
    ray.Origin = origin;
    ray.Direction = direction;
    ray.TMin = ray_min;
    ray.TMax = ray_max;
    // The closest committed hit is required for NRD hitT.
    RayQuery<RAY_FLAG_FORCE_OPAQUE> query;
    query.TraceRayInline(scene_tlas, RAY_FLAG_NONE, 0xff, ray);
    while (query.Proceed())
    {
    }
    visible = query.CommittedStatus() == COMMITTED_NOTHING;
    return visible ? ray_max : query.CommittedRayT();
}

float fallback_hit_distance(
    Surface surface, float3 view_direction, float3 tangent, float3 bitangent,
    float max_distance, bool specular, inout uint state)
{
    float3 direction = specular
        ? sample_specular_direction(surface.normal, tangent, bitangent, view_direction, surface.roughness, state)
        : sample_diffuse_direction(surface.normal, tangent, bitangent, state);
    if (dot(surface.normal, direction) <= 0.0)
    {
        return 0.0;
    }
    bool visible;
    float bias = frame.visibility.x;
    return trace_hit_distance(surface.position + surface.normal * bias, direction, bias, max_distance, visible);
}

void evaluate_sample(
    Surface surface, float3 view_direction, PointLight light, uint packed_uv,
    out float3 diffuse, out float3 specular, out float diffuse_pdf, out float specular_pdf, out float distance)
{
    diffuse = float3(0.0);
    specular = float3(0.0);
    diffuse_pdf = 0.0;
    specular_pdf = 0.0;
    float3 to_sample = sample_position(light, packed_uv) - surface.position;
    float distance_squared = max(dot(to_sample, to_sample), 1e-8);
    distance = sqrt(distance_squared);
    if (light.position_range.w <= 0.0 || distance >= light.position_range.w)
    {
        return;
    }

    float3 light_direction = to_sample / distance;
    float n_dot_v = saturate(dot(surface.normal, view_direction));
    float n_dot_l = saturate(dot(surface.normal, light_direction));
    if (n_dot_v <= 0.0 || n_dot_l <= 0.0)
    {
        return;
    }

    float3 half_vector = normalize(view_direction + light_direction);
    float n_dot_h = saturate(dot(surface.normal, half_vector));
    float v_dot_h = saturate(dot(view_direction, half_vector));
    float3 f0 = lerp(float3(0.04), surface.base_color, surface.metallic);
    float alpha = surface.roughness * surface.roughness;
    float alpha_squared = alpha * alpha;
    float denominator = n_dot_h * n_dot_h * (alpha_squared - 1.0) + 1.0;
    float distribution = alpha_squared / max(PI * denominator * denominator, 1e-7);
    float geometry = ggx_g1(n_dot_v, alpha_squared) * ggx_g1(n_dot_l, alpha_squared);
    float3 fresnel = f0 + (1.0 - f0) * pow(1.0 - v_dot_h, 5.0);
    float3 diffuse_brdf = (1.0 - fresnel) * (1.0 - surface.metallic) * surface.base_color / PI;
    float3 specular_brdf = distribution * geometry * fresnel / max(4.0 * n_dot_v * n_dot_l, 1e-6);
    float normalized_distance = distance / light.position_range.w;
    float range_falloff = saturate(1.0 - pow(normalized_distance, 4.0));
    float attenuation = range_falloff * range_falloff / max(distance_squared, 1e-4);
    float3 illumination = light.color_intensity.rgb * light.color_intensity.w * attenuation * n_dot_l;
    diffuse = diffuse_brdf * illumination;
    specular = specular_brdf * illumination;

    diffuse_pdf = n_dot_l / PI;
    float sin_view = sqrt(saturate(1.0 - n_dot_v * n_dot_v));
    float2 warped_view = alpha * float2(sin_view, 0.0);
    float warped_length = sqrt(dot(warped_view, warped_view) + n_dot_v * n_dot_v);
    float s = 1.0 + sin_view;
    float k = (1.0 - alpha_squared) * s * s / (s * s + alpha_squared * n_dot_v * n_dot_v);
    specular_pdf = 0.5 * distribution / max(k * n_dot_v + warped_length, 1e-6);
}

float4 pack_signal(float3 irradiance, float3 factor, float hit_distance)
{
    float3 radiance = irradiance / factor;
    // RELAX tracks FP16 second moments, so 250^2 must remain representable.
    radiance = all(isfinite(radiance)) ? clamp(radiance, 0.0, 250.0) : float3(0.0);
    hit_distance = isfinite(hit_distance) ? clamp(hit_distance, 0.0, 65504.0) : 0.0;
    return float4(radiance, hit_distance);
}

Output main(Input input)
{
    Output output;
    output.specular = float4(0.0);
    uint2 pixel = uint2(input.position.xy);
    bool write_diffuse = ((pixel.x | pixel.y) & 1u) == 0u;
    float4 diffuse_signal = float4(0.0);
    uint2 viewport = frame.light_count_viewport.yz;
    float depth = depth_texture.Load(int3(pixel, 0));
    if (depth >= 1.0)
    {
        if (write_diffuse) diffuse_texture[pixel >> 1] = diffuse_signal;
        return output;
    }

    float4 normal_roughness = normal_roughness_texture.Load(int3(pixel, 0));
    Surface surface;
    surface.position = reconstruct_position(input.position.xy, depth, frame);
    surface.normal = normalize(normal_roughness.xyz);
    surface.base_color = max(base_color_ao_texture.Load(int3(pixel, 0)).rgb, 0.0);
    surface.roughness = clamp(normal_roughness.w, 0.045, 1.0);
    surface.metallic = saturate(emissive_metallic_texture.Load(int3(pixel, 0)).a);
    float3 view_direction = normalize(frame.camera_position.xyz - surface.position);
    if (dot(surface.normal, view_direction) <= 0.0)
    {
        if (write_diffuse) diffuse_texture[pixel >> 1] = diffuse_signal;
        return output;
    }

    float3 diffuse_factor;
    float3 specular_factor;
    direct_light_material_factors(surface.normal, view_direction, surface.base_color, surface.metallic,
        surface.roughness, diffuse_factor, specular_factor);

    if (frame.light_count_viewport.x == 0u)
    {
        if (write_diffuse) diffuse_texture[pixel >> 1] = diffuse_signal;
        return output;
    }

    uint state = pixel.x * 1973u + pixel.y * 9277u + frame.sampling.x * 26699u + 911u;
    float3 tangent = normalize(cross(abs(surface.normal.z) < 0.999 ? float3(0.0, 0.0, 1.0) : float3(0.0, 1.0, 0.0), surface.normal));
    float3 bitangent = cross(surface.normal, tangent);
    Reservoir reservoir = final_reservoirs[pixel.y * viewport.x + pixel.x];
    bool valid = reservoir.light_index < frame.light_count_viewport.x && reservoir.sample_count > 0u &&
        reservoir.weight > 0.0 && isfinite(reservoir.weight);
    if (!valid)
    {
        // A missing ReSTIR sample still needs first-bounce distances on both lobes.
        if (write_diffuse)
        {
            diffuse_signal.a = fallback_hit_distance(surface, view_direction, tangent, bitangent, 65504.0, false, state);
            diffuse_texture[pixel >> 1] = diffuse_signal;
        }
        output.specular.a = fallback_hit_distance(surface, view_direction, tangent, bitangent, 65504.0, true, state);
        return output;
    }

    PointLight light = point_lights[reservoir.light_index];
    float3 diffuse;
    float3 specular;
    float diffuse_pdf;
    float specular_pdf;
    float distance;
    evaluate_sample(surface, view_direction, light, reservoir.packed_uv,
        diffuse, specular, diffuse_pdf, specular_pdf, distance);

    // Accept each lobe's selected-light hitT by its bounded BRDF*cos/PDF score.
    float diffuse_score = dot(max(diffuse, 0.0), float3(0.2126, 0.7152, 0.0722)) / max(diffuse_pdf, 1e-6);
    float specular_score = dot(max(specular, 0.0), float3(0.2126, 0.7152, 0.0722)) / max(specular_pdf, 1e-6);
    float max_score = max(diffuse_score, specular_score);
    float diffuse_probability = isfinite(max_score) && max_score > 0.0 ? saturate(diffuse_score / max_score) : 0.0;
    float specular_probability = isfinite(max_score) && max_score > 0.0 ? saturate(specular_score / max_score) : 0.0;

    float bias = frame.visibility.x;
    float3 origin = surface.position + surface.normal * bias;
    float3 to_sample = sample_position(light, reservoir.packed_uv) - origin;
    float ray_distance = length(to_sample);
    bool visible;
    float selected_hit = trace_hit_distance(origin, to_sample / max(ray_distance, 1e-6), bias,
        max(ray_distance - bias, bias), visible);
    if (visible)
    {
        selected_hit = distance;
    }
    else
    {
        diffuse = float3(0.0);
        specular = float3(0.0);
    }

    bool accept_diffuse = random_float(state) < diffuse_probability;
    bool accept_specular = random_float(state) < specular_probability;
    // A miss in the auxiliary lobe ray uses the finite FP16 distance limit.
    float fallback_limit = 65504.0;
    float specular_hit = accept_specular ? selected_hit : fallback_hit_distance(
        surface, view_direction, tangent, bitangent, fallback_limit, true, state);

    if (write_diffuse)
    {
        float diffuse_hit = accept_diffuse ? selected_hit : fallback_hit_distance(
            surface, view_direction, tangent, bitangent, fallback_limit, false, state);
        diffuse_signal = pack_signal(diffuse * reservoir.weight, diffuse_factor, diffuse_hit);
        diffuse_texture[pixel >> 1] = diffuse_signal;
    }
    output.specular = pack_signal(specular * reservoir.weight, specular_factor, specular_hit);
    return output;
}
