struct DirectLightFragmentInput
{
    float4 position : SV_Position;
};

struct DirectLightFragmentOutput
{
    // Linear direct lighting and emissive color. An sRGB target performs
    // the final linear-to-sRGB conversion.
    [[vk::location(0)]]
    float4 color;
};

struct DirectLightFrame
{
    // Reconstructs a world-space position from Vulkan ZO device depth.
    column_major matrix<float, 4, 4> inverse_view_projection;

    // XYZ: world-space camera position, W: unused.
    float4 camera_position;

    // X: point-light count, YZ: viewport width and height, W: unused.
    uint4 light_count_viewport;
};

struct PointLight
{
    // XYZ: world-space position, W: finite influence range.
    float4 position_range;

    // RGB: linear light color, W: intensity.
    float4 color_intensity;

    // X: radiance, YZW: unused.
    float4 radiance;
};

[[vk::binding(0, 0)]]
ConstantBuffer<DirectLightFrame> frame;

[[vk::binding(1, 0)]]
StructuredBuffer<PointLight> point_lights;

// RGB: linear base color, A: ambient occlusion.
[[vk::binding(2, 0)]]
Texture2D<float4> base_color_ao_texture;

// RG: octahedral world normal, B: roughness, A: metallic.
[[vk::binding(3, 0)]]
Texture2D<float4> normal_roughness_metallic_texture;

// RGB: linear HDR emissive color.
[[vk::binding(4, 0)]]
Texture2D<float4> emissive_texture;

// Vulkan ZO device depth in [0, 1].
[[vk::binding(5, 0)]]
Texture2D<float> depth_texture;

static const float PI = 3.14159265358979323846;
static const float DIELECTRIC_F0 = 0.04;

// ------------------------------------------------------------
// Octahedral normal decoding
// ------------------------------------------------------------

float2 sign_not_zero(float2 value)
{
    return float2(value.x >= 0.0 ? 1.0 : -1.0, value.y >= 0.0 ? 1.0 : -1.0);
}

float3 decode_octahedral(float2 encoded)
{
    float3 normal = float3(encoded.x, encoded.y, 1.0 - abs(encoded.x) - abs(encoded.y));

    if (normal.z < 0.0)
    {
        normal.xy = (1.0 - abs(normal.yx)) * sign_not_zero(normal.xy);
    }

    return normalize(normal);
}

// ------------------------------------------------------------
// World-position reconstruction
// Vulkan NDC depth is [0, 1].
// ------------------------------------------------------------

float3 reconstruct_world_position(float2 pixel_position, float depth)
{
    float2 viewport_size = float2(frame.light_count_viewport.y, frame.light_count_viewport.z);
    float2 uv = pixel_position / viewport_size;

    // Projection matrix already contains the Vulkan Y flip, so do not
    // flip uv.y again here.
    float2 ndc_xy = uv * 2.0 - 1.0;
    float4 clip_position = float4(ndc_xy, depth, 1.0);
    float4 world_position = mul(frame.inverse_view_projection, clip_position);

    return world_position.xyz / world_position.w;
}

// ------------------------------------------------------------
// Fresnel-Schlick
//
// F = F0 + (1 - F0)(1 - V·H)^5
// ------------------------------------------------------------

float3 fresnel_schlick(float v_dot_h, float3 f0)
{
    float factor = pow(1.0 - saturate(v_dot_h), 5.0);
    return f0 + (1.0 - f0) * factor;
}

// ------------------------------------------------------------
// GGX / Trowbridge-Reitz normal distribution
//
// alpha  = roughness²
// alpha² = roughness⁴
//
// D = alpha² /
//     [PI * ((N·H)²(alpha² - 1) + 1)²]
// ------------------------------------------------------------

float distribution_ggx(float n_dot_h, float alpha_squared)
{
    float denominator = n_dot_h * n_dot_h * (alpha_squared - 1.0) + 1.0;
    denominator = PI * denominator * denominator;
    return alpha_squared / max(denominator, 1e-7);
}

// ------------------------------------------------------------
// Exact Smith GGX masking-shadowing term
//
// G1(N·X) = 2(N·X) /
//           [(N·X) + sqrt(alpha² + (1-alpha²)(N·X)²)]
// ------------------------------------------------------------

float geometry_smith_ggx_g1(float n_dot_x, float alpha_squared)
{
    float n_dot_x_squared = n_dot_x * n_dot_x;
    float denominator = n_dot_x + sqrt(
        alpha_squared + (1.0 - alpha_squared) * n_dot_x_squared
    );
    return (2.0 * n_dot_x) / max(denominator, 1e-6);
}

float geometry_smith_ggx(float n_dot_v, float n_dot_l, float alpha_squared)
{
    float g_view = geometry_smith_ggx_g1(n_dot_v, alpha_squared);
    float g_light = geometry_smith_ggx_g1(n_dot_l, alpha_squared);
    return g_view * g_light;
}

// ------------------------------------------------------------
// Smooth finite-range attenuation.
//
// inverse-square attenuation:
//     1 / distance²
//
// smooth range cutoff:
//     [1 - (distance / range)^4]²
// ------------------------------------------------------------

float point_light_attenuation(float distance_squared, float distance_to_light, float light_range)
{
    float safe_range = max(light_range, 1e-4);
    float normalized_distance = distance_to_light / safe_range;
    float normalized_distance_squared = normalized_distance * normalized_distance;
    float range_falloff = saturate(1.0 - normalized_distance_squared * normalized_distance_squared);

    range_falloff *= range_falloff;
    return range_falloff / max(distance_squared, 1e-4);
}

// ------------------------------------------------------------
// Cook-Torrance metallic/roughness BRDF
//
// BRDF = diffuse + specular
//
// diffuse  = kD * baseColor / PI
//
// specular = D * G * F /
//            [4(N·V)(N·L)]
// ------------------------------------------------------------

float3 evaluate_pbr_brdf(
    float3 base_color,
    float metallic,
    float roughness,
    float3 normal,
    float3 view_direction,
    float3 light_direction)
{
    float3 half_vector = normalize(view_direction + light_direction);
    float n_dot_v = saturate(dot(normal, view_direction));
    float n_dot_l = saturate(dot(normal, light_direction));
    float n_dot_h = saturate(dot(normal, half_vector));
    float v_dot_h = saturate(dot(view_direction, half_vector));

    // Metals use their base color as F0.
    // Dielectrics use approximately 4% reflectance.
    float3 f0 = lerp(float3(DIELECTRIC_F0), base_color, metallic);

    // glTF roughness is perceptual roughness.
    float alpha = roughness * roughness;
    float alpha_squared = alpha * alpha;
    float distribution = distribution_ggx(n_dot_h, alpha_squared);
    float geometry = geometry_smith_ggx(n_dot_v, n_dot_l, alpha_squared);
    float3 fresnel = fresnel_schlick(v_dot_h, f0);
    float specular_denominator = 4.0 * n_dot_v * n_dot_l;
    float3 specular = distribution * geometry * fresnel / max(specular_denominator, 1e-6);

    // Fresnel-reflected energy cannot also become diffuse light.
    float3 k_specular = fresnel;

    // Metals do not have a diffuse lobe.
    float3 k_diffuse = (1.0 - k_specular) * (1.0 - metallic);
    float3 diffuse = k_diffuse * base_color / PI;

    return diffuse + specular;
}

// ------------------------------------------------------------
// Fragment shader
// ------------------------------------------------------------

DirectLightFragmentOutput main(DirectLightFragmentInput input)
{
    DirectLightFragmentOutput output;
    int2 pixel = int2(input.position.xy);
    float4 base_color_ao = base_color_ao_texture.Load(int3(pixel, 0));
    float4 normal_roughness_metallic = normal_roughness_metallic_texture.Load(int3(pixel, 0));
    float3 emissive = emissive_texture.Load(int3(pixel, 0)).rgb;
    float depth = depth_texture.Load(int3(pixel, 0));

    // Depth is cleared to 1.0. It therefore represents background.
    if (depth >= 1.0)
    {
        output.color = float4(emissive, 1.0);
        return output;
    }

    float3 base_color = max(base_color_ao.rgb, 0.0);

    // AO belongs to indirect/ambient lighting. It is deliberately not
    // multiplied into the direct-light result below.
    float ambient_occlusion = saturate(base_color_ao.a);
    float3 normal = decode_octahedral(normal_roughness_metallic.rg);

    // Avoid a singular GGX highlight at exactly zero roughness.
    float roughness = clamp(normal_roughness_metallic.b, 0.045, 1.0);
    float metallic = saturate(normal_roughness_metallic.a);
    float3 world_position = reconstruct_world_position(input.position.xy, depth);
    float3 view_direction = normalize(frame.camera_position.xyz - world_position);
    float n_dot_v = saturate(dot(normal, view_direction));
    float3 direct_lighting = float3(0.0);

    // Back-facing surfaces receive no direct light.
    if (n_dot_v > 0.0)
    {
        uint light_count = frame.light_count_viewport.x;

        for (uint i = 0; i < light_count; ++i)
        {
            PointLight light = point_lights[i];
            float3 surface_to_light = light.position_range.xyz - world_position;
            float distance_squared = dot(surface_to_light, surface_to_light);
            float distance_to_light = sqrt(max(distance_squared, 1e-8));
            float light_range = light.position_range.w;

            if (distance_to_light >= light_range)
            {
                continue;
            }

            float3 light_direction = surface_to_light / distance_to_light;
            float n_dot_l = saturate(dot(normal, light_direction));

            if (n_dot_l <= 0.0)
            {
                continue;
            }

            float attenuation = point_light_attenuation(
                distance_squared, 
                distance_to_light, 
                light_range
            );

            // With the current CPU light structure:
            //
            // Li = color * intensity * radiance / distance²
            //
            // Here radiance.x acts as an additional scalar multiplier.
            float3 incident_radiance = light.color_intensity.rgb * light.color_intensity.w
                * light.radiance.x * attenuation;

            float3 brdf = evaluate_pbr_brdf(
                base_color, 
                metallic, 
                roughness, 
                normal, 
                view_direction, 
                light_direction
            );

            // Rendering equation:
            //
            // Lo += BRDF * Li * max(N·L, 0)
            direct_lighting += brdf * incident_radiance * n_dot_l;
        }
    }

    // Full result:
    //
    // color = direct + indirect + emissive
    //
    // This pass currently has no IBL resources, so indirect is omitted.
    float3 final_color = direct_lighting + emissive;
    output.color = float4(final_color, 1.0);

    return output;
}
