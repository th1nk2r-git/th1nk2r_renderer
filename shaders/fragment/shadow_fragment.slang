struct Input {
    [[vk::location(0)]] float3 world_position;
    [[vk::location(1)]] float2 texcoord;
};

struct ShadowFace {
    column_major matrix<float, 4, 4> light_view_projection;
    float4 light_position_far;
};

struct ShadowDrawConstants {
    column_major matrix<float, 4, 4> transform;
    uint face_index;
    uint padding_0;
    uint padding_1;
    uint padding_2;
};

struct ShadowMaterialParameters {
    float4 alpha;
};

[[vk::binding(0, 0)]]
StructuredBuffer<ShadowFace> shadow_faces;

[[vk::binding(0, 1)]]
SamplerState material_sampler;

[[vk::binding(1, 1)]]
Texture2D<float4> base_color_texture;

[[vk::binding(2, 1)]]
ConstantBuffer<ShadowMaterialParameters> material;

[[vk::push_constant]]
ConstantBuffer<ShadowDrawConstants> draw;

float main(Input input) : SV_Depth {
    if (material.alpha.z > 0.5) {
        const float alpha = base_color_texture.Sample(
            material_sampler,
            input.texcoord
        ).a * material.alpha.x;
        if (alpha < material.alpha.y) {
            discard;
        }
    }

    const ShadowFace face = shadow_faces[draw.face_index];
    const float distance_to_light = length(
        input.world_position - face.light_position_far.xyz
    );
    return saturate(
        distance_to_light / max(face.light_position_far.w, 1.0e-4)
    );
}
