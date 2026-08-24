struct IblConstants {
    uint output_resolution;
    uint environment_resolution;
    uint sample_count;
    float roughness;
};

[[vk::binding(0, 0)]]
SamplerState environment_sampler;

[[vk::binding(1, 0)]]
TextureCube<float4> environment_map;

[[vk::binding(2, 0)]]
[[vk::image_format("rgba32f")]]
RWTexture2DArray<float4> output_cube;

[[vk::push_constant]]
ConstantBuffer<IblConstants> constants;

static const float PI = 3.14159265359;

float3 cube_direction(uint face, float2 uv) {
    if (face == 0) return normalize(float3(1.0, -uv.y, -uv.x));
    if (face == 1) return normalize(float3(-1.0, -uv.y, uv.x));
    if (face == 2) return normalize(float3(uv.x, 1.0, uv.y));
    if (face == 3) return normalize(float3(uv.x, -1.0, -uv.y));
    if (face == 4) return normalize(float3(uv.x, -uv.y, 1.0));
    return normalize(float3(-uv.x, -uv.y, -1.0));
}

float radical_inverse_vdc(uint bits) {
    bits = (bits << 16) | (bits >> 16);
    bits = ((bits & 0x55555555u) << 1) |
        ((bits & 0xAAAAAAAAu) >> 1);
    bits = ((bits & 0x33333333u) << 2) |
        ((bits & 0xCCCCCCCCu) >> 2);
    bits = ((bits & 0x0F0F0F0Fu) << 4) |
        ((bits & 0xF0F0F0F0u) >> 4);
    bits = ((bits & 0x00FF00FFu) << 8) |
        ((bits & 0xFF00FF00u) >> 8);
    return float(bits) * 2.3283064365386963e-10;
}

float2 hammersley(uint index, uint count) {
    return float2(float(index) / float(count), radical_inverse_vdc(index));
}

float3 importance_sample_ggx(float2 xi, float3 normal, float roughness) {
    const float alpha = roughness * roughness;
    const float alpha_squared = alpha * alpha;
    const float phi = 2.0 * PI * xi.x;
    const float cos_theta = sqrt(
        max((1.0 - xi.y) /
            (1.0 + (alpha_squared - 1.0) * xi.y), 0.0)
    );
    const float sin_theta = sqrt(max(1.0 - cos_theta * cos_theta, 0.0));
    const float3 halfway_tangent = float3(
        cos(phi) * sin_theta,
        sin(phi) * sin_theta,
        cos_theta
    );

    const float3 up = abs(normal.y) < 0.999 ?
        float3(0.0, 1.0, 0.0) : float3(1.0, 0.0, 0.0);
    const float3 tangent = normalize(cross(up, normal));
    const float3 bitangent = cross(normal, tangent);
    return normalize(
        tangent * halfway_tangent.x +
        bitangent * halfway_tangent.y +
        normal * halfway_tangent.z
    );
}

float distribution_ggx(float normal_dot_halfway, float roughness) {
    const float alpha = roughness * roughness;
    const float alpha_squared = alpha * alpha;
    const float denominator = normal_dot_halfway * normal_dot_halfway *
        (alpha_squared - 1.0) + 1.0;
    return alpha_squared / max(PI * denominator * denominator, 1.0e-7);
}

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    if (id.x >= constants.output_resolution ||
        id.y >= constants.output_resolution || id.z >= 6) {
        return;
    }

    const float2 uv =
        (float2(id.xy) + 0.5) / float(constants.output_resolution) *
        2.0 - 1.0;
    const float3 normal = cube_direction(id.z, uv);
    if (constants.roughness <= 1.0e-4) {
        output_cube[id] = environment_map.SampleLevel(
            environment_sampler,
            normal,
            0.0
        );
        return;
    }

    const float3 view_direction = normal;
    const float texel_solid_angle = 4.0 * PI /
        (6.0 * float(constants.environment_resolution) *
            float(constants.environment_resolution));
    float3 prefiltered = float3(0.0);
    float total_weight = 0.0;

    for (uint index = 0; index < constants.sample_count; ++index) {
        const float2 xi = hammersley(index, constants.sample_count);
        const float3 halfway_direction = importance_sample_ggx(
            xi,
            normal,
            constants.roughness
        );
        const float3 light_direction = normalize(
            2.0 * dot(view_direction, halfway_direction) *
                halfway_direction - view_direction
        );
        const float normal_dot_light = saturate(
            dot(normal, light_direction)
        );
        if (normal_dot_light <= 0.0) {
            continue;
        }

        const float normal_dot_halfway = saturate(
            dot(normal, halfway_direction)
        );
        const float halfway_dot_view = saturate(
            dot(halfway_direction, view_direction)
        );
        const float distribution = distribution_ggx(
            normal_dot_halfway,
            constants.roughness
        );
        const float pdf = max(
            distribution * normal_dot_halfway /
                max(4.0 * halfway_dot_view, 1.0e-6),
            1.0e-6
        );
        const float sample_solid_angle = 1.0 /
            (float(constants.sample_count) * pdf);
        const float mip_level = max(
            0.5 * log2(sample_solid_angle / texel_solid_angle),
            0.0
        );
        prefiltered += environment_map.SampleLevel(
            environment_sampler,
            light_direction,
            mip_level
        ).rgb * normal_dot_light;
        total_weight += normal_dot_light;
    }

    output_cube[id] = float4(
        prefiltered / max(total_weight, 1.0e-6),
        1.0
    );
}
