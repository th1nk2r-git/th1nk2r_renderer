struct IblConstants {
    uint output_resolution;
    uint environment_resolution;
    uint sample_count;
    float roughness;
};

[[vk::binding(0, 0)]]
SamplerState panorama_sampler;

[[vk::binding(1, 0)]]
Texture2D<float4> panorama;

[[vk::binding(2, 0)]]
[[vk::image_format("rgba32f")]]
RWTexture2DArray<float4> output_cube;

[[vk::push_constant]]
ConstantBuffer<IblConstants> constants;

static const float PI = 3.14159265359;

float3 cube_direction(uint face, float2 uv) {
    if (face == 0) {
        return normalize(float3(1.0, -uv.y, -uv.x));
    }
    if (face == 1) {
        return normalize(float3(-1.0, -uv.y, uv.x));
    }
    if (face == 2) {
        return normalize(float3(uv.x, 1.0, uv.y));
    }
    if (face == 3) {
        return normalize(float3(uv.x, -1.0, -uv.y));
    }
    if (face == 4) {
        return normalize(float3(uv.x, -uv.y, 1.0));
    }
    return normalize(float3(-uv.x, -uv.y, -1.0));
}

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    if (id.x >= constants.output_resolution ||
        id.y >= constants.output_resolution || id.z >= 6) {
        return;
    }

    const float2 uv = (float2(id.xy) + 0.5) / float(constants.output_resolution) * 2.0 - 1.0;
    const float3 direction = cube_direction(id.z, uv);
    const float2 panorama_uv = float2(
        atan2(direction.z, direction.x) / (2.0 * PI) + 0.5,
        0.5 - asin(clamp(direction.y, -1.0, 1.0)) / PI
    );
    output_cube[id] = panorama.SampleLevel(
        panorama_sampler,
        panorama_uv,
        0.0
    );
}
