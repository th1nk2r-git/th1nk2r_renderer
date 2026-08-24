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

void tangent_basis(float3 normal, out float3 tangent, out float3 bitangent) {
    const float3 up = abs(normal.y) < 0.999 ?
        float3(0.0, 1.0, 0.0) : float3(1.0, 0.0, 0.0);
    tangent = normalize(cross(up, normal));
    bitangent = cross(normal, tangent);
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
    float3 tangent;
    float3 bitangent;
    tangent_basis(normal, tangent, bitangent);

    const float texel_solid_angle = 4.0 * PI /
        (6.0 * float(constants.environment_resolution) *
            float(constants.environment_resolution));
    float3 irradiance = float3(0.0);
    for (uint index = 0; index < constants.sample_count; ++index) {
        const float2 xi = hammersley(index, constants.sample_count);
        const float phi = 2.0 * PI * xi.x;
        const float cos_theta = sqrt(max(1.0 - xi.y, 0.0));
        const float sin_theta = sqrt(max(xi.y, 0.0));
        const float3 local_direction = float3(
            cos(phi) * sin_theta,
            sin(phi) * sin_theta,
            cos_theta
        );
        const float3 sample_direction = normalize(
            tangent * local_direction.x +
            bitangent * local_direction.y +
            normal * local_direction.z
        );

        const float pdf = max(cos_theta / PI, 1.0e-6);
        const float sample_solid_angle = 1.0 /
            (float(constants.sample_count) * pdf);
        const float mip_level = max(
            0.5 * log2(sample_solid_angle / texel_solid_angle),
            0.0
        );
        irradiance += environment_map.SampleLevel(
            environment_sampler,
            sample_direction,
            mip_level
        ).rgb;
    }

    irradiance *= PI / float(constants.sample_count);
    output_cube[id] = float4(irradiance, 1.0);
}
