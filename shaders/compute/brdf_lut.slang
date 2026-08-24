struct IblConstants {
    uint output_resolution;
    uint environment_resolution;
    uint sample_count;
    float roughness;
};

[[vk::binding(0, 0)]]
[[vk::image_format("rgba32f")]]
RWTexture2D<float4> output_lut;

[[vk::push_constant]]
ConstantBuffer<IblConstants> constants;

static const float PI = 3.14159265359;

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

float3 importance_sample_ggx(float2 xi, float roughness) {
    const float alpha = roughness * roughness;
    const float alpha_squared = alpha * alpha;
    const float phi = 2.0 * PI * xi.x;
    const float cos_theta = sqrt(
        max((1.0 - xi.y) /
            (1.0 + (alpha_squared - 1.0) * xi.y), 0.0)
    );
    const float sin_theta = sqrt(max(1.0 - cos_theta * cos_theta, 0.0));
    return float3(
        cos(phi) * sin_theta,
        sin(phi) * sin_theta,
        cos_theta
    );
}

float geometry_schlick_ggx(float normal_dot_direction, float roughness) {
    const float k = roughness * roughness * 0.5;
    return normal_dot_direction / max(
        normal_dot_direction * (1.0 - k) + k,
        1.0e-7
    );
}

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    if (id.x >= constants.output_resolution ||
        id.y >= constants.output_resolution) {
        return;
    }

    const float normal_dot_view = clamp(
        (float(id.x) + 0.5) / float(constants.output_resolution),
        1.0e-4,
        1.0
    );
    const float roughness = clamp(
        (float(id.y) + 0.5) / float(constants.output_resolution),
        1.0e-4,
        1.0
    );
    const float3 view_direction = float3(
        sqrt(max(1.0 - normal_dot_view * normal_dot_view, 0.0)),
        0.0,
        normal_dot_view
    );

    float scale = 0.0;
    float bias = 0.0;
    for (uint index = 0; index < constants.sample_count; ++index) {
        const float2 xi = hammersley(index, constants.sample_count);
        const float3 halfway_direction = importance_sample_ggx(
            xi,
            roughness
        );
        const float3 light_direction = normalize(
            2.0 * dot(view_direction, halfway_direction) *
                halfway_direction - view_direction
        );
        const float normal_dot_light = saturate(light_direction.z);
        const float normal_dot_halfway = saturate(halfway_direction.z);
        const float halfway_dot_view = saturate(
            dot(halfway_direction, view_direction)
        );
        if (normal_dot_light <= 0.0) {
            continue;
        }

        const float geometry =
            geometry_schlick_ggx(normal_dot_view, roughness) *
            geometry_schlick_ggx(normal_dot_light, roughness);
        const float visibility = geometry * halfway_dot_view /
            max(normal_dot_halfway * normal_dot_view, 1.0e-6);
        const float fresnel = pow(1.0 - halfway_dot_view, 5.0);
        scale += (1.0 - fresnel) * visibility;
        bias += fresnel * visibility;
    }

    output_lut[id.xy] = float4(
        scale / float(constants.sample_count),
        bias / float(constants.sample_count),
        0.0,
        1.0
    );
}
