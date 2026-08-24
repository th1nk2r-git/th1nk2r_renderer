struct Input {
    [[vk::location(0)]] float3 color;
    [[vk::location(1)]] float2 texcoord;
    [[vk::location(2)]] float3 world_position;
    [[vk::location(3)]] float3 world_normal;
    [[vk::location(4)]] float4 world_tangent;
};

struct Output {
    [[vk::location(0)]] float4 color;
};

struct GpuPointLight {
    float4 position_intensity;
    float4 color;
    float4 shadow_parameters;
};

struct ViewProjection {
    column_major matrix<float, 4, 4> view;
    column_major matrix<float, 4, 4> projection;
    float4 camera_position;
};

struct DrawConstants {
    column_major matrix<float, 4, 4> transform;
    float4 normal_column_0;
    float4 normal_column_1;
    float4 normal_column_2;
    uint point_light_count;
    uint padding_0;
    uint padding_1;
    uint padding_2;
};

struct MaterialParameters {
    float4 base_color_factor;
    float4 emissive_normal_scale;
    float4 metallic_roughness_occlusion_alpha_cutoff;
    uint4 flags;
};

struct ShadowSamplingParameters {
    float4 map_parameters;
};

[[vk::binding(0, 0)]]
ConstantBuffer<ViewProjection> camera;

[[vk::push_constant]]
ConstantBuffer<DrawConstants> draw;

[[vk::binding(0, 1)]]
SamplerState material_sampler;

[[vk::binding(1, 1)]]
Texture2D<float4> base_color_texture;

[[vk::binding(2, 1)]]
Texture2D<float4> metallic_roughness_texture;

[[vk::binding(3, 1)]]
Texture2D<float4> normal_texture;

[[vk::binding(4, 1)]]
Texture2D<float4> occlusion_texture;

[[vk::binding(5, 1)]]
Texture2D<float4> emissive_texture;

[[vk::binding(6, 1)]]
ConstantBuffer<MaterialParameters> material;

[[vk::binding(0, 2)]]
StructuredBuffer<GpuPointLight> point_lights;

[[vk::binding(0, 3)]]
SamplerState ibl_sampler;

[[vk::binding(1, 3)]]
TextureCube<float4> irradiance_map;

[[vk::binding(2, 3)]]
TextureCube<float4> prefiltered_environment_map;

[[vk::binding(3, 3)]]
Texture2D<float4> brdf_lut;

[[vk::binding(0, 4)]]
SamplerState shadow_sampler;

[[vk::binding(1, 4)]]
TextureCubeArray<float> point_shadow_maps;

[[vk::binding(2, 4)]]
ConstantBuffer<ShadowSamplingParameters> shadow_sampling;

static const float PI = 3.14159265359;
static const float IBL_INTENSITY = 1.0;

static const uint PCSS_SAMPLE_COUNT = 24;

static const float2 PCSS_DISK[PCSS_SAMPLE_COUNT] = {
    float2(-0.613392, 0.617481),
    float2(0.170019, -0.040254),
    float2(-0.299417, 0.791925),
    float2(0.645680, 0.493210),
    float2(-0.651784, 0.717887),
    float2(0.421003, 0.027070),
    float2(-0.817194, -0.271096),
    float2(-0.705374, -0.668203),
    float2(0.977050, -0.108615),
    float2(0.063326, 0.142369),
    float2(0.203528, 0.214331),
    float2(-0.667531, 0.326090),
    float2(-0.098422, -0.295755),
    float2(-0.885922, 0.215369),
    float2(0.566637, 0.605213),
    float2(0.039766, -0.396100),
    float2(0.751946, 0.453352),
    float2(0.078707, -0.715323),
    float2(-0.075838, -0.529344),
    float2(0.724479, -0.580798),
    float2(0.222999, -0.215125),
    float2(-0.467574, -0.405438),
    float2(-0.248268, -0.814753),
    float2(0.354411, -0.887570)
};

float3 safe_normalize(float3 value) {
    const float length_squared = max(dot(value, value), 1.0e-8);
    return value * rsqrt(length_squared);
}

float random_angle(float3 position) {
    const float value = dot(
        position,
        float3(12.9898, 78.233, 37.719)
    );
    return frac(sin(value) * 43758.5453) * (2.0 * PI);
}

float2 rotate_disk_sample(float2 sample_position, float angle) {
    const float sine = sin(angle);
    const float cosine = cos(angle);
    return float2(
        cosine * sample_position.x - sine * sample_position.y,
        sine * sample_position.x + cosine * sample_position.y
    );
}

float3 offset_cube_direction(
    float3 direction,
    float3 tangent,
    float3 bitangent,
    float2 disk_sample,
    float angular_radius
) {
    return safe_normalize(
        direction +
        tangent * disk_sample.x * angular_radius +
        bitangent * disk_sample.y * angular_radius
    );
}

float sample_point_shadow_depth(
    uint shadow_index,
    float3 direction
) {
    return point_shadow_maps.SampleLevel(
        shadow_sampler,
        float4(direction, float(shadow_index)),
        0.0
    );
}

float point_light_pcss_visibility(
    GpuPointLight light,
    float3 world_position,
    float normal_dot_light
) {
    if (light.shadow_parameters.x < 0.0 || normal_dot_light <= 0.0) {
        return 1.0;
    }

    const uint shadow_index = uint(light.shadow_parameters.x + 0.5);
    const float far_plane = max(light.shadow_parameters.y, 1.0e-3);
    const float source_radius = max(light.shadow_parameters.z, 0.0);
    const float3 light_to_fragment =
        world_position - light.position_intensity.xyz;
    const float receiver_distance = length(light_to_fragment);
    if (receiver_distance <= 1.0e-4 || receiver_distance >= far_plane) {
        return 1.0;
    }

    const float3 cube_direction = light_to_fragment / receiver_distance;
    const float3 helper_axis = abs(cube_direction.y) < 0.99
        ? float3(0.0, 1.0, 0.0)
        : float3(1.0, 0.0, 0.0);
    const float3 tangent = safe_normalize(
        cross(helper_axis, cube_direction)
    );
    const float3 bitangent = cross(cube_direction, tangent);
    const float rotation = random_angle(world_position);
    const float inverse_resolution =
        shadow_sampling.map_parameters.y;
    const float minimum_bias = shadow_sampling.map_parameters.z;
    const float maximum_filter_angle =
        shadow_sampling.map_parameters.w;
    const float bias = max(
        minimum_bias,
        minimum_bias * (1.0 + 3.0 * (1.0 - normal_dot_light))
    );
    const float biased_receiver_distance = max(
        receiver_distance - bias,
        0.0
    );

    const float search_angle = clamp(
        source_radius / max(receiver_distance, source_radius + 1.0e-4),
        inverse_resolution * 1.5,
        maximum_filter_angle * 0.5
    );
    float blocker_distance_sum = 0.0;
    uint blocker_count = 0;
    for (uint sample_index = 0;
        sample_index < PCSS_SAMPLE_COUNT;
        ++sample_index) {
        const float2 disk_sample = rotate_disk_sample(
            PCSS_DISK[sample_index],
            rotation
        );
        const float3 sample_direction = offset_cube_direction(
            cube_direction,
            tangent,
            bitangent,
            disk_sample,
            search_angle
        );
        const float sample_distance = sample_point_shadow_depth(
            shadow_index,
            sample_direction
        ) * far_plane;
        if (sample_distance < biased_receiver_distance) {
            blocker_distance_sum += sample_distance;
            ++blocker_count;
        }
    }

    if (blocker_count == 0) {
        return 1.0;
    }

    const float average_blocker_distance =
        blocker_distance_sum / float(blocker_count);
    const float penumbra_width = source_radius * max(
        receiver_distance - average_blocker_distance,
        0.0
    ) / max(average_blocker_distance, 1.0e-3);
    const float filter_angle = clamp(
        penumbra_width / receiver_distance,
        inverse_resolution * 1.5,
        maximum_filter_angle
    );

    float visibility = 0.0;
    for (uint sample_index = 0;
        sample_index < PCSS_SAMPLE_COUNT;
        ++sample_index) {
        const float2 disk_sample = rotate_disk_sample(
            PCSS_DISK[sample_index],
            rotation + 1.57079632679
        );
        const float3 sample_direction = offset_cube_direction(
            cube_direction,
            tangent,
            bitangent,
            disk_sample,
            filter_angle
        );
        const float sample_distance = sample_point_shadow_depth(
            shadow_index,
            sample_direction
        ) * far_plane;
        visibility += biased_receiver_distance <= sample_distance
            ? 1.0
            : 0.0;
    }
    return visibility / float(PCSS_SAMPLE_COUNT);
}

float3 sample_world_normal(Input input) {
    const float3 vertex_normal = safe_normalize(input.world_normal);
    const float3 tangent = safe_normalize(
        input.world_tangent.xyz -
        vertex_normal * dot(vertex_normal, input.world_tangent.xyz)
    );
    const float handedness = input.world_tangent.w < 0.0 ? -1.0 : 1.0;
    const float3 bitangent = cross(vertex_normal, tangent) * handedness;

    float3 tangent_normal = normal_texture.Sample(
        material_sampler,
        input.texcoord
    ).xyz * 2.0 - 1.0;
    tangent_normal.xy *= material.emissive_normal_scale.w;
    tangent_normal = safe_normalize(tangent_normal);

    return safe_normalize(
        tangent * tangent_normal.x +
        bitangent * tangent_normal.y +
        vertex_normal * tangent_normal.z
    );
}

float distribution_ggx(float normal_dot_halfway, float roughness) {
    const float alpha = roughness * roughness;
    const float alpha_squared = alpha * alpha;
    const float denominator = normal_dot_halfway * normal_dot_halfway *
        (alpha_squared - 1.0) + 1.0;
    return alpha_squared / max(PI * denominator * denominator, 1.0e-7);
}

float geometry_schlick_ggx(float normal_dot_direction, float roughness) {
    const float radius = roughness + 1.0;
    const float k = radius * radius * 0.125;
    return normal_dot_direction / max(
        normal_dot_direction * (1.0 - k) + k,
        1.0e-7
    );
}

float geometry_smith(
    float normal_dot_view,
    float normal_dot_light,
    float roughness
) {
    return geometry_schlick_ggx(normal_dot_view, roughness) *
        geometry_schlick_ggx(normal_dot_light, roughness);
}

float3 fresnel_schlick(float cosine, float3 reflectance_at_normal) {
    const float one_minus_cosine = 1.0 - saturate(cosine);
    const float factor = one_minus_cosine * one_minus_cosine *
        one_minus_cosine * one_minus_cosine * one_minus_cosine;
    return reflectance_at_normal +
        (float3(1.0) - reflectance_at_normal) * factor;
}

float3 fresnel_schlick_roughness(
    float cosine,
    float3 reflectance_at_normal,
    float roughness
) {
    const float one_minus_cosine = 1.0 - saturate(cosine);
    const float factor = one_minus_cosine * one_minus_cosine *
        one_minus_cosine * one_minus_cosine * one_minus_cosine;
    return reflectance_at_normal +
        (max(float3(1.0 - roughness), reflectance_at_normal) -
            reflectance_at_normal) * factor;
}

Output main(Input input) {
    Output output;
    const float4 base_color = base_color_texture.Sample(
        material_sampler,
        input.texcoord
    ) * material.base_color_factor * float4(input.color, 1.0);

    if (material.flags.x != 0 &&
        base_color.a <
            material.metallic_roughness_occlusion_alpha_cutoff.w) {
        discard;
    }

    const float4 metallic_roughness = metallic_roughness_texture.Sample(
        material_sampler,
        input.texcoord
    );
    const float metallic = saturate(
        metallic_roughness.b *
        material.metallic_roughness_occlusion_alpha_cutoff.x
    );
    const float roughness = clamp(
        metallic_roughness.g *
            material.metallic_roughness_occlusion_alpha_cutoff.y,
        0.045,
        1.0
    );
    const float occlusion = lerp(
        1.0,
        occlusion_texture.Sample(material_sampler, input.texcoord).r,
        material.metallic_roughness_occlusion_alpha_cutoff.z
    );
    const float3 emissive = emissive_texture.Sample(
        material_sampler,
        input.texcoord
    ).rgb * material.emissive_normal_scale.xyz;

    const float3 normal = sample_world_normal(input);
    const float3 view_direction = safe_normalize(
        camera.camera_position.xyz - input.world_position
    );
    const float normal_dot_view = saturate(dot(normal, view_direction));
    const float3 reflectance_at_normal = lerp(
        float3(0.04),
        base_color.rgb,
        metallic
    );
    float3 direct_lighting = float3(0.0);

    for (uint index = 0; index < draw.point_light_count; ++index) {
        const GpuPointLight light = point_lights[index];
        const float3 to_light = light.position_intensity.xyz - input.world_position;
        const float distance_squared = max(dot(to_light, to_light), 0.01);
        const float3 light_direction = to_light * rsqrt(distance_squared);
        const float3 halfway_direction = safe_normalize(
            light_direction + view_direction
        );
        const float normal_dot_light = saturate(
            dot(normal, light_direction)
        );
        const float normal_dot_halfway = saturate(
            dot(normal, halfway_direction)
        );
        const float halfway_dot_view = saturate(
            dot(halfway_direction, view_direction)
        );

        const float distribution = distribution_ggx(
            normal_dot_halfway,
            roughness
        );
        const float geometry = geometry_smith(
            normal_dot_view,
            normal_dot_light,
            roughness
        );
        const float3 fresnel = fresnel_schlick(
            halfway_dot_view,
            reflectance_at_normal
        );
        const float3 specular =
            distribution * geometry * fresnel /
            max(4.0 * normal_dot_view * normal_dot_light, 1.0e-6);
        const float3 diffuse_weight =
            (float3(1.0) - fresnel) * (1.0 - metallic);

        const float3 radiance = light.color.rgb *
            max(light.position_intensity.w, 0.0) /
            distance_squared;
        const float visibility = point_light_pcss_visibility(
            light,
            input.world_position,
            normal_dot_light
        );
        direct_lighting += (
            diffuse_weight * base_color.rgb / PI + specular
        ) * radiance * normal_dot_light * visibility;
    }

    const float3 ibl_fresnel = fresnel_schlick_roughness(
        normal_dot_view,
        reflectance_at_normal,
        roughness
    );
    const float3 ibl_diffuse_weight =
        (float3(1.0) - ibl_fresnel) * (1.0 - metallic);
    const float3 irradiance = irradiance_map.Sample(
        ibl_sampler,
        normal
    ).rgb;
    const float3 diffuse_ibl = irradiance * base_color.rgb / PI;

    const float3 reflection_direction = reflect(-view_direction, normal);
    const float3 prefiltered_environment =
        prefiltered_environment_map.SampleLevel(
            ibl_sampler,
            reflection_direction,
            roughness * 7.0
        ).rgb;
    const float2 brdf = brdf_lut.Sample(
        ibl_sampler,
        float2(normal_dot_view, roughness)
    ).rg;
    const float3 specular_ibl = prefiltered_environment *
        (ibl_fresnel * brdf.x + brdf.y);
    const float3 ambient =
        (ibl_diffuse_weight * diffuse_ibl + specular_ibl) *
        occlusion * IBL_INTENSITY;
    const float3 lighting = ambient + direct_lighting + emissive;
    const float3 mapped_color = lighting / (lighting + float3(1.0));
    output.color = float4(mapped_color, base_color.a);
    return output;
}
